#!/usr/bin/env python3
"""Sequential TODS experiment recipes and result collection."""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import platform
import shlex
import subprocess
import sys

from report import generate_report, read_metrics

ROOT = Path(__file__).resolve().parents[1]
FILTERS = "adaptive,nonAdaptive,skiQF,randomizedSkiQF,hybridSkiQF"
PRESETS = {"small": (20, 1_000_000), "medium": (24, 10_000_000),
           "large": (27, 100_000_000)}
FAMILIES = {
    "singleton-mixture": "Once-or-twice keys at five singleton fractions.",
    "distributions": "Throughput, false positives, adaptation and I/O across skew levels.",
    "cost-ratios": "Sensitivity to database/reverse-map I/O cost ratios.",
    "microbenchmarks": "Filter-only insertion and query costs, using mock storage.",
    "phased": "Response to four alternating uniform/adversarial phases.",
    "engine-comparison": "System insertion with WiredTiger versus SplinterDB reverse maps.",
    "adversarial": "Fixed replay frequency crossed with database cache budget.",
    "adaptive-adversary": "Feedback-driven replay against each filter independently.",
    "reverse-map-comparison": "Effect of 4 MB versus 64 MB reverse-map cache.",
}


def recipes(size, seed):
    qbits, queries = PRESETS[size]
    common = dict(quotient_bits=qbits, remainder_bits=8, num_queries=queries,
                  num_rounds=100, seed=seed, storage_engine="wiredTiger",
                  reverse_map_engine="wiredTiger", storage_cache_size_mb=64,
                  reverse_map_cache_size_mb=4, break_even=2,
                  collect_db_stats=True, capture_extra_stats=True,
                  filter_names=FILTERS, work_dir=str(ROOT / "sponge"),
                  storage_sleep_us=0, reverse_sleep_us=0)
    cases = []

    def add(family, name, **changes):
        cases.append(dict(id=f"{family}/{name}", family=family,
                          config={**common, **changes}))

    distributions = [("uniform", "uniform"), ("normal", "normal"),
                     ("lognormal", "lognormal"), ("zipf", "zipfian")]
    for name, workload in distributions:
        add("distributions", name, query_workload=workload, zipf_constant=0.99, collect_workload_stats=True)
    for percent in [0,25,50,75,100]:
        add("singleton-mixture", f"f-{percent:03d}", query_workload="singleton-mixture",
            singleton_fraction=percent/100, collect_workload_stats=True)
    for name, workload in distributions:
        for ratio, overrides in [
            ("half", dict(break_even=1, storage_sleep_us=130)),
            ("twice", dict(break_even=3, reverse_sleep_us=130)),
            ("quad", dict(break_even=5, reverse_sleep_us=390)),
        ]:
            add("cost-ratios", f"{name}-{ratio}", query_workload=workload,
                zipf_constant=0.99, **overrides)
    for name, workload in distributions:
        add("microbenchmarks", name, query_workload=workload, zipf_constant=0.99,
            num_queries=queries * 5, microbench=True, collect_db_stats=False,
            capture_extra_stats=False, reverse_map_cache_size_mb=64)
    for name, adversarial_first in [("uniform-first", False), ("adversarial-first", True)]:
        add("phased", name, query_workload="uniform", is_phased_test=True,
            num_phases=4, start_with_adversarial_phase=adversarial_first)
    for engine in ["wiredTiger", "splinterDB"]:
        add("engine-comparison", engine, query_workload="uniform",
            quotient_bits=min(qbits, 24), num_queries=1_000_000,
            reverse_map_engine=engine, reverse_map_cache_size_mb=64,
            collect_db_stats=False, filter_names="adaptive,nonAdaptive",
            sort_and_insert_keys=False, sort_and_insert_fingerprints=False,
            is_insert_test=True)
    for cache in [64, 128, 256]:
        for freq in [1, 5, 10]:
            add("adversarial", f"frequency-{freq}-cache-{cache}",
                query_workload="adversarial", storage_cache_size_mb=cache, adv_freq=freq)
    add("adaptive-adversary", "immediate-feedback", query_workload="adaptive-adversarial",
        num_queries=30_000_000 if size == "medium" else queries,
        adv_freq=5, hybrid_window_size=100_000 if size == "medium" else 1_000_000)
    for cache in [4, 64]:
        for workload in ["uniform", "normal"]:
            add("reverse-map-comparison", f"{workload}-cache-{cache}",
                query_workload=workload, reverse_map_cache_size_mb=cache)
    return cases


def select_cases(cases, requested):
    tokens = [part for value in requested for part in value.split(",") if part]
    known = {"all", *FAMILIES, *(case["id"] for case in cases)}
    unknown = set(tokens) - known
    if unknown:
        raise ValueError(f"Unknown tests: {', '.join(sorted(unknown))}; use --list")
    return [case for case in cases if "all" in tokens or case["family"] in tokens
            or case["id"] in tokens]


def timestamp():
    return datetime.now(timezone.utc).isoformat()


def write_summary(output, summary):
    (output / "run.json").write_text(json.dumps(summary, indent=2) + "\n")


def benchmark_command(config, artifact_dir):
    return [sys.executable, str(ROOT / "bench/bench_filters.py"),
            "-F", str(artifact_dir), "with",
            *(f"{key}={value}" for key, value in config.items())]


