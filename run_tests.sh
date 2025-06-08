#!/bin/bash
  python3 ./bench/bench_filters.py  -F statRuns with num_queries=2000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=false-positive break_even=2 collect_db_stats=True microbench=True quotient_bits=25

  exit 


  python3 ./bench/bench_filters.py  -F statRuns with num_queries=40000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=64 adv_freq=1
  python3 ./bench/bench_filters.py  -F statRuns with num_queries=40000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=64 adv_freq=5
  python3 ./bench/bench_filters.py  -F statRuns with num_queries=40000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=64 adv_freq=10

  python3 ./bench/bench_filters.py  -F statRuns with num_queries=40000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=128 adv_freq=1
  python3 ./bench/bench_filters.py  -F statRuns with num_queries=40000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=128 adv_freq=5
  python3 ./bench/bench_filters.py  -F statRuns with num_queries=40000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=128 adv_freq=10

  python3 ./bench/bench_filters.py  -F statRuns with num_queries=40000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=256 adv_freq=1
  python3 ./bench/bench_filters.py  -F statRuns with num_queries=40000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=256 adv_freq=5
  python3 ./bench/bench_filters.py  -F statRuns with num_queries=40000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=256 adv_freq=10

  python3 ./bench/bench_filters.py  -F statRuns with num_queries=40000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=normal break_even=2 collect_db_stats=True quotient_bits=25
  python3 ./bench/bench_filters.py  -F statRuns with num_queries=40000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=lognormal break_even=2 collect_db_stats=True quotient_bits=25
  python3 ./bench/bench_filters.py  -F statRuns with num_queries=40000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=uniform break_even=2 collect_db_stats=True quotient_bits=25
  python3 ./bench/bench_filters.py  -F statRuns with num_queries=40000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=zipfian break_even=2 collect_db_stats=True quotient_bits=25

# microbenchmark
  python3 ./bench/bench_filters.py  -f statruns with num_queries=2000000 num_rounds=100 storage_engine=wiredtiger reverse_map_engine=wiredtiger query_workload=false-positive break_even=2 collect_db_stats=true microbench=true quotient_bits=25

exit 
for i in {1..1} 
do
  python3 ./bench/bench_filters.py  -F statRuns with num_queries=400000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=false-positive break_even=2 collect_db_stats=True
  python3 ./bench/bench_filters.py  -F statRuns with num_queries=40000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=zipfian break_even=2 collect_db_stats=True
  python3 ./bench/bench_filters.py  -F statRuns with num_queries=40000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=uniform break_even=2 collect_db_stats=True
  python3 ./bench/bench_filters.py  -F statRuns with num_queries=40000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True
done;
