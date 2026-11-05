"""Read existing benchmark artifacts and emit portable PGFPlots/LaTeX."""

import csv
from bisect import bisect_right
import json
import math
from pathlib import Path
import re
import shutil
import statistics
import subprocess

FILTERS = ["nonAdaptive", "adaptive", "skiQF", "randomizedSkiQF", "hybridSkiQF"]
LABELS = dict(zip(FILTERS, ["Non-adaptive", "AdaptiveQF", "SkiQF", "RandomizedSkiQF", "HybridSkiQF"]))
STYLES = dict(zip(FILTERS, ["skiqfNonAdaptive", "skiqfAdaptive", "skiqfSki", "skiqfRandomized", "skiqfHybrid"]))
METRICS = {
    "singleton_key_fraction": "Singleton fraction of distinct workload keys",
    "singleton_operation_fraction": "Singleton fraction of workload operations",
    "initial_fp_singleton_fraction": "Singleton fraction of initial false-positive keys",
    "throughput": "Workload throughput (MOps/s)",
    "fpr": "Observed FPR",
    "adaptations": "Adaptations",
    "adversarial_queries": "Adversarial queries",
    "regime_switches": "Adversary flag changes",
    "repeat_query_fraction": "Fraction of queries with replay enabled",
    "uniform_false_positives": "False positives with replay disabled",
    "repeat_false_positives": "False positives with replay enabled",
    "uniform_adaptations": "Corrections with replay disabled",
    "repeat_adaptations": "Corrections with replay enabled",
    "db_read_mib": "Database reads (MiB)",
    "rm_read_mib": "Reverse-map reads (MiB)",
    "db_read_ios": "Database read I/Os",
    "rm_read_ios": "Reverse-map read I/Os",
    "total_read_mib": "Total reads (MiB)",
    "total_read_ios": "Total read I/Os",
    "filter_insert": "Filter insertion (MOps/s)",
    "system_insert": "System insertion (MOps/s)",
    "true_query": "Filter-only true queries (MOps/s)",
    "filter_query": "Filter-only workload queries (MOps/s)",
    "size_mib": "Reported QF storage (MiB)",
    "load_factor": "Final load factor",
    "db_miss_avg_ns": "DB-miss mean latency (ns)",
    "adapt_avg_ns": "Adapt-call mean latency (ns)",
}


def tex(value):
    replacements = {"\\": r"\textbackslash{}", "_": r"\_", "%": r"\%",
                    "&": r"\&", "#": r"\#", "$": r"\$", "{": r"\{",
                    "}": r"\}", "~": r"\textasciitilde{}", "^": r"\textasciicircum{}"}
    return "".join(replacements.get(char, char) for char in str(value))


def read_rounds(path):
    lines = path.read_text().splitlines()
    if len(lines) < 2:
        raise ValueError(f"No round measurements in {path}")
    fields = lines[0].split()
    rows = []
    for line in lines[1:]:
        values = line.split()
        if len(values) != len(fields):
            raise ValueError(f"Malformed round row in {path}")
        rows.append(dict(zip(fields, map(float, values))))
    return rows


