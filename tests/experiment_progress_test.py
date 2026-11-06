import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'experiments'))
from progress import CaseProgress


class ExperimentProgressTest(unittest.TestCase):
    def test_phases_rounds_and_unrelated_output(self):
        """Recognize construction stages and uneven query boundaries from real log syntax.

        Ignore headers, unrelated output, duplicate rounds, and invalid round indices.
        """
        with contextlib.redirect_stderr(io.StringIO()):
            monitor = CaseProgress(None, dict(num_queries=103, num_rounds=3))
            monitor.consume('Writing to randomizedSkiQF.csv\n')
            self.assertEqual(monitor.filter, 'randomizedSkiQF')
            monitor.consume('Beginning bulkLoad\n')
            self.assertIn('bulk load', monitor.phase)
            monitor.consume('Done. starting test\n')
            for row in ['round round_thput', 'garbage', '0 1 0 0 1 0 0 0 0 .9']:
                monitor.consume(row)
            self.assertEqual(monitor.bar.n, 34)
            monitor.consume('0 1 0 0 1 0 0 0 0 .9')
            monitor.consume('99 1 0 0 1 0 0 0 0 .9')
            self.assertEqual(monitor.bar.n, 34)
            monitor.consume('2 1 0 0 1 0 0 0 0 .9')
            self.assertEqual(monitor.bar.n, 103)
            monitor.consume('Done with the test\n')
            self.assertIn('microbenchmarks', monitor.phase)
            monitor.bar.close()

    def test_monitor_drains_log_on_exit(self):
        """Read the last progress messages before stopping, even for a fast process."""
        with tempfile.TemporaryDirectory() as tmp, contextlib.redirect_stderr(io.StringIO()):
            path = Path(tmp) / 'execution.log'
            path.touch()
            with CaseProgress(path, dict(num_queries=100, num_rounds=1)) as monitor:
                with path.open('a') as log:
                    log.write('Writing to skiQF.csv\nDone. starting test\n0 1 0 0 1 0 0 0 0 .9\n')
            self.assertEqual(monitor.bar.n, 100)


if __name__ == '__main__':
    unittest.main()
