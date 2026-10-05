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

Install the external dependencies once, then run either an individual recipe or
the complete suite. Every recipe accepts `full` (the default) or `smoke` and
records its parameters and seed with Sacred.

```bash
./setup.sh
./experiments/run-distributions.sh smoke
./experiments/run-all.sh full
```

Results default to `./sponge/tods/<experiment-name>/`. Set `RESULTS_ROOT` and
`SEED` to override the output root and reproducible seed. For example:

```bash
RESULTS_ROOT=/data/skiqf-runs SEED=12345 \
    ./experiments/run-adaptive-adversary.sh full
```

| Recipe | Experiment family |
|---|---|
| `run-distributions.sh` | Uniform, normal, lognormal, and Zipfian workloads |
| `run-cost-ratios.sh` | Reverse-map/database I/O cost ratios |
| `run-microbenchmarks.sh` | In-memory insertion and query measurements |
| `run-phased.sh` | Existing fixed workload-drift phases |
| `run-engine-comparison.sh` | WiredTiger versus SplinterDB reverse maps |
| `run-adversarial.sh` | Existing fixed adversarial replay matrix |
| `run-adaptive-adversary.sh` | Black-box adversary targeting HybridSkiQF's detection lag |
| `run-all.sh` | All recipes in sequence |

The behavioral recipes compare `adaptive`, `nonAdaptive`, `skiQF`,
`randomizedSkiQF`, and `hybridSkiQF`. Main WiredTiger experiments use a 4 MB
reverse-map cache; the engine comparison explicitly uses 64 MB because
SplinterDB does not support the smaller budget. The adaptive adversary uses a
100,000-negative-query observation window by default, configurable with
`ADVERSARY_WINDOW_SIZE`; it must remain below HybridSkiQF's one-million-query
detection window.

`run_tests.sh` remains as the historical command list for the original paper
experiments.

Validation
----------

```bash
make randomized_policy_test
./sponge/build/randomized_policy_test
make clean bench_variants workload_gen
```

The Python driver also accepts `work_dir=/path/to/scratch` so validation runs
can avoid overwriting existing `./sponge` artifacts.
