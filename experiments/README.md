# TODS experiments

Run one test at a time. `sponge/` is shared scratch storage; Sacred copies each
registered artifact into the selected result bundle before the next test runs.

```bash
./setup.sh
.venv/bin/python experiments/run.py --list
.venv/bin/python experiments/run.py run --tests all --size small --output sponge/tods/local-small
.venv/bin/python experiments/run.py run --tests all --size medium --output sponge/tods/local-medium
.venv/bin/python experiments/run.py run --tests all --size large --output /data/tods/paper-run
```

Family names and individual case IDs can be mixed, separated by spaces or commas:

```bash
.venv/bin/python experiments/run.py run --tests distributions adaptive-adversary --size medium --output sponge/tods/new-comparison
.venv/bin/python experiments/run.py run --tests distributions/zipf --size small --output sponge/tods/zipf-check --dry-run
.venv/bin/python experiments/run.py run --tests adaptive-adversary --size medium --seed 12345 --repetitions 3 --output sponge/tods/adaptive-repeats
.venv/bin/python experiments/run.py report /data/tods/paper-run
```

## Experiment catalog

`all` selects 38 cases. Each case compares the five filters `adaptive`,
`nonAdaptive`, `skiQF`, `randomizedSkiQF`, and `hybridSkiQF`, except engine
comparison, which retains the historical `adaptive`/`nonAdaptive` insertion test.

| Family | Cases | Purpose |
|---|---|---|
| `distributions` | `uniform`, `normal`, `lognormal`, `zipf` | Throughput, FPR, adaptations and physical I/O under different skew levels |
| `cost-ratios` | Each distribution above with `-half`, `-twice`, `-quad` | Sensitivity to external I/O costs |
| `microbenchmarks` | `uniform`, `normal`, `lognormal`, `zipf` | Insertion and filter-only true/workload query throughput with mock storage |
| `phased` | `uniform-first`, `adversarial-first` | Response to four alternating uniform/adversarial phases |
| `engine-comparison` | `wiredTiger`, `splinterDB` | System insertion with different reverse-map engines; database remains WiredTiger |
| `adversarial` | `frequency-{1,5,10}-cache-{64,128,256}` | Fixed false-positive replays crossed with database cache budget |
| `adaptive-adversary` | `immediate-feedback` | Perfect outcome knowledge; corrections disable replay, tolerated false positives enable it immediately |
| `reverse-map-comparison` | `{uniform,normal}-cache-{4,64}` | Effect of reverse-map cache budget |

Individual IDs are `family/case`, for example `cost-ratios/normal-twice`.
Zipfian exponent is 0.99. Frequencies are the historical benchmark `adv_freq`
settings: 1, 5, and 10 correspond to replay opportunities every 100, 20, and 10
executed queries when candidates are available. Adaptive adversary uses 5 and
assumes perfect recognition of ordinary query outcomes through a timing side
channel. It starts with replay disabled, enables it after a tolerated false
positive, and disables it after a correction. Filter negatives and true positives
leave the flag unchanged. Corrected keys are retired immediately; candidates are
deduplicated and replayed FIFO. There are no additional probes or adversary
windows. The medium adaptive-adversary recipe uses a 100,000-negative-query
HybridSkiQF detector window to show switching within its 10-million-query budget.
Other recipes and sizes retain the million-query detector default. The Python
driver exposes `hybrid_window_size`, forwarded as `--hybridWindowSize` in C++.
Smoothing remains 0.7 and the repeated-FP threshold remains 6; the corrected-FP
threshold already scales with the detector window. Reports record the window
explicitly, so this medium diagnostic is distinguishable from the paper default.
Every executed query counts toward the budget; only fresh queries advance the
seeded workload cursor. Each filter has its own feedback-dependent stream.
Correction failures abort the case. This models perfect outcome knowledge,
not a classifier for noisy measured latency.

## Fixed size presets

