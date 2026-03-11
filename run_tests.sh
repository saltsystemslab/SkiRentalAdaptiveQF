#!/bin/bash
#  python3 ./bench/bench_filters.py  -F paper-small/uniform-phased-adv-first with num_queries=1000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=uniform break_even=2 collect_db_stats=True quotient_bits=24 capture_extra_stats=False is_phased_test=True num_phases=4 start_with_adversarial_phase=True 
#python3 ./bench/bench_filters.py  -F paper-microbench/uniform with num_queries=5000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=uniform break_even=2 collect_db_stats=True quotient_bits=24 capture_extra_stats=False microbench=True
# python3 ./bench/bench_filters.py  -F paper-microbench/normal with num_queries=5000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=normal break_even=2 collect_db_stats=True quotient_bits=24 capture_extra_stats=False microbench=True

#exit
#cp -r ${SPONGE_DIR} /home/chesetti.y/ 

SPONGE_DIR='./sponge/paper'

DELAY_US=130
# C_RM/C_DB = 1/2
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/uniform_delay_half with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=uniform break_even=1 collect_db_stats=True quotient_bits=27 capture_extra_stats=True  storage_sleep_us=130
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/normal_delay_half with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=normal break_even=1 collect_db_stats=True quotient_bits=27 capture_extra_stats=True storage_sleep_us=${DELAY_US}
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/lognormal_delay_half with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=lognormal break_even=1 collect_db_stats=True quotient_bits=27 capture_extra_stats=True storage_sleep_us=${DELAY_US}
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/zipf_delay_half with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=zipfian break_even=1 collect_db_stats=True quotient_bits=27 capture_extra_stats=True zipf_constant=0.99 storage_cache_size_mb=64 storage_sleep_us=${DELAY_US}

DELAY_US=130
#C_RM/C_DB=2
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/uniform_delay_twice with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=uniform break_even=3 collect_db_stats=True quotient_bits=27 capture_extra_stats=True reverse_sleep_us=${DELAY_US}
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/normal_delay_twice with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=normal break_even=3 collect_db_stats=True quotient_bits=27 capture_extra_stats=True reverse_sleep_us=${DELAY_US}
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/lognormal_delay_twice with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=lognormal break_even=3 collect_db_stats=True quotient_bits=27 capture_extra_stats=True reverse_sleep_us=${DELAY_US}
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/zipf_delay_twice with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=zipfian break_even=3 collect_db_stats=True quotient_bits=27 capture_extra_stats=True zipf_constant=0.99 storage_cache_size_mb=64 reverse_sleep_us=${DELAY_US}

DELAY_US=390
#C_RM/C_DB=2
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/uniform_delay_quad with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=uniform break_even=5 collect_db_stats=True quotient_bits=27 capture_extra_stats=True reverse_sleep_us=${DELAY_US}
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/normal_delay_quad with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=normal break_even=5 collect_db_stats=True quotient_bits=27 capture_extra_stats=True reverse_sleep_us=${DELAY_US}
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/lognormal_delay_quad with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=lognormal break_even=5 collect_db_stats=True quotient_bits=27 capture_extra_stats=True reverse_sleep_us=${DELAY_US}
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/zipf_delay_quad with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=zipfian break_even=5 collect_db_stats=True quotient_bits=27 capture_extra_stats=True zipf_constant=0.99 storage_cache_size_mb=64 reverse_sleep_us=${DELAY_US}

# Distribution tests
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/uniform with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=uniform break_even=2 collect_db_stats=True quotient_bits=27 capture_extra_stats=True
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/normal with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=normal break_even=2 collect_db_stats=True quotient_bits=27 capture_extra_stats=True
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/lognormal with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=lognormal break_even=2 collect_db_stats=True quotient_bits=27 capture_extra_stats=True
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/zipf with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=zipfian break_even=2 collect_db_stats=True quotient_bits=27 capture_extra_stats=True zipf_constant=0.99 storage_cache_size_mb=64

