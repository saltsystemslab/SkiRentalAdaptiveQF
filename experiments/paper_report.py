"""Paper-organized comparisons, built from the same measurements as the appendix."""

import statistics

from report import FILTERS, LABELS, STYLES, mean_sd, number, tex

DISTRIBUTIONS = [("uniform", "Uniform"), ("normal", "Normal"),
                 ("lognormal", "Lognormal"), ("zipf", "Zipfian")]


def observations(groups, case_id, name, metric):
    return [item["metrics"][metric] for item in
            groups.get(case_id, {}).get("filters", {}).get(name, [])
            if metric in item["metrics"]]


def scalar(groups, case_id, metric, name, scale=1):
    return [value * scale for value in observations(groups, case_id, name, metric)]


def round_series(group, name, metric):
    items = group.get("filters", {}).get(name, [])
    if not items:
        return []
    config = group["config"]
    points = []
    for index in range(config["num_rounds"]):
        boundary = config["num_queries"] * (index + 1) // config["num_rounds"]
        previous = config["num_queries"] * index // config["num_rounds"]
        values = [item["rounds"][index]["round_fp"] / (boundary - previous)
                  if metric == "interval_fpr" else item["rounds"][index][metric]
                  for item in items]
        points.append((boundary / 1e6, [statistics.mean(values)]))
    return points


def panel(title, ylabel, curves, *, bars=False, labels=None, xlabel=None,
          options="", phase_config=None, lower_zero=True):
    return dict(title=title, ylabel=ylabel, curves=curves, bars=bars,
                labels=labels, xlabel=xlabel, options=options,
                phase_config=phase_config, lower_zero=lower_zero)


def shared_legend(names, bars):
    parts = [r"\begin{tikzpicture}",
             r"\begin{axis}[hide axis,xmin=0,xmax=1,ymin=0,ymax=1,",
             r"scale only axis,width=1cm,height=0.4cm,",
             r"legend columns=5,legend cell align=left,",
             r"legend style={draw=none,font=\small,at={(0.5,0.5)},anchor=center}]"]
    for name in names:
        style = STYLES[name] + ("Bar" if bars else "")
        parts += ["\\addlegendimage{" + ("ybar legend,/tikz/ybar," if bars else "") + style + "}",
                  "\\addlegendentry{" + LABELS[name] + "}"]
    return "\n".join(parts + [r"\end{axis}\end{tikzpicture}"])