| Size | Quotient bits | Behavioral queries | Microbenchmark queries |
|---|---:|---:|---:|
| `small` | 20 | 1,000,000 | 5,000,000 |
| `medium` | 24 | 10,000,000 | 50,000,000 |
| `large` | 27 | 100,000,000 | 500,000,000 |

All use 8 remainder bits, 100 reporting rounds, and the benchmark's 90% initial
occupancy. Engine insertion tests use one million queries and quotient bits
capped at 24. Reporting rounds are intervals within a test, not repetitions.
Default seed is 741262640; default repetitions is one. Additional repetitions
use seed + repetition index. The generator and randomized policy honor the seed;
timings and physical I/O naturally depend on the machine and cache state.
Seeded workload generation fixes the historical OpenSSL-generated unseeded keys,
so new runs are reproducible starting workloads, not exact replays of old data.

## Database configuration

Behavioral defaults: WiredTiger 11.3.1 for database and reverse map, 64 MB database
cache plus 4 MB reverse-map cache, break-even 2, no artificial I/O delay.
`nonAdaptive` receives the combined 68 MB database budget because it has no
reverse map. The benchmark explicitly uses mock reverse-map storage for this
baseline, correcting historical reverse-map construction inherited from the
adaptive base class. New insertion results therefore measure a true baseline.
The same equal-budget rule applies to recipe overrides.

| Recipe override | Configuration |
|---|---|
| Cost ratio half | Break-even 1; database sleep 130 us |
| Cost ratio twice | Break-even 3; reverse-map sleep 130 us |
| Cost ratio quadruple | Break-even 5; reverse-map sleep 390 us |
| Fixed adversarial | Database cache 64, 128 or 256 MB; reverse map 4 MB |
| Engine comparison | Database 64 MB; reverse map 64 MB; WiredTiger versus SplinterDB revision 05654abb; unsorted insertion |
| Reverse-map comparison | Database 64 MB; reverse map 4 or 64 MB |
| Microbenchmarks | Mock database and reverse map; cache flags retained at 64 MB each but no physical I/O |

Database statistics and extra latency/FP statistics are enabled for behavioral
tests. Engine insertion tests collect extra statistics without database stats.
Microbenchmarks collect neither. Latency artifact values are nanoseconds.

## Results

Each output directory is one run. A populated directory is rejected.

- `run.json`: selected/resolved recipes, source revision and dirty status, seeds,
  machine information, commands and case completion status.
  Copied source trees without Git metadata record an unknown revision and null
  dirty status; Git metadata is not required to execute experiments.
- `results.tex`, `results.pdf`: the complete report, organized like the paper's
  experimental section: reverse-map overhead, distribution throughput and I/O
  breakdowns, adversarial cache/frequency comparisons, shaded workload phases,
  cost-ratio curves, and microbenchmarks. Additional sections compare the
  adaptive adversary and reverse-map cache budgets. Individual cases follow in
  an appendix. PDF compilation requires `latexmk` and PGFPlots; `--no-pdf` skips it.
- `appendix.tex`, `appendix.pdf`, `appendix-content.tex`: the individual case
  configurations, measurements and plots, also available as a standalone report.
- `paper/`: portable grouped figures/tables, individual case snippets, and
  preamble instructions, shared by the complete report. Colors and marker shapes
  follow the camera-ready paper; orange/star identifies RandomizedSkiQF. Existing
  paper files are not modified. Four-panel snippets suit a full-width `figure*`.
- `metrics.csv`, `rounds.csv`: normalized measurements and trajectories.
- `<family>/<case>/rep-N/execution.log`: driver output including build failures.
- `<family>/<case>/rep-N/1/`: Sacred configuration, logs and registered raw CSV/JSON.
- `<filter>_adversary.csv` in immediate-feedback cases: ordinary false-positive
  feedback and replay retirements, query positions, keys, flag changes, and
  active candidate counts. Buffered events are written after the timed workload.
  Reports show regime switches, the fraction of queries with replay enabled,
  and corrections by regime. Historical window/probe reports remain readable.

