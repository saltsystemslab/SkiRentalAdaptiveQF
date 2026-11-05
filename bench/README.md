The [experiment runner](../experiments/README.md) selects paper recipes and
generates a complete results report. For a standalone benchmark from the project
directory:

```bash
.venv/bin/python bench/bench_filters.py -F sponge/manual-results with quotient_bits=20 remainder_bits=8 num_queries=100000 query_workload=uniform storage_engine=wiredTiger reverse_map_engine=wiredTiger storage_cache_size_mb=64 reverse_map_cache_size_mb=4 break_even=2
```

Only one benchmark should use shared `sponge` scratch storage at a time.
The raw driver's historical defaults use SplinterDB; the experiment runner
supplies the WiredTiger paper configuration explicitly.
