#!/bin/bash

for i in {1..1} 
do
  python3 ./bench/bench_filters.py  -F statRuns with num_queries=400000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=false-positive break_even=2 collect_db_stats=True
  python3 ./bench/bench_filters.py  -F statRuns with num_queries=40000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=zipfian break_even=2 collect_db_stats=True
  python3 ./bench/bench_filters.py  -F statRuns with num_queries=40000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=uniform break_even=2 collect_db_stats=True
  python3 ./bench/bench_filters.py  -F statRuns with num_queries=40000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True
done;
