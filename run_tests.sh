#!/bin/bash
#python3 ./bench/bench_filters.py  -F paper/zipf with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=zipfian break_even=2 collect_db_stats=True quotient_bits=27 capture_extra_stats=True zipf_constant=0.80 storage_cache_size_mb=64
#python3 ./bench/bench_filters.py  -F paper/zipf with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=zipfian break_even=2 collect_db_stats=True quotient_bits=27 capture_extra_stats=True zipf_constant=1.2 storage_cache_size_mb=64
#python3 ./bench/bench_filters.py  -F paper/zipf with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=zipfian break_even=2 collect_db_stats=True quotient_bits=27 capture_extra_stats=True zipf_constant=1.5 storage_cache_size_mb=64

python3 ./bench/bench_filters.py  -F paper/zipf with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=zipfian break_even=2 collect_db_stats=True quotient_bits=27 capture_extra_stats=True zipf_constant=0.99 storage_cache_size_mb=64
python3 ./bench/bench_filters.py  -F paper/normal with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=normal break_even=2 collect_db_stats=True quotient_bits=27 capture_extra_stats=True
python3 ./bench/bench_filters.py  -F paper/lognormal with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=lognormal break_even=2 collect_db_stats=True quotient_bits=27 capture_extra_stats=True
python3 ./bench/bench_filters.py  -F paper/uniform with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=uniform break_even=2 collect_db_stats=True quotient_bits=27 capture_extra_stats=True

python3 ./bench/bench_filters.py  -F paper/uniform-microbenchmark with num_queries=500000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=uniform break_even=2 collect_db_stats=True microbench=true quotient_bits=27
python3 ./bench/bench_filters.py  -F paper/normal-microbenchmark with num_queries=500000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=normal break_even=2 collect_db_stats=True microbench=true quotient_bits=27
python3 ./bench/bench_filters.py  -F paper/lognormal-microbenchmark with num_queries=500000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=lognormal break_even=2 collect_db_stats=True microbench=true quotient_bits=27
python3 ./bench/bench_filters.py  -F paper/zipf-microbenchmark with num_queries=500000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=zipfian break_even=2 collect_db_stats=True quotient_bits=27 microbench=true capture_extra_stats=True zipf_constant=0.99 storage_cache_size_mb=1

# Adversarial Tests
python3 ./bench/bench_filters.py  -F paper/adversarial_advFreq-1_cache-64 with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=64 adv_freq=1
python3 ./bench/bench_filters.py  -F paper/adversarial_advFreq-1_cache-64 with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=64 adv_freq=5
python3 ./bench/bench_filters.py  -F paper/adversarial_advFreq-1_cache-64 with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=64 adv_freq=10

python3 ./bench/bench_filters.py  -F statRuns/adversarial_advFreq-1_cache-128 with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=128 adv_freq=1
python3 ./bench/bench_filters.py  -F statRuns/adversarial_advFreq-5_cache-128 with num_queries=10000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=128 adv_freq=5
python3 ./bench/bench_filters.py  -F statRuns/adversarial_advFreq-10_cache-128 with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=128 adv_freq=10

python3 ./bench/bench_filters.py  -F statRuns/adversarial_advFreq-1_cache-256 with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=256 adv_freq=1
python3 ./bench/bench_filters.py  -F statRuns/adversarial_advFreq-5_cache-256 with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=256 adv_freq=5
python3 ./bench/bench_filters.py  -F statRuns/adversarial_advFreq-10_cache-256 with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=256 adv_freq=10


