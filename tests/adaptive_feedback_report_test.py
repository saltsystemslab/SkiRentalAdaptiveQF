import csv
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "experiments"))
from report import read_adversary
from paper_report import build_paper_sections
from run import recipes, select_cases


class AdaptiveFeedbackReportTest(unittest.TestCase):
    def test_medium_recipe(self):
        case, = select_cases(recipes("medium", 741262640), ["adaptive-adversary"])
        self.assertEqual(case["id"], "adaptive-adversary/immediate-feedback")
        self.assertEqual(case["config"]["num_queries"], 30_000_000)
        self.assertEqual(case["config"]["quotient_bits"], 24)
        self.assertEqual(case["config"]["adv_freq"], 5)
        self.assertEqual(case["config"]["hybrid_window_size"], 100_000)
        self.assertNotIn("adversary_window_size", case["config"])
        for size in ["small", "large"]:
            other, = select_cases(recipes(size, 741262640), ["adaptive-adversary"])
            self.assertEqual(other["config"]["hybrid_window_size"], 1_000_000)

    def test_trace_accounting_and_intervals(self):
        with tempfile.TemporaryDirectory() as scratch:
            trace = Path(scratch) / "trace.csv"
            trace.write_text(
                "query_index,key,source,outcome,repeat_before,repeat_after,active_candidates\n"
                "2,7,fresh,tolerated,0,1,1\n"
                "5,7,replay,adapted,1,0,0\n"
                "8,9,fresh,tolerated,0,1,1\n")
            metrics, fractions, replays = read_adversary(
                trace, dict(num_queries=10, num_rounds=2, adv_freq=50))
            self.assertEqual(fractions, [0.6, 0.4])
            self.assertEqual(metrics["repeat_query_fraction"], 0.5)
            self.assertEqual(metrics["regime_switches"], 3)
            self.assertEqual(metrics["uniform_false_positives"], 2)
            self.assertEqual(metrics["repeat_adaptations"], 1)
            self.assertEqual(replays, 1)
            trace.write_text(trace.read_text().replace("5,7,replay,adapted,1,0", "5,7,replay,adapted,1,1"))
            with self.assertRaises(ValueError):
                read_adversary(trace, dict(num_queries=10, num_rounds=2, adv_freq=50))

    def test_current_and_historical_report_labels(self):
        for window in [None, 100, 100_000]:
            with self.subTest(window=window), tempfile.TemporaryDirectory() as scratch:
                root = Path(scratch)
                config = dict(num_queries=100, num_rounds=2)
                if window is not None:
                    config["adversary_window_size"] = window
                else:
                    config["hybrid_window_size"] = 100_000
                metrics = dict(throughput=1.6, fpr=0.03, uniform_adaptations=1, repeat_adaptations=1)
                rounds = [dict(round_thput=4, round_fp=1, repeat_query_fraction=1),
                          dict(round_thput=1, round_fp=2, repeat_query_fraction=0)]
                case = "adaptive-adversary/" + (f"window-{window}" if window else "immediate-feedback")
                groups = {case: dict(config=config, filters={
                    "skiQF": [dict(metrics=metrics, rounds=rounds)]})}
                body, snippets = build_paper_sections(groups, root)
                text = "\n".join(body)
                if window:
                    self.assertIn(f"observation window of {window:,} queries", text)
                else:
                    self.assertIn("Perfect recognition of ordinary query outcomes", text)
                    self.assertIn("detector window is 100,000 negative-key queries", text)
                    self.assertIn("adaptive-adversary-regimes.tex", snippets)
                    self.assertIn("adaptive-adversary-corrections.tex", snippets)
                self.assertIn("(0,1.6)", (root / "adaptive-adversary-overall.tex").read_text())
                self.assertIn("(5e-05,4)", (root / "adaptive-adversary-trajectories.tex").read_text())


if __name__ == "__main__":
    unittest.main()
