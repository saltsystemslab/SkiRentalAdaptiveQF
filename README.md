# Online Adaptive Filters

Implementation of online adaptive quotient filters: approximate membership data structures that can learn from false positives when it is worth the I/O cost.

Adaptive filters fix the problem of high observed false positive rates on skewed workloads by learning from repeated false positives. However, adaptivity increases the disk I/O overhead of a false positive, degrading throughput on workloads with no repeated false positives.

Unlike adaptive filters that adapt on every detected false positive, online adaptive filters make an online decision on whether adaptation is worth the cost.

This repository extends the Adaptive Quotient Filter (AQF) with two online-adaptive filter designs:

| Variant | File | Description |
|---|---|---|
| **SkiQF** | [`variants/skiqf.hpp`](variants/skiqf.hpp) | Applies ski-rental analysis to the adapt/tolerate decision. Guaranteed 2-competitive on total I/O. |
| **HybridSkiQF** | [`variants/hybrid_skiqf.hpp`](variants/hybrid_skiqf.hpp) | Hybrid variant combining SkiQF with additional heuristics. |

All variants extend [`BaseAdaptiveFilter`](variants/base_adaptive_filter.hpp) and share a common query/adapt interface.

For code-samples on how to use the filters, please refer to [`bench_variants.cc`](bench/cpp/bench_variants.cc). 


Experiments
-------

The experiments run in our paper can be triggered by the running the test script.

```bash
$ ./setup.sh   # builds external dependencies (SplinterDB, WiredTiger)
$ ./run_tests
```

Runs the full experiment suite from the paper. Results are written to ./paper/<experiment-name>/.