# Microbenchmark Tests
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/uniform-microbenchmark with num_queries=500000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=uniform break_even=2 collect_db_stats=True microbench=true quotient_bits=27
cp -r ${SPONGE_DIR} /home/chesetti.y/ 
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/normal-microbenchmark with num_queries=500000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=normal break_even=2 collect_db_stats=True microbench=true quotient_bits=27
cp -r ${SPONGE_DIR} /home/chesetti.y/ 
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/lognormal-microbenchmark with num_queries=500000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=lognormal break_even=2 collect_db_stats=True microbench=true quotient_bits=27
cp -r ${SPONGE_DIR} /home/chesetti.y/ 
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/zipf-microbenchmark with num_queries=500000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=zipfian break_even=2 collect_db_stats=True quotient_bits=27 microbench=true capture_extra_stats=True zipf_constant=0.99 storage_cache_size_mb=64
cp -r ${SPONGE_DIR} /home/chesetti.y/ 

# Phased Tests
 python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/uniform-phased-adv-first with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=uniform break_even=2 collect_db_stats=True quotient_bits=27 capture_extra_stats=False is_phased_test=True num_phases=4 start_with_adversarial_phase=True
cp -r ${SPONGE_DIR} /home/chesetti.y/ 
 python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/uniform-phased with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=uniform break_even=2 collect_db_stats=True quotient_bits=27 capture_extra_stats=False is_phased_test=True num_phases=4 start_with_adversarial_phase=False
cp -r ${SPONGE_DIR} /home/chesetti.y/ 

 # Insert test
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/insert with num_queries=1000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=uniform break_even=2 collect_db_stats=False quotient_bits=24 capture_extra_stats=True sort_and_insert_keys=False sort_and_insert_fingerprints=False is_insert_test=True
cp -r ${SPONGE_DIR} /home/chesetti.y/ 
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/insert with num_queries=1000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=splinterDB query_workload=uniform break_even=2 collect_db_stats=False quotient_bits=24 capture_extra_stats=True sort_and_insert_keys=False sort_and_insert_fingerprints=False is_insert_test=True
cp -r ${SPONGE_DIR} /home/chesetti.y/ 


# Zipf Tests
#python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/zipf with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=zipfian break_even=2 collect_db_stats=True quotient_bits=27 capture_extra_stats=True zipf_constant=0.80 storage_cache_size_mb=64
#python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/zipf with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=zipfian break_even=2 collect_db_stats=True quotient_bits=27 capture_extra_stats=True zipf_constant=1.2 storage_cache_size_mb=64
#python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/zipf with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=zipfian break_even=2 collect_db_stats=True quotient_bits=27 capture_extra_stats=True zipf_constant=1.5 storage_cache_size_mb=64

# Adversarial Tests
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/adversarial_advFreq-1_cache-64 with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=64 adv_freq=1
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/adversarial_advFreq-5_cache-64 with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=64 adv_freq=5
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/adversarial_advFreq-10_cache-64 with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=64 adv_freq=10

python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/adversarial_advFreq-1_cache-128 with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=128 adv_freq=1
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/adversarial_advFreq-5_cache-128 with num_queries=10000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=128 adv_freq=5
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/adversarial_advFreq-10_cache-128 with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=128 adv_freq=10

python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/adversarial_advFreq-1_cache-256 with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=256 adv_freq=1
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/adversarial_advFreq-5_cache-256 with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=256 adv_freq=5
python3 ./bench/bench_filters.py  -F ${SPONGE_DIR}/adversarial_advFreq-10_cache-256 with num_queries=100000000 num_rounds=100 storage_engine=wiredTiger reverse_map_engine=wiredTiger query_workload=adversarial break_even=2 collect_db_stats=True quotient_bits=27 storage_cache_size_mb=256 adv_freq=10