Metrics distinguish final cumulative workload throughput from the subsequent
filter-only microbenchmark. Physical read metrics use WiredTiger block-manager
bytes and connection read I/Os. Reported I/O is the recorded cumulative database
or reverse-map snapshot; no baseline subtraction is introduced.
Missing measurements are shown as unavailable, not zero (except absent reverse
map for `nonAdaptive`). Repeated scalars show mean and sample standard deviation;
trajectories show means. Small and medium runs are local validation results.
The existing size metric covers QF storage, excluding derived state such as
the randomized generator and Hybrid's detector, as well as database caches.
The paper's cost-ratio plot combines the default distribution recipes at nominal
alpha=1 with the half/twice/quadruple recipes. Phase shading and query axes use
the actual run configuration, including small and medium sizes. Cache titles
show MB, rather than copying dataset percentages from the old paper. The engine
table reports adaptation-call latency as such; the current artifacts do not
isolate reverse-map lookup latency or its measured cost ratio.

Failures are recorded; remaining tests continue and the run exits nonzero with
an incomplete report. There is no resume mechanism: rerun affected cases into a
new bundle. `report` regenerates output from saved artifacts without benchmarks.
Only completed cases are included in comparisons. A stopped run remains marked
incomplete, with completed measurements preserved.

## Compatibility and setup

Existing shell entry points delegate to the runner. They accept size names;
`full` maps to `large` and `smoke` now maps to `small` (q=20, one million queries).
`RESULTS_ROOT` specifies the parent directory, `RUN_NAME` the optional run name,
and `SEED` the seed. `PYTHON` overrides the interpreter; otherwise wrappers use
the local `.venv` when available. Experiment selection uses runner arguments;
the old `FILTER_NAMES` and `ADVERSARY_WINDOW_SIZE` environment overrides are
replaced by the fixed five-filter recipes. The adaptive adversary has no window
setting; `adversary_window_size` and `--adaptiveAdversaryWindowSize` were removed.

`setup.sh` is separate and requires gcc/g++, CMake, git, curl, OpenSSL and libaio
development headers, and Python venv support. It builds database dependencies,
checks out fixed header-library revisions, and installs Python dependencies in
`.venv`. It does not install system packages. `JOBS` controls build parallelism.

```bash
make workload_gen bench_variants randomized_policy_test benchmark_workload_test
./sponge/build/randomized_policy_test
./sponge/build/benchmark_workload_test
.venv/bin/python -m unittest discover -s tests -p '*_test.py'
```

## Full medium suite followed by large

Run from the repository root, using fresh output directories:

```bash
.venv/bin/python experiments/run.py run --tests all --size medium \
  --progress --no-pdf --output sponge/tods/full-medium &&
.venv/bin/python experiments/run.py run --tests all --size large \
  --progress --no-pdf --output sponge/tods/full-large
```

`all` currently selects 43 cases, including the singleton-mixture study. The
large command starts only if the medium runner succeeds; failed cases make the
medium runner exit unsuccessfully. Both commands generate report sources.
Omit `--no-pdf` to also compile PDFs when LaTeX is installed. Use `report BUNDLE`
to regenerate a report later. Runs share scratch storage: do not run them concurrently.

`--progress` displays elapsed time for each construction stage and query counts
at the existing reporting boundaries (normally 100 updates per filter). Bulk
loading exposes no intermediate counts, so its display has no percentage or ETA.
Setup, workload generation, and compilation share an elapsed-time display.
Finalization includes supplementary filter-only microbenchmarks, which do not
expose intermediate query counts. Displayed elapsed times include observer polling delays and are not benchmark
timing measurements; use the saved metrics for analysis.

The display monitors execution logs once per second in a Python thread and
makes benchmark stdout line-buffered. It adds no per-query callbacks, but log
flushing and display activity still have overhead, including possible contention
when all processes are pinned to one CPU. The impact has not been quantified.
Omit `--progress` for the existing logging behavior and keep the setting consistent
across comparisons; the bundle manifest records it. Keep raw measurements when
normalizing or smoothing plots, and identify any smoothing explicitly.
