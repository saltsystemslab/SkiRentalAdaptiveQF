"""Regression checks for sequential scratch reuse and recorded workload seeds."""
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

BINARY = Path(__file__).resolve().parents[1] / "sponge/build/workload_gen"


class WorkloadGenerationTest(unittest.TestCase):
    def test_singleton_mixtures(self):
        """Generate all five singleton mixtures and inspect actual key multiplicities and stats.

        Check query budgets, absence from the insert set, rounding, and seeded reproducibility.
        """
        import collections
        import json
        for fraction in [0, .25, .5, .75, 1]:
            with tempfile.TemporaryDirectory() as scratch:
                cmd = [str(BINARY), "-q", "8", "--numQueries", "1001", "--queryWorkload", "singleton-mixture", "--singletonFraction", str(fraction), "--frequencyStats", "--seed", "42"]
                subprocess.run(cmd, cwd=scratch, check=True, stdout=subprocess.DEVNULL)
                root = Path(scratch)
                def keys(name):
                    raw = (root/name).read_bytes()
                    return struct.unpack("Q"*((len(raw)-8)//8), raw[8:])
                query = keys("querySet")
                counts = collections.Counter(query)
                self.assertEqual(len(query), 1001)
                self.assertTrue(set(counts.values()) <= {1, 2})
                self.assertFalse(set(query) & set(keys("insertSet")))
                stats = json.loads((root/"workload_stats.json").read_text())
                self.assertEqual(stats["singleton_keys"], sum(v==1 for v in counts.values()))
                self.assertAlmostEqual(stats["singleton_key_fraction"], fraction, delta=.003)
                digest = (root/"querySet").read_bytes()
                subprocess.run(cmd, cwd=scratch, check=True, stdout=subprocess.DEVNULL)
                self.assertEqual(digest, (root/"querySet").read_bytes())

    def test_seeded_workloads_and_shorter_file_reuse(self):
        """Check seeded distribution files for reproducibility and valid lengths/headers.

        Regenerating a shorter workload must truncate stale queries from the prior file.
        """
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)

            def generate(workload, queries, seed):
                subprocess.run([str(BINARY), "-q", "8", "-r", "8", "--numQueries", str(queries),
                                "--queryWorkload", workload, "--seed", str(seed),
                                "--hashAgainForZipfian"], cwd=root, check=True,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                digests = []
                for name, expected in [("insertSet", int((1 << 8) * 0.9)), ("querySet", queries)]:
                    data = (root / name).read_bytes()
                    self.assertEqual(struct.unpack("Q", data[:8])[0], expected)
                    self.assertEqual(len(data), (expected + 1) * 8)
                    if name == "insertSet" and seed == 42:
                        # The first mt19937_64(42) output must survive serialization.
                        self.assertEqual(struct.unpack("Q", data[8:16])[0], 13930160852258120406)
                    digests.append(hashlib.sha256(data).hexdigest())
                return digests

            for workload in ["uniform", "normal", "lognormal", "zipfian"]:
                original = generate(workload, 1000, 42)
                self.assertEqual(original, generate(workload, 1000, 42))
                self.assertNotEqual(original, generate(workload, 1000, 43))
                generate(workload, 100, 42)  # Must truncate the previous larger querySet.


if __name__ == "__main__":
    unittest.main()
