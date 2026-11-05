import argparse
import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "experiments"))
import run as runner
from report import generate_report, read_metrics
from paper_report import build_paper_sections, comparison_table, round_series


class ExperimentRunnerTest(unittest.TestCase):
    def test_recipe_matrix_and_presets(self):
        for size, (qbits, queries) in runner.PRESETS.items():
            cases = runner.recipes(size, 42)
            self.assertEqual(len(cases), 43)
            self.assertEqual(len({case["id"] for case in cases}), 43)
            adaptive = runner.select_cases(cases, ["adaptive-adversary"])[0]["config"]
            self.assertEqual(adaptive["quotient_bits"], qbits)
            self.assertEqual(adaptive["num_queries"], 30_000_000 if size == "medium" else queries)
            self.assertEqual(adaptive["filter_names"], runner.FILTERS)
            self.assertNotIn("adversary_window_size", adaptive)
            engines = runner.select_cases(cases, ["engine-comparison"])
            self.assertEqual(len(engines), 2)
            self.assertTrue(all(case["config"]["reverse_map_cache_size_mb"] == 64 for case in engines))
        self.assertEqual(len(runner.select_cases(cases, ["distributions,distributions/uniform"])), 4)
        with self.assertRaises(ValueError):
            runner.select_cases(cases, ["unknown"])

    def test_parse_whitespace_rounds_and_text_summary(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "skiQF.csv").write_text(
                "round round_thput round_fp round_tp cumulative_thput cumulative_fp "
                "cumulative_adversarial_queries round_adapts cumulative_adapts load_factor\n"
                "0 4 1 0 4 1 0 0 0 0.9\n1 1 2 0 1.6 3 10 2 2 0.9\n")
            (root / "skiQF_summary.csv").write_text(
                "FPR:0.030000\nQueries: 100\nFilterInsert Thput:2\nSystemInsert Thput:1\n"
                "True Queries Thput:8\nQueries Thput:12\nSize(B): 1048576 Size(MB): 1\n")
            (root / "db_stats.csv").write_text(
                "name,block_manager_bytes_read,connection_read_ios\nskiQF,2097152,17\n")
            config = dict(num_queries=100, num_rounds=2, collect_db_stats=True)
            metrics, rounds = read_metrics(root, "skiQF", config)
            self.assertEqual(metrics["throughput"], 1.6)  # Not the arithmetic interval mean.
            self.assertEqual(metrics["filter_query"], 12)
            self.assertEqual(metrics["fpr"], 0.03)
            self.assertEqual(metrics["db_read_mib"], 2)
            self.assertEqual(metrics["db_read_ios"], 17)
            self.assertEqual(len(rounds), 2)
            with self.assertRaises(ValueError):
                read_metrics(root, "skiQF", {**config, "num_rounds": 3})
            config.update(filter_names="skiQF", query_workload="uniform", seed=42)
            (root / "run.json").write_text(json.dumps(dict(
                size="small", seed=42, repetitions=2, started="test", status="INCOMPLETE",
                host="test_host", source_revision="test", source_dirty=True,
                selected_cases=["distributions/uniform", "phased/uniform-first"], cases=[
                    dict(id="distributions/uniform", config=config, repetition=1,
                         status="COMPLETED", artifacts="."),
                    dict(id="distributions/uniform", config=config, repetition=2,
                         status="FAILED", error="failed test")])) )
            generate_report(root, compile_pdf=False)
            document = (root / "results.tex").read_text()
            self.assertIn("1/4", document)
            self.assertIn("Selected cases not started", document)
            self.assertIn("Incomplete result", (root / "appendix-content.tex").read_text())
            self.assertIn("Robustness across workload distributions", document)
            self.assertIn("appendix-content.tex", document)
            snippet = (root / "paper/distributions--uniform-figure.tex").read_text()
            self.assertNotIn(str(root), snippet)
            self.assertIn("1.6", snippet)
            self.assertTrue((root / "metrics.csv").is_file())

    def test_paper_grouping_units_phase_boundaries_and_paired_baselines(self):
        config = dict(num_queries=100, num_rounds=2, num_phases=4,
                      start_with_adversarial_phase=False)
        rounds = [dict(round_thput=4, round_fp=1, load_factor=0.9),
                  dict(round_thput=1, round_fp=2, load_factor=0.91)]
        metrics = dict(throughput=1.6, fpr=0.03, db_read_mib=1024,
                       rm_read_mib=2048, total_read_mib=3072, adaptations=2,
                       filter_insert=6, filter_query=12)
        group = dict(config=config, filters={"skiQF": [dict(metrics=metrics, rounds=rounds, repetition=1)]})
        groups = {"distributions/uniform": group, "cost-ratios/uniform-half": group,
                  "phased/uniform-first": group, "microbenchmarks/uniform": group}
        self.assertEqual(round_series(group, "skiQF", "interval_fpr"),
                         [(0.00005, [0.02]), (0.0001, [0.04])])
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            body, snippets = build_paper_sections(groups, root)
            self.assertIn("distribution-overall-throughput.tex", snippets)
            self.assertIn("(0,1.6)", (root / "distribution-overall-throughput.tex").read_text())
            db_io = (root / "distribution-database-io.tex").read_text()
            self.assertIn("(0,1)", db_io)  # 1024 MiB -> 1 GiB.
            self.assertIn("(0,3e-06)", db_io)  # FPR .03 * 100 queries -> 3 FP.
            costs = (root / "cost-ratio-throughput.tex").read_text()
            self.assertIn("(0.5,1.6) (1,1.6)", costs)
            self.assertNotIn("(2,0)", costs)  # Omitted recipes must not become zero.
            phases = (root / "phased-throughput.tex").read_text()
            self.assertIn(r"\fill[blue!15] (rel axis cs:0,0) rectangle (rel axis cs:0.25,1)", phases)
            self.assertIn(r"\fill[red!15] (rel axis cs:0.25,0) rectangle (rel axis cs:0.5,1)", phases)
            self.assertIn("xmin=0,xmax=0.0001,enlarge x limits=false", phases)
            self.assertIn("precision=3", (root / "microbenchmark-load-factor.tex").read_text())
            micro = (root / "microbenchmark-throughput.tex").read_text()
            self.assertIn("(0,6) (1,12)", micro)
            self.assertIn("skiqfSkiBar", micro)
            self.assertNotIn(str(root), "\n".join(path.read_text() for path in root.glob("*.tex")))
        engine = dict(filters={
            "adaptive": [dict(repetition=2, metrics={"system_insert": 4}),
                         dict(repetition=1, metrics={"system_insert": 1})],
            "nonAdaptive": [dict(repetition=1, metrics={"system_insert": 2}),
                            dict(repetition=2, metrics={"system_insert": 10})]})
        table = comparison_table({"engine-comparison/wiredTiger": engine},
                                 [("engine-comparison/wiredTiger", "WiredTiger"),
                                  ("engine-comparison/splinterDB", "SplinterDB")])
        self.assertIn(r"45 $\pm$ 7.07", table)  # Paired 50%, 40%; not a ratio of means.

    def test_adaptive_report_uses_saved_window_and_case(self):
        for window in [100, 100_000]:
            with self.subTest(window=window), tempfile.TemporaryDirectory() as scratch:
                root = Path(scratch)
                case = f"adaptive-adversary/window-{window}"
                group = dict(config=dict(num_queries=100, num_rounds=2,
                                         adversary_window_size=window), filters={
                    "skiQF": [dict(metrics=dict(throughput=1.6, fpr=0.03), rounds=[
                        dict(round_thput=4, round_fp=1), dict(round_thput=1, round_fp=2)])]})
                body, snippets = build_paper_sections({case: group}, root)
                self.assertIn(f"observation window of {window:,} queries", "\n".join(body))
                self.assertIn("adaptive-adversary-trajectories.tex", snippets)
                self.assertIn("(0,1.6)", (root / "adaptive-adversary-overall.tex").read_text())
                self.assertIn("(5e-05,4)", (root / "adaptive-adversary-trajectories.tex").read_text())

    def test_failures_continue_and_existing_results_are_protected(self):
        with tempfile.TemporaryDirectory() as scratch:
            output = Path(scratch) / "run"
            args = argparse.Namespace(size="small", seed=42, repetitions=1,
                                      tests=["phased"], output=output, dry_run=False, no_pdf=True)
            with patch.object(runner.subprocess, "check_output", side_effect=subprocess.CalledProcessError(128, "git")), \
                 patch.object(runner.subprocess, "run", side_effect=[subprocess.CalledProcessError(1, "bench"), None]) as execute, \
                 patch.object(runner, "validate_artifacts"), \
                 patch.object(runner, "generate_report"), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(runner.run(args), 1)
            self.assertEqual(execute.call_count, 2)
            manifest = json.loads((output / "run.json").read_text())
            self.assertEqual(manifest["status"], "INCOMPLETE")
            self.assertEqual(manifest["source_revision"], "unknown")
            self.assertIsNone(manifest["source_dirty"])
            self.assertEqual([case["status"] for case in manifest["cases"]], ["FAILED", "COMPLETED"])
            with self.assertRaises(ValueError):
                runner.run(args)


if __name__ == "__main__":
    unittest.main()