def validate_artifacts(directory, config):
    run = json.loads((directory / "run.json").read_text())
    if run["status"] != "COMPLETED":
        raise ValueError(f"Sacred status: {run['status']}")
    if config.get("collect_workload_stats"):
        stats = json.loads((directory / "workload_stats.json").read_text())
        if stats["total_queries"] != config["num_queries"]:
            raise ValueError("Workload statistics query count mismatch")
        if not (directory / "workload_frequencies.csv").is_file():
            raise ValueError("Missing workload frequency histogram")
    for name in config["filter_names"].split(","):
        read_metrics(directory, name, config)  # Also verifies complete round data.
        if config["capture_extra_stats"]:
            for suffix in ["latency.csv", "fp_stats.csv"]:
                if not (directory / f"{name}_{suffix}").is_file():
                    raise ValueError(f"Missing {name}_{suffix}")
        if config["collect_db_stats"]:
            suffixes = ["db_stats.json"]
            if name != "nonAdaptive":
                suffixes.append("rm_stats.json")
            for suffix in suffixes:
                if not (directory / f"{name}_{suffix}").is_file():
                    raise ValueError(f"Missing {name}_{suffix}")
    if config["collect_db_stats"]:
        for filename in ["db_stats.csv", "rm_stats.csv"]:
            if not (directory / filename).is_file():
                raise ValueError(f"Missing {filename}")


def run(args):
    cases = select_cases(recipes(args.size, args.seed), args.tests)
    output = args.output.resolve()
    if args.repetitions < 1:
        raise ValueError("repetitions must be at least 1")
    if args.seed < 0 or args.seed + args.repetitions - 1 >= 2**32:
        raise ValueError("Seeds must be between 0 and 4294967295 (Sacred/NumPy limit)")
    if not cases:
        raise ValueError("Select at least one test")
    if args.dry_run:
        for case in cases:
            for rep in range(args.repetitions):
                config = {**case["config"], "seed": args.seed + rep}
                print(shlex.join(benchmark_command(config, output / case["id"] / f"rep-{rep + 1}")))
        return 0
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"Output directory is populated: {output}")
    scratch = ROOT / "sponge"
    if output == scratch or output in scratch.parents:
        raise ValueError("Output must not be sponge itself or an ancestor of sponge")
    output.mkdir(parents=True, exist_ok=True)
    try:
        revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True, stderr=subprocess.DEVNULL).strip()
        dirty = bool(subprocess.check_output(
            ["git", "status", "--porcelain"], cwd=ROOT, text=True, stderr=subprocess.DEVNULL))
    except (OSError, subprocess.CalledProcessError):
        # A copied source tree can run without the worktree's local .git pointer.
        revision, dirty = "unknown", None
    summary = dict(size=args.size, seed=args.seed, repetitions=args.repetitions,
                   source_revision=revision, source_dirty=dirty, host=platform.node(),
                   platform=platform.platform(), started=timestamp(), status="RUNNING",
                   selected_cases=[case["id"] for case in cases], cases=[])
    write_summary(output, summary)
    interrupted = False
    try:
        for case in cases:
            for rep in range(args.repetitions):
                directory = output / case["id"] / f"rep-{rep + 1}"
                directory.mkdir(parents=True)
                config = {**case["config"], "seed": args.seed + rep}
                command = benchmark_command(config, directory)
                record = dict(id=case["id"], family=case["family"], repetition=rep + 1,
                              config=config, command=command, started=timestamp(), status="RUNNING")
                summary["cases"].append(record)
                write_summary(output, summary)
                print(f"[{len(summary['cases'])}/{len(cases) * args.repetitions}] {case['id']} "
                      f"({args.size}, seed={config['seed']})", flush=True)
                try:
                    with (directory / "execution.log").open("w") as log:
                        subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
                    # Sacred copies registered artifacts out of shared sponge immediately.
                    artifact_dir = directory / "1"
                    record["artifacts"] = str(artifact_dir.relative_to(output))
                    validate_artifacts(artifact_dir, config)
                    record["status"] = "COMPLETED"
                except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
                    record["status"] = "FAILED"
                    record["error"] = str(error)
                    print(f"  FAILED: {error}; see {directory / 'execution.log'}", flush=True)
                finally:
                    record["finished"] = timestamp()
                    write_summary(output, summary)
    except KeyboardInterrupt:
        interrupted = True
        if summary["cases"] and summary["cases"][-1]["status"] == "RUNNING":
            summary["cases"][-1]["status"] = "INTERRUPTED"
    summary["finished"] = timestamp()
    summary["status"] = "COMPLETED" if not interrupted and all(
        case["status"] == "COMPLETED" for case in summary["cases"]) else "INCOMPLETE"
    write_summary(output, summary)
    generate_report(output, compile_pdf=not args.no_pdf)
    print(f"{summary['status']}: {output / 'results.tex'}", flush=True)
    return 0 if summary["status"] == "COMPLETED" else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="Describe all families and cases")
    sub = parser.add_subparsers(dest="command")
    runner = sub.add_parser("run", help="Run selected tests sequentially")
    runner.add_argument("--tests", nargs="+", default=["all"])
    runner.add_argument("--size", choices=PRESETS, default="small")
    runner.add_argument("--seed", type=int, default=741262640)
    runner.add_argument("--repetitions", type=int, default=1)
    runner.add_argument("--output", type=Path, required=True)
    runner.add_argument("--dry-run", action="store_true")
    runner.add_argument("--no-pdf", action="store_true")
    report = sub.add_parser("report", help="Regenerate LaTeX from an existing run")
    report.add_argument("output", type=Path)
    report.add_argument("--no-pdf", action="store_true")
    args = parser.parse_args()
    try:
        if args.list:
            for family, description in FAMILIES.items():
                print(f"{family}: {description}")
                for case in recipes("small", 741262640):
                    if case["family"] == family:
                        print(f"  {case['id']}")
            return 0
        if args.command == "run":
            return run(args)
        if args.command == "report":
            generate_report(args.output.resolve(), compile_pdf=not args.no_pdf)
            return 0
        parser.error("Choose run, report, or --list")
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
