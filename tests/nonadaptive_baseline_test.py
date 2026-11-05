"""The physical baseline must open only its forward database."""
from pathlib import Path
import subprocess
import tempfile
import unittest

BUILD = Path(__file__).resolve().parents[1] / "sponge/build"


class NonadaptiveBaselineTest(unittest.TestCase):
    def test_no_reverse_database_for_either_engine_recipe(self):
        """Run the real nonadaptive benchmark and verify it opens only the forward database.

        Neither reverse-map engine setting should create reverse-map storage.
        """
        for reverse_engine in ["wiredTiger", "splinterDB"]:
            with self.subTest(engine=reverse_engine), tempfile.TemporaryDirectory() as scratch:
                root = Path(scratch)
                subprocess.run([str(BUILD / "workload_gen"), "-q", "14", "-r", "8",
                                "--numQueries", "1000", "--queryWorkload", "uniform", "--seed", "42"],
                               cwd=root, check=True, stdout=subprocess.DEVNULL)
                result = subprocess.run([
                    str(BUILD / "bench_variants"), "--filter", "nonAdaptive", "-q", "14", "-r", "8",
                    "--numRounds", "10", "--storageEngine", "wiredTiger",
                    "--reverseMapEngine", reverse_engine, "--storageCacheSizeMB", "68",
                    "--reverseMapCacheSizeMB", "4", "--seed", "4294967295"],
                    cwd=root, check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                self.assertTrue((root / "database_wiredTiger").is_dir())
                self.assertFalse((root / "reverseMap_wiredTiger").exists())
                self.assertFalse((root / "reverseMap_splinterDB").exists())
                self.assertEqual(result.stdout.count("ConnectionConfig:"), 2)
                self.assertEqual(len((root / "nonAdaptive.csv").read_text().splitlines()), 11)


if __name__ == "__main__":
    unittest.main()
