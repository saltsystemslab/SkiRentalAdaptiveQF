# Online Adaptive Filters

Implementation of online adaptive quotient filters: approximate membership data structures that can learn from false positives when it is worth the I/O cost.

Adaptive filters fix the problem of high observed false positive rates on skewed workloads by learning from repeated false positives. However, adaptivity increases the disk I/O overhead of a false positive, degrading throughput on workloads with no repeated false positives.

Unlike adaptive filters that adapt on every detected false positive, online adaptive filters make an online decision on whether adaptation is worth the cost.

This repository extends the Adaptive Quotient Filter (AQF) with three online-adaptive filter designs:

| Variant | File | Description |
|---|---|---|
| **SkiQF** | [`variants/skiqf.hpp`](variants/skiqf.hpp) | Applies ski-rental analysis to the adapt/tolerate decision. Guaranteed 2-competitive on total I/O. |
| **RandomizedSkiQF** | [`variants/randomized_skiqf.hpp`](variants/randomized_skiqf.hpp) | Uses Karlin et al.'s optimal discrete randomized ski-rental policy with reproducible per-run randomness. |
| **HybridSkiQF** | [`variants/hybrid_skiqf.hpp`](variants/hybrid_skiqf.hpp) | Hybrid variant combining SkiQF with additional heuristics. |

All variants extend [`BaseAdaptiveFilter`](variants/base_adaptive_filter.hpp) and share a common query/adapt interface.

For code-samples on how to use the filters, please refer to [`bench_variants.cc`](bench/cpp/bench_variants.cc). 


Experiments
-----------

Install dependencies once, then select experiment families or individual cases.
The runner executes sequentially in shared `sponge` scratch space and saves
Sacred artifacts into one result bundle. See [the experiment catalog and
configuration](experiments/README.md) for all 38 cases.

```bash
./setup.sh
.venv/bin/python experiments/run.py --list
.venv/bin/python experiments/run.py run --tests all --size small --output sponge/tods/local-small
.venv/bin/python experiments/run.py run --tests all --size medium --output sponge/tods/local-medium
# On the measurement machine:
.venv/bin/python experiments/run.py run --tests all --size large --output /data/tods/paper-run
# Regenerate the report without running benchmarks:
.venv/bin/python experiments/run.py report /data/tods/paper-run
```

Each bundle contains `results.tex` (and `results.pdf` when `latexmk` is available),
portable paper snippets in `paper/`, normalized `metrics.csv` and `rounds.csv`,
and raw artifacts/configuration/logs under each case and repetition. Existing
paper files are untouched. The report covers every selected experiment in one
place. A populated output directory is rejected to avoid mixing runs.

The historical shell entry points now delegate to this runner. They accept
`small`, `medium`, or `large`; legacy `smoke` maps to `small` (now q=20 and one
million queries), and `full` maps to `large`. `RESULTS_ROOT` selects the parent
output directory, `RUN_NAME` optionally names the bundle, and `SEED` overrides
the default seed. Dependency setup remains separate.

Validation
----------

Focused adaptive-adversary checks:

```bash
make adaptive_adversary_test
./sponge/build/adaptive_adversary_test
.venv/bin/python -m unittest discover -s tests -p 'adaptive_feedback_report_test.py'
```

```bash
make randomized_policy_test
./sponge/build/randomized_policy_test
make benchmark_workload_test
./sponge/build/benchmark_workload_test
make clean bench_variants workload_gen
```

The Python driver also accepts `work_dir=/path/to/scratch` so validation runs
can avoid overwriting existing `./sponge` artifacts.

Runner regression checks (after building workload_gen):

```bash
.venv/bin/python -m unittest discover -s tests -p "*_test.py"
```