def read_adversary(path, config):
    """Audit perfect-outcome feedback and recover executed-query regimes."""
    count, parts = config["num_queries"], config["num_rounds"]
    boundaries = [count * (i + 1) // parts for i in range(parts)]
    enabled = [0] * parts
    previous, repeat = 0, 0
    metrics = dict(regime_switches=0, uniform_false_positives=0,
                   repeat_false_positives=0, uniform_adaptations=0, repeat_adaptations=0)
    replays = 0

    def segment(start, end, active):
        while active and start < end:
            index = bisect_right(boundaries, start)
            stop = min(end, boundaries[index])
            enabled[index] += stop - start
            start = stop

    with path.open() as source:
        for row in csv.DictReader(source):
            index = int(row["query_index"])
            before, after = int(row["repeat_before"]), int(row["repeat_after"])
            outcome, origin = row["outcome"], row["source"]
            if (not previous < index <= count or before != repeat or after not in (0, 1)
                    or origin not in ("fresh", "replay") or int(row["active_candidates"]) < 0):
                raise ValueError(f"Invalid adversary event in {path}")
            segment(previous, index, before)
            prefix = "repeat" if before else "uniform"
            if outcome in ("adapted", "tolerated"):
                if after != (outcome == "tolerated"):
                    raise ValueError(f"Feedback contradicts replay flag in {path}")
                metrics[f"{prefix}_false_positives"] += 1
                metrics[f"{prefix}_adaptations"] += outcome == "adapted"
            elif outcome not in ("negative", "present") or origin != "replay" or after != before:
                raise ValueError(f"Invalid replay retirement in {path}")
            if origin == "replay" and (not before or (index - 1) % (100 // config["adv_freq"])):
                raise ValueError(f"Replay outside its ordinary query slot in {path}")
            metrics["regime_switches"] += before != after
            replays += origin == "replay"
            previous, repeat = index, after
    segment(previous, count, repeat)
    metrics["repeat_query_fraction"] = sum(enabled) / count
    fractions = [value / (boundaries[i] - (boundaries[i - 1] if i else 0))
                 for i, value in enumerate(enabled)]
    return metrics, fractions, replays


def read_metrics(directory, name, config):
    rounds = read_rounds(directory / f"{name}.csv")
    if len(rounds) != config["num_rounds"] or [row["round"] for row in rounds] != list(range(config["num_rounds"])):
        raise ValueError(f"Incomplete rounds for {name} in {directory}")
    text = (directory / f"{name}_summary.csv").read_text()

    def summary(label):
        match = re.search(r"^" + re.escape(label) + r":\s*([-+\deE.]+)", text, re.MULTILINE)
        if not match:
            raise ValueError(f"Missing summary metric {label} in {directory}")
        return float(match.group(1))

    if summary("Queries") != config["num_queries"]:
        raise ValueError(f"Wrong query count for {name} in {directory}")
    final = rounds[-1]
    metrics = dict(throughput=final["cumulative_thput"],
                   fpr=final["cumulative_fp"] / config["num_queries"],
                   adaptations=final["cumulative_adapts"],
                   adversarial_queries=final["cumulative_adversarial_queries"],
                   filter_insert=summary("FilterInsert Thput"),
                   system_insert=summary("SystemInsert Thput"),
                   true_query=summary("True Queries Thput"),
                   filter_query=summary("Queries Thput"),
                   size_mib=summary("Size(B)") / 1024**2,
                   load_factor=final["load_factor"])
    if config.get("microbench"):
        # Mock storage gives these no physical/system interpretation.
        metrics.pop("system_insert")
        metrics.pop("throughput")
    if config.get("query_workload") == "adaptive-adversarial" and "adversary_window_size" not in config:
        feedback, fractions, replays = read_adversary(directory / f"{name}_adversary.csv", config)
        if (feedback["uniform_false_positives"] + feedback["repeat_false_positives"] != final["cumulative_fp"]
                or feedback["uniform_adaptations"] + feedback["repeat_adaptations"] != final["cumulative_adapts"]
                or replays != final["cumulative_adversarial_queries"]):
            raise ValueError(f"Adversary trace totals disagree with rounds for {name}")
        metrics.update(feedback)
        for row, fraction in zip(rounds, fractions):
            row["repeat_query_fraction"] = fraction
    for filename, prefix in [("db_stats.csv", "db"), ("rm_stats.csv", "rm")]:
        path = directory / filename
        if path.is_file():
            with path.open() as source:
                for row in csv.DictReader(source):
                    if row["name"] == name:
                        metrics[f"{prefix}_read_mib"] = float(row["block_manager_bytes_read"]) / 1024**2
                        metrics[f"{prefix}_read_ios"] = float(row["connection_read_ios"])
        if name == "nonAdaptive" and prefix == "rm" and config["collect_db_stats"]:
            metrics["rm_read_mib"] = 0.0
            metrics["rm_read_ios"] = 0.0
    if "db_read_mib" in metrics and "rm_read_mib" in metrics:
        metrics["total_read_mib"] = metrics["db_read_mib"] + metrics["rm_read_mib"]
        metrics["total_read_ios"] = metrics["db_read_ios"] + metrics["rm_read_ios"]
    latency = directory / f"{name}_latency.csv"
    if latency.is_file():
        with latency.open() as source:
            for row in csv.DictReader(source):
                if int(row["numSamples"]) > 0:
                    key = "db_miss_avg_ns" if row["metric"] == "DbMiss" else "adapt_avg_ns"
                    metrics[key] = float(row["avg"])
    workload_path = directory / "workload_stats.json"
    if workload_path.exists():
        workload = json.loads(workload_path.read_text())
        for key in ["singleton_key_fraction", "singleton_operation_fraction"]:
            metrics[key] = workload[key]
        baseline = directory / "nonAdaptive_fp_stats.csv"
        if baseline.exists() and config.get("query_workload") != "adaptive-adversarial":
            with baseline.open() as f:
                rows = [line.split() for line in f.read().splitlines()[1:] if line.strip()]
            total = sum(int(row[1]) for row in rows)
            if total:
                metrics["initial_fp_singleton_fraction"] = sum(int(c) for n,c in rows if int(n)==1)/total
    if not all(math.isfinite(value) and value >= 0 for value in metrics.values()):
        raise ValueError(f"Non-finite or negative metrics for {name} in {directory}")
    return metrics, rounds


def write_csv(path, rows, fields):
    with path.open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def mean_sd(values):
    return statistics.mean(values), statistics.stdev(values) if len(values) > 1 else 0.0


def number(values):
    if not values:
        return "--"
    mean, sd = mean_sd(values)
    result = f"{mean:.5g}"
    return f"{result} $\\pm$ {sd:.3g}" if len(values) > 1 else result


def bar_plot(metric, grouped):
    names = [name for name in FILTERS if any(metric in item["metrics"] for item in grouped.get(name, []))]
    if not names:
        return ""
    parts = [r"\begin{minipage}{0.32\linewidth}\centering", r"\begin{tikzpicture}",
             r"\begin{axis}[skiqf axis,ybar,bar width=9pt,width=\linewidth,height=4.3cm,",
             "ymin=0,enlarge x limits=0.2,xtick={" + ",".join(map(str, range(len(names)))) + "},",
             "xticklabels={" + ",".join(LABELS[name] for name in names) + "},",
             r"x tick label style={rotate=45,anchor=east,font=\tiny},",
             "title={" + tex(METRICS[metric]) + r"},title style={font=\scriptsize},",
             r"y tick label style={font=\tiny},grid=major]" ]
    for index, name in enumerate(names):
        values = [item["metrics"][metric] for item in grouped[name] if metric in item["metrics"]]
        mean, sd = mean_sd(values)
        options = STYLES[name] + "Bar,bar shift=0pt"
        if len(values) > 1:
            parts.append(f"\\addplot+[{options},error bars/.cd,y dir=both,y explicit] "
                         f"coordinates {{({index},{mean:.10g}) +- (0,{sd:.10g})}};")
        else:
            parts.append(f"\\addplot+[{options}] coordinates {{({index},{mean:.10g})}};")
    parts += [r"\end{axis}\end{tikzpicture}\end{minipage}"]
    return "\n".join(parts)


def trajectories(grouped, config):
    plots = []
    for metric, label in [("round_thput", "Interval throughput (MOps/s)"), ("round_fpr", "Interval FPR")]:
        parts = [r"\begin{minipage}{0.49\linewidth}\centering\begin{tikzpicture}",
                 r"\begin{axis}[skiqf axis,width=\linewidth,height=5.2cm,xlabel={Executed queries (millions)},",
                 "ylabel={" + label + "},ymin=0,grid=major,",
                 r"tick label style={font=\tiny},label style={font=\scriptsize},",
                 r"legend style={font=\tiny,at={(0.5,-0.3)},anchor=north,legend columns=2}]" ]
        for name in FILTERS:
            items = grouped.get(name, [])
            if not items:
                continue
            coords = []
            for index in range(config["num_rounds"]):
                boundary = config["num_queries"] * (index + 1) // config["num_rounds"]
                previous = config["num_queries"] * index // config["num_rounds"]
                values = [item["rounds"][index]["round_thput"] if metric == "round_thput" else
                          item["rounds"][index]["round_fp"] / (boundary - previous) for item in items]
                coords.append(f"({boundary / 1e6:.8g},{statistics.mean(values):.10g})")
            parts.append("\\addplot[" + STYLES[name] + ",mark repeat=11] coordinates {" + " ".join(coords) + "};")
            parts.append("\\addlegendentry{" + LABELS[name] + "}")
        parts.append(r"\end{axis}\end{tikzpicture}\end{minipage}")
        plots.append("\n".join(parts))
    return "\n".join(plots)


def generate_report(output, compile_pdf=True):
    output = Path(output)
    run = json.loads((output / "run.json").read_text())
    paper = output / "paper"
    paper.mkdir(exist_ok=True)
    shutil.copyfile(Path(__file__).with_name("plot-styles.tex"), paper / "styles.tex")
    groups = {}
    normalized = []
    series = []
    issues = []
    for case in run["cases"]:
        group = groups.setdefault(case["id"], {"config": case["config"], "filters": {}, "records": []})
        group["records"].append(case)
        if case["status"] != "COMPLETED":
            continue
        directory = output / case["artifacts"]
        for name in case["config"]["filter_names"].split(","):
            try:
                metrics, rounds = read_metrics(directory, name, case["config"])
            except (OSError, ValueError, KeyError) as error:
                issues.append(f"{case['id']} / repetition {case['repetition']} / {name}: {error}")
                continue
            group["filters"].setdefault(name, []).append(dict(metrics=metrics, rounds=rounds,
                                                           repetition=case["repetition"]))
            normalized.append(dict(case=case["id"], repetition=case["repetition"],
                                   seed=case["config"]["seed"], filter=name, **metrics))
            for index, row in enumerate(rounds):
                boundary = case["config"]["num_queries"] * (index + 1) // case["config"]["num_rounds"]
                previous = case["config"]["num_queries"] * index // case["config"]["num_rounds"]
                series.append(dict(case=case["id"], repetition=case["repetition"], filter=name,
                                   executed_queries=boundary, interval_fpr=row["round_fp"] / (boundary - previous), **row))
    requested = run.get("selected_cases", list(groups))
    expected_count = len(requested) * run["repetitions"]
    write_csv(output / "metrics.csv", normalized, ["case", "repetition", "seed", "filter", *METRICS])
    round_fields = ["case", "repetition", "filter", "executed_queries", "interval_fpr",
                    "round", "round_thput", "round_fp", "round_tp", "cumulative_thput",
                    "cumulative_fp", "cumulative_adversarial_queries", "round_adapts",
                    "cumulative_adapts", "load_factor", "repeat_query_fraction"]
    write_csv(output / "rounds.csv", series, round_fields)
    doc = [r"\documentclass[11pt]{article}", r"\usepackage[margin=1.6cm]{geometry}",
           r"\input{paper/styles.tex}", r"\usepackage{hyperref}",
           r"\begin{document}\sloppy", r"\title{SkiQF: experimental results}\author{}",
           "\\date{" + tex(run["started"]) + "}", r"\maketitle",
           "Run status: \\textbf{" + tex(run["status"]) + "}. Size: " + tex(run["size"]) + ".\\\\",
           "Host: " + tex(run["host"]) + ". Source revision: \\texttt{" + tex(run["source_revision"][:12]) +
           "}; modified sources: " + tex(run["source_dirty"]) + ".\\\\",
           f"Base seed: {run['seed']}. Requested repetitions: {run['repetitions']}. "
           f"Completed cases: {sum(c['status'] == 'COMPLETED' for c in run['cases'])}/{expected_count}.",
           r"\paragraph{Reading the results.} Throughput uses the final cumulative workload measurement. "
           r"MOps/s equals operations per microsecond; MiB uses $2^{20}$ bytes. Filter-only query "
           r"measurements follow the main workload. Mock-storage microbenchmarks exclude database I/O. "
           r"Repeated scalar measurements show mean $\pm$ sample standard deviation; trajectories show means. "
           r"A dash means unavailable. Adaptive-adversary streams respond independently to each filter. "
           r"Reported size covers QF storage, excluding derived filter state and database caches. "
           r"Local small/medium runs validate the workflow and are not publication-scale results.",
           r"\tableofcontents"]
    not_started = [case_id for case_id in requested if case_id not in groups]
    if not_started:
        doc += [r"\section*{Selected cases not started}",
                *(tex(case_id) + r"\par" for case_id in not_started)]
    if issues:
        doc += [r"\section*{Artifact errors}", *(tex(issue) + r"\par" for issue in issues)]
    header = list(doc)
    from paper_report import build_paper_sections
    paper_body, grouped_snippets = build_paper_sections(groups, paper)
    doc.extend(paper_body)
    appendix = [r"\appendix", r"\clearpage", r"\section{Individual experiment results}",
                "Detailed configurations, metrics and plots for each case follow. "
                "These measurements are the source of the grouped comparisons in the main report."]
    snippets = []
    for case_id, group in groups.items():
        config, grouped = group["config"], group["filters"]
        stem = case_id.replace("/", "--")
        appendix += [r"\clearpage", "\\subsection{" + tex(case_id) + "}"]
        descriptions = {
            "singleton-mixture": "Controlled fraction of distinct keys appearing once; remaining keys appear twice.",
            "distributions": "Compare workload skew and its effect on throughput, false positives and I/O.",
            "cost-ratios": "Compare online adaptation under different external I/O cost ratios.",
            "microbenchmarks": "Measure filter operations using mock database and reverse-map storage.",
            "phased": "Measure adaptation as four uniform and adversarial phases alternate.",
            "engine-comparison": "Compare insertion cost with WiredTiger and SplinterDB reverse maps; queries are diagnostic.",
            "adversarial": "Measure fixed false-positive replay at different replay frequencies and database cache budgets.",
            "adaptive-adversary": ("Historical boundary-probe adversary." if "adversary_window_size" in config else
                                   "Perfect outcome knowledge: every tolerated false positive enables replay; "
                                   "every correction disables it immediately. No probes or adversary window."),
            "reverse-map-comparison": "Compare reverse-map cache budgets under uniform and normal queries.",
        }
        appendix.append(tex(descriptions[case_id.split("/")[0]]))
        config_keys = ["quotient_bits", "remainder_bits", "num_queries", "num_rounds",
                       "query_workload", "storage_engine", "reverse_map_engine", "storage_cache_size_mb",
                       "reverse_map_cache_size_mb", "break_even", "storage_sleep_us", "reverse_sleep_us",
                       "singleton_fraction", "zipf_constant", "adv_freq", "adversary_window_size", "hybrid_window_size", "num_phases",
                       "start_with_adversarial_phase"]
        appendix += [r"\paragraph{Configuration.}\small " + "; ".join(
            tex(f"{key}={config[key]}") for key in config_keys if key in config) +
            ". Seeds: " + ", ".join(str(record["config"]["seed"]) for record in group["records"]) + ".",
            r"\par Database cache for nonAdaptive includes the reverse-map budget.\normalsize"]
        for record in group["records"]:
            if record["status"] != "COMPLETED":
                appendix.append(r"\paragraph{Incomplete result.} " + tex(
                    f"Repetition {record['repetition']}: {record['status']}. {record.get('error', '')}") +
                    " See execution.log in the case directory.")
        if not grouped:
            appendix.append("No complete measurements available.")
            continue
        table = [r"\begin{center}\small", r"\resizebox{\linewidth}{!}{%",
                 r"\begin{tabular}{l" + "r" * len(grouped) + "}", r"\toprule",
                 "Metric & " + " & ".join(tex(name) for name in grouped) + r" \\", r"\midrule"]
        for metric, label in METRICS.items():
            if any(metric in item["metrics"] for items in grouped.values() for item in items):
                table.append(tex(label) + " & " + " & ".join(number(
                    [item["metrics"][metric] for item in items if metric in item["metrics"]])
                    for items in grouped.values()) + r" \\")
        table += [r"\bottomrule\end{tabular}}\end{center}"]
        table_name = f"{stem}-table.tex"
        (paper / table_name).write_text("\n".join(table) + "\n")
        appendix.append("\\input{paper/" + table_name + "}")
        plot_metrics = (["filter_insert", "true_query", "filter_query"] if config.get("microbench") else
                        ["system_insert", "filter_insert"] if config.get("is_insert_test") else
                        ["throughput", "fpr", "adaptations"])
        plots = "\n".join(bar_plot(metric, grouped) for metric in plot_metrics)
        if config.get("is_phased_test") or config["query_workload"] == "adaptive-adversarial":
            plots += "\n\\par\\bigskip\n" + trajectories(grouped, config)
        figure_name = f"{stem}-figure.tex"
        (paper / figure_name).write_text(plots + "\n")
        appendix.append("\\input{paper/" + figure_name + "}")
        snippets.extend([table_name, figure_name])
    (output / "appendix-content.tex").write_text("\n".join(appendix) + "\n")
    doc.extend([r"\input{appendix-content.tex}", r"\end{document}"])
    (output / "results.tex").write_text("\n".join(doc) + "\n")
    (output / "appendix.tex").write_text("\n".join(header + [r"\input{appendix-content.tex}",
                                                            r"\end{document}"]) + "\n")
    (paper / "README.md").write_text(
        "# Paper snippets\n\nLoad styles.tex in your preamble. The colors, bar fills and marker shapes "
        "follow Paper-code/SkiRentalForAdaptivity/camready/main.tex; orange/star adds RandomizedSkiQF. "
        "Non-adaptive is labeled without assuming the CQF implementation was enabled. "
        "Each figure/table embeds this run's values and has no absolute data paths. "
        "Use \\input{...} inside a figure or figure* (four-panel comparisons suit a two-column figure*). "
        "Widths adapt to \\linewidth. Add a caption in the destination paper. "
        "The complete report uses these same grouped comparisons, followed by individual cases in an appendix.\n\n"
        "Metrics are also available in ../metrics.csv and trajectories in ../rounds.csv. "
        "Inspect ../run.json for failed or omitted cases before using a comparison. "
        "Means and sample standard deviations use completed repetitions only. "
        "Absent measurements are not plotted as zero.\n\n## Grouped paper comparisons\n\n" +
        "\n".join(f"- {name}" for name in grouped_snippets) + "\n\n## Individual case appendix\n\n" +
        "\n".join(f"- {name}" for name in snippets) + "\n")
    if compile_pdf and shutil.which("latexmk"):
        with (output / "latex-build.log").open("w") as log:
            subprocess.run(["latexmk", "-pdf", "-interaction=nonstopmode", "-halt-on-error", "results.tex", "appendix.tex"],
                           cwd=output, stdout=log, stderr=subprocess.STDOUT, check=True)
    if issues:
        raise ValueError("Report contains artifact errors; see results.tex")