def grid(panels, columns):
    """A shared legend and paper-like small multiples, with explicit missing data."""
    names = [name for name in FILTERS if any(p["curves"].get(name) for p in panels)]
    gap = 1.3 if any(p["ylabel"] != panels[index - index % columns]["ylabel"]
                     for index, p in enumerate(panels)) else 0.7
    if any("precision=3" in p["options"] for p in panels):
        gap = max(gap, 1.1)
    parts = [r"\begin{center}", shared_legend(names, panels[0]["bars"]), r"\par\medskip",
             r"\begingroup",
             f"\\pgfmathsetlengthmacro{{\\skiqfpanelwidth}}{{(\\linewidth-1.6cm-{(columns - 1) * gap}cm)/{columns}}}",
             r"\begin{tikzpicture}\begin{groupplot}[skiqf axis,",
             f"group style={{group size={columns} by {(len(panels) + columns - 1) // columns},"
             f"horizontal sep={gap}cm,vertical sep=1.5cm}},",
             r"scale only axis,width=\skiqfpanelwidth,height=3cm,scaled y ticks=true]"]
    for index, p in enumerate(panels):
        options = ["title={" + tex(p["title"]) + "}"]
        if index % columns == 0 or p["ylabel"] != panels[index - index % columns]["ylabel"]:
            options.append("ylabel={" + p["ylabel"] + "}")
        if p["lower_zero"]:
            options.append("ymin=0")
        if p["xlabel"]:
            options.append("xlabel={" + p["xlabel"] + "}")
        if p["bars"]:
            options += ["ybar=0,bar width=5pt,enlarge x limits=false",
                        f"xmin=-0.5,xmax={len(p['labels']) - 0.5}",
                        "xtick={" + ",".join(str(i) for i in range(len(p["labels"]))) + "}",
                        "xticklabels={" + ("" if len(p["labels"]) == 1 and p["labels"][0] == p["title"] else
                                            ",".join(tex(label) for label in p["labels"])) + "}",
                        r"x tick label style={rotate=45,anchor=east,font=\scriptsize}"]
        if p["options"]:
            options.append(p["options"])
        if p["phase_config"]:
            # Paint behind curves using relative y coordinates, independent of units/limits.
            config = p["phase_config"]
            count = config["num_phases"]
            shades = []
            for phase in range(count):
                adversarial = (phase % 2 == 0) == config["start_with_adversarial_phase"]
                color = "red!15" if adversarial else "blue!15"
                shades.append(f"\\fill[{color}] (rel axis cs:{phase/count:.8g},0) "
                              f"rectangle (rel axis cs:{(phase+1)/count:.8g},1);")
            options.append("execute at begin axis={" + "".join(shades) + "}")
        parts.append("\\nextgroupplot[" + ",\n".join(options) + "]")
        for name in names:
            points = p["curves"].get(name, [])
            # Keep each filter's grouped-bar slot even when its measurements are absent.
            style = STYLES[name] + ("Bar" if p["bars"] else "")
            repeated = any(len(values) > 1 for _, values in points)
            opts = style + ",mark size=1.8pt,mark repeat=" + ("1" if len(points) < 20 else "11")
            if repeated:
                opts += ",error bars/.cd,y dir=both,y explicit"
            coords = []
            for x, values in points:
                mean, sd = mean_sd(values)
                coords.append(f"({x:.10g},{mean:.10g})" +
                              (f" +- (0,{sd:.10g})" if repeated else ""))
            parts.append("\\addplot[" + opts + "] coordinates {" + " ".join(coords) + "};")
        missing = []
        if p["bars"]:
            for index, label in enumerate(p["labels"]):
                if not any(any(x == index for x, _ in p["curves"].get(name, [])) for name in names):
                    missing.append(label)
        if not any(p["curves"].values()):
            parts.append(r"\node[font=\scriptsize,align=center] at (rel axis cs:0.5,0.5) {No complete data};")
        elif missing:
            parts.append(r"\node[font=\tiny,anchor=north,align=center] at (rel axis cs:0.5,1) {Unavailable: " +
                         tex(", ".join(missing)) + "};")
    return "\n".join(parts + [r"\end{groupplot}\end{tikzpicture}\endgroup\end{center}"])


def category_panel(groups, title, metric, ylabel, cases, *, scale=1):
    curves = {}
    for name in FILTERS:
        curves[name] = [(index, values) for index, (case_id, _) in enumerate(cases)
                        if (values := scalar(groups, case_id, metric, name, scale))]
    return panel(title, ylabel, curves, bars=True, labels=[label for _, label in cases])


def trajectory_panel(groups, case_id, title, metric, ylabel, *, phases=False):
    group = groups.get(case_id, {})
    options = []
    if metric == "load_factor":
        options.append(r"yticklabel={\pgfmathprintnumber[fixed zerofill,precision=3]{\tick}}")
    if group.get("config"):
        maximum = group["config"]["num_queries"] / 1e6
        # Relative phase rectangles must map exactly to executed-query boundaries.
        options.append(f"xmin=0,xmax={maximum:.10g},enlarge x limits=false")
    return panel(title, ylabel, {name: round_series(group, name, metric) for name in FILTERS},
                 xlabel="Queries (millions)", phase_config=group.get("config") if phases else None,
                 options=",".join(options),
                 lower_zero=metric != "load_factor")


def comparison_table(groups, cases):
    metrics = [("system_insert", "System insert (Keys/s)", 1e6),
               ("throughput", "Query throughput (MOps/sec)", 1),
               ("db_miss_avg_ns", "DB-miss latency (ns)", 1),
               ("adapt_avg_ns", "Adapt-call latency (ns)", 1)]
    parts = [r"\begin{center}\small\begin{tabular}{lrr}\toprule",
             r"Metric & B-Tree (WiredTiger) & $B^\epsilon$-Tree (SplinterDB) \\", r"\midrule"]
    for metric, label, scale in metrics:
        parts.append(label + " & " + " & ".join(number(scalar(groups, case_id, metric, "adaptive", scale))
                                                 for case_id, _ in cases) + r" \\")
        if metric == "system_insert":
            relative = []
            for case_id, _ in cases:
                records = groups.get(case_id, {}).get("filters", {})
                baseline = {item["repetition"]: item["metrics"].get(metric)
                            for item in records.get("nonAdaptive", [])}
                values = [100 * item["metrics"][metric] / baseline[item["repetition"]]
                          for item in records.get("adaptive", [])
                          if item["metrics"].get(metric) is not None and baseline.get(item["repetition"])]
                relative.append(number(values))
            parts.append(r"Relative to no reverse map (\%) & " + " & ".join(relative) + r" \\")
    return "\n".join(parts + [r"\bottomrule\end{tabular}\end{center}"])


def build_paper_sections(groups, paper):
    """Write standalone grouped snippets; return the report body and snippet manifest."""
    body, manifest = [], []

    def section(title, families, intro):
        if not any(case.split("/")[0] in families and group["filters"] for case, group in groups.items()):
            return False
        body.extend([r"\clearpage", "\\section{" + title + "}", intro])
        return True

    def figure(stem, panels, columns, caption):
        filename = stem + ".tex"
        (paper / filename).write_text(grid(panels, columns) + "\n")
        body.extend(["\\input{paper/" + filename + "}",
                     r"\begin{center}\small " + caption + r"\end{center}"])
        manifest.append(filename)

    if section("Experimental setup: reverse-map overhead", {"engine-comparison"},
               "Insertion and query comparisons follow the paper's reverse-map table. "
               "Each engine uses its own nonadaptive baseline for relative insertion throughput."):
        cases = [("engine-comparison/wiredTiger", "WiredTiger"),
                 ("engine-comparison/splinterDB", "SplinterDB")]
        filename = "reverse-map-overhead.tex"
        (paper / filename).write_text(comparison_table(groups, cases) + "\n")
        body.extend(["\\input{paper/" + filename + "}",
                     "Latency artifacts measure the full DB-miss and adaptation calls. "
                     "They do not isolate reverse-map lookup latency, so the paper's measured "
                     r"$C_{RM}/C_{DB}$ ratio is unavailable here."])
        manifest.append(filename)
    if section("Singleton mixtures", {"singleton-mixture"},
               "The horizontal axis is the fraction of distinct keys appearing once; all other keys appear twice."):
        panels = []
        for metric, label in [("throughput", "Throughput (MOps/s)"), ("fpr", "Observed FPR"),
                              ("adaptations", "Adaptations"), ("total_read_mib", "Physical reads (MiB)")]:
            curves = {name: [(f/100, values) for f in [0,25,50,75,100]
                      if (values := scalar(groups, f"singleton-mixture/f-{f:03d}", metric, name))]
                      for name in FILTERS}
            panels.append(panel(label, label, curves, xlabel="Singleton fraction"))
        figure("singleton-mixture", panels, 2, "Single-run results; no error bars.")
    distributions = [("distributions/" + name, label) for name, label in DISTRIBUTIONS]
    if section("Robustness across workload distributions", {"distributions"},
               "Distribution panels and aggregate comparisons follow the paper's robustness study."):
        figure("distribution-instantaneous-throughput", [trajectory_panel(groups, case, label, "round_thput",
               r"Throughput\\(MOps/sec)") for case, label in distributions], 4,
               "Instantaneous database query throughput. Higher is better.")
        figure("distribution-overall-throughput", [category_panel(groups, "Overall query throughput", "throughput",
               r"Throughput\\(MOps/sec)", distributions)], 1, "Final cumulative throughput; higher is better.")
        body.append(r"\clearpage")
        figure("distribution-overall-io", [category_panel(groups, "Overall disk I/O", "total_read_mib",
               r"Bytes read\\(GiB)", distributions, scale=1/1024)], 1,
               "Database plus reverse-map physical reads. Lower is better.")
        for stem, title, pairs in [
            ("database-io", "Database I/O and false positives", [("db_read_mib", r"Bytes read\\(GiB)", 1/1024),
                                                               ("fpr", r"False positives\\(millions)", None)]),
            ("reverse-map-io", "Reverse-map I/O and adaptations", [("rm_read_mib", r"Bytes read\\(GiB)", 1/1024),
                                                                 ("adaptations", r"Adaptations\\(millions)", 1e-6)])]:
            body.append(r"\clearpage")
            panels = []
            for metric, ylabel, scale in pairs:
                for case, label in distributions:
                    # FPR * query count gives exact cumulative FP count, then millions.
                    resolved_scale = scale if scale is not None else groups.get(case, {}).get("config", {}).get("num_queries", 0)/1e6
                    panels.append(category_panel(groups, label, metric, ylabel, [(case, label)], scale=resolved_scale))
            figure("distribution-" + stem, panels, 4, title + ". Lower is better.")
    if section("Adversarial workload", {"adversarial"},
               "Cache-size panels compare adversarial replay frequencies as in the paper. "
               "Titles use actual cache budgets in this run rather than the paper's dataset percentages."):
        panels = []
        for cache in [64, 128, 256]:
            curves = {name: [(freq, values) for freq in [1, 5, 10]
                            if (values := scalar(groups, f"adversarial/frequency-{freq}-cache-{cache}",
                                                 "throughput", name))] for name in FILTERS}
            panels.append(panel(f"Database cache: {cache} MB", r"Throughput\\(MOps/sec)", curves,
                                xlabel=r"Replay opportunity\\frequency (\%)", options="xtick={1,5,10}"))
        figure("adversarial-throughput", panels, 3, "Higher is better. Realized replay counts appear in the appendix.")
    if section("Shifting workload distributions", {"phased"},
               "Phase boundaries come from each recipe. Blue denotes uniform queries; red denotes adversarial queries."):
        figure("phased-throughput", [trajectory_panel(groups, "phased/" + name, title, "round_thput",
               r"Throughput\\(MOps/sec)", phases=True) for name, title in
               [("uniform-first", "Uniform first"), ("adversarial-first", "Adversarial first")]], 2,
               "Instantaneous database query throughput under workload drift. Higher is better.")
    if section("Varying external-memory costs", {"cost-ratios"},
               r"The paper's $\alpha$ sweep combines half/twice/quadruple recipes with the "
               r"default distribution run at $\alpha=1$. Ratios are nominal delay settings, not measured latencies."):
        panels = []
        for dist, title in DISTRIBUTIONS:
            cases = [(0.5, f"cost-ratios/{dist}-half"), (1, f"distributions/{dist}"),
                     (2, f"cost-ratios/{dist}-twice"), (4, f"cost-ratios/{dist}-quad")]
            curves = {name: [(x, values) for x, case in cases
                            if (values := scalar(groups, case, "throughput", name))] for name in FILTERS}
            panels.append(panel(title, r"Throughput\\(MOps/sec)", curves,
                                xlabel=r"Nominal $\alpha=C_{RM}/C_{DB}$", options=
                                r"xmode=log,log basis x=2,xtick={0.5,1,2,4},xticklabels={$2^{-1}$,$2^0$,$2^1$,$2^2$}"))
        figure("cost-ratio-throughput", panels, 4, "Higher is better. Missing recipes produce no data points.")
    micro = [("microbenchmarks/" + name, label) for name, label in DISTRIBUTIONS]
    if section("Microbenchmarks", {"microbenchmarks"}, "These measurements use mock storage, as in the paper's main-memory experiments."):
        p = category_panel(groups, "Filter-only insertion and query throughput", "filter_query",
                           r"Throughput\\(MOps/sec)", [(micro[0][0], "Insert"), *micro])
        for name in FILTERS:
            p["curves"][name] = [(x, values) for x, values in p["curves"][name] if x != 0]
            inserted = scalar(groups, micro[0][0], "filter_insert", name)
            if inserted:
                p["curves"][name].insert(0, (0, inserted))
        figure("microbenchmark-throughput", [p], 1,
               "Insert uses the uniform recipe's initial insertion; remaining bars show filter-only workload queries.")
        body.append(r"\clearpage")
        figure("microbenchmark-load-factor", [trajectory_panel(groups, case, label, "load_factor", "Load factor")
               for case, label in micro], 4, "Load factor throughout the main-memory workloads.")
        figure("microbenchmark-fpr", [trajectory_panel(groups, case, label, "interval_fpr", "False positive rate")
               for case, label in micro], 4, "Interval false positive rates. Lower is better.")
    if section("Adaptive adversary", {"adaptive-adversary"},
               "This new comparison extends the paper's adversarial study. "
               "Each filter receives its own feedback-dependent stream; each case identifies its feedback model."):
        cases = [(case, (f"Window: {group['config']['adversary_window_size']:,} queries"
                         if "adversary_window_size" in group["config"] else "Immediate feedback"))
                 for case, group in groups.items()
                 if case.split("/")[0] == "adaptive-adversary" and group["filters"]]
        for case, label in cases:
            window = groups[case]["config"].get("adversary_window_size")
            explanation = (f"Interval measurements against the adaptive adversary with an observation window of {window:,} queries."
                           if window is not None else
                           "Perfect recognition of ordinary query outcomes: corrections immediately disable replay; "
                           "tolerated false positives enable it. There are no probes or adversary windows.")
            hybrid_window = groups[case]["config"].get("hybrid_window_size", 1_000_000)
            explanation += (f" HybridSkiQF's detector window is {hybrid_window:,} negative-key queries; "
                            "smoothing remains 0.7 and the repeated-FP threshold remains 6.")
            suffix = "" if len(cases) == 1 else "-" + case.split("/")[1]
            figure("adaptive-adversary-trajectories" + suffix,
                   [trajectory_panel(groups, case, title, metric, ylabel)
                    for title, metric, ylabel in [("Query throughput", "round_thput", r"Throughput\\(MOps/sec)"),
                                                  ("False positives", "interval_fpr", "False positive rate")]], 2,
                   explanation)
            if window is None:
                figure("adaptive-adversary-regimes" + suffix,
                       [trajectory_panel(groups, case, "Replay flag", "repeat_query_fraction",
                                         "Fraction of queries\\\\with replay enabled")], 1,
                       "The flag changes immediately; the plot averages its value over each reporting interval.")
                curves = {name: [(i, values) for i, prefix in enumerate(["uniform", "repeat"])
                                 if (values := scalar(groups, case, prefix + "_adaptations", name))]
                          for name in FILTERS}
                figure("adaptive-adversary-corrections" + suffix,
                       [panel("Corrections by traffic regime", "Corrections", curves, bars=True,
                              labels=["Replay disabled", "Replay enabled"])], 1,
                       "Regimes refer to the flag before the query; a correction disables replay for the next query.")
        figure("adaptive-adversary-overall", [category_panel(groups, title, metric, ylabel, cases)
               for title, metric, ylabel in [("Overall throughput", "throughput", r"Throughput\\(MOps/sec)"),
                                             ("Overall FPR", "fpr", "False positive rate")]], 2,
               "Final cumulative measurements. Throughput: higher is better; FPR: lower is better.")
    if section("Reverse-map cache budget", {"reverse-map-comparison"},
               "These grouped comparisons extend the paper's database/reverse-map I/O study."):
        panels = []
        for metric, ylabel in [("throughput", r"Throughput\\(MOps/sec)"), ("rm_read_mib", r"Reverse-map reads\\(GiB)")]:
            for dist, title in DISTRIBUTIONS[:2]:
                cases = [(f"reverse-map-comparison/{dist}-cache-{cache}", f"{cache} MB") for cache in [4, 64]]
                panels.append(category_panel(groups, title, metric, ylabel, cases, scale=1/1024 if metric == "rm_read_mib" else 1))
        figure("reverse-map-cache-comparison", panels, 2,
               "Reverse-map cache budgets on the horizontal axis; database cache remains 64 MB. "
               "Throughput: higher is better; reads: lower is better.")
    return body, manifest
