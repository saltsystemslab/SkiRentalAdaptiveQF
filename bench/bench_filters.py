from sacred import Experiment
import os

ex = Experiment()
filters = ['adaptive', 'nonAdaptive', 'dSkiAdaptive', 'rSkiAdaptive', 'coinFlip', 'blockCount']

@ex.config
def test_config():
    quotient_bits=22
    remainder_bits=8
    num_queries=20000
    microbench=False
    num_rounds=100
    storage_engine = 'splinterDB'
    reverse_map_engine = 'splinterDB'
    query_workload = 'false-positive'
    adv_freq = 5
    max_adv_repeat = 0
    break_even = 24
    collect_db_stats=False
    hash_again_for_zipfian=False
    capture_extra_stats=False
    storage_cache_size_mb=64

@ex.capture
def run_filter_bench(quotient_bits, remainder_bits, num_queries, num_rounds, microbench,storage_engine, reverse_map_engine, query_workload, adv_freq, max_adv_repeat, break_even, collect_db_stats, hash_again_for_zipfian, capture_extra_stats, storage_cache_size_mb, _seed):
    extra_build_flags = ''
    if capture_extra_stats:
        extra_build_flags = ' EXTRA_STATS=1'

    os.system('make clean && make bench_variants workload_gen' + extra_build_flags)
    argDict = {
        '-q': quotient_bits,
        '-r': remainder_bits,
        '--numQueries': num_queries,
        '--numRounds': num_rounds,
        '--queryWorkload': query_workload,
        '--seed': _seed,
    }
    cmd = "./workload_gen"
    for arg_name in argDict:
        cmd = cmd + (' %s %s' % (arg_name, argDict[arg_name]))
    if hash_again_for_zipfian:
        cmd = cmd + '--hashAgainForZipfian'

    print(cmd)
    os.system(cmd)
    ex.add_artifact('queryStats')

    argDict = {
        '-q': quotient_bits,
        '-r': remainder_bits,
        '--numQueries': num_queries,
        '--numRounds': num_rounds,
        '--queryWorkload': query_workload,
        '--storageEngine': storage_engine,
        '--reverseMapEngine': reverse_map_engine,
        '--storageCacheSizeMB': str(storage_cache_size_mb),
        '--advFreq': adv_freq,
        '--breakEven': break_even,
    }
    for filter in filters:
        if filter == 'blockCount':
            os.system('make clean && make bench_variants SEVEN_BIT_OFFSET=1' + extra_build_flags)
        else:
            os.system('make clean && make bench_variants' + extra_build_flags)
        cmd = "./bench_variants --filter %s " % filter
        for arg_name in argDict:
            cmd = cmd + (' %s %s' % (arg_name, argDict[arg_name]))
        if microbench:
            cmd = cmd + ' --microBench=True'
        if collect_db_stats:
            cmd = cmd + ' --dbStats'
        print(cmd)
        os.system(cmd)
        ex.add_artifact('%s.csv' % filter)
        ex.add_artifact('%s_summary.csv' % filter)
        if capture_extra_stats:
            ex.add_artifact('%s_latency.csv' % filter)
            ex.add_artifact('%s_fp_stats.csv' % filter)

        if collect_db_stats and not microbench:
            #os.system('jq . database_wiredTiger/WiredTigerStat* > %s_db_stats.json' % filter)
            os.system('cp database_wiredTiger/WiredTigerStat* %s_db_stats.json' % filter)
            ex.add_artifact('%s_db_stats.json' % filter)
            if filter != 'nonAdaptive':
                #os.system('jq . reverseMap_wiredTiger/WiredTigerStat* > %s_rm_stats.json' % filter)
                os.system('cp reverseMap_wiredTiger/WiredTigerStat* %s_rm_stats.json' % filter)
                ex.add_artifact('%s_rm_stats.json' % filter)
    if not microbench:
        os.system('python3 ./bench/parse_db_stats.py .')
        ex.add_artifact('db_stats.csv')
        ex.add_artifact('rm_stats.csv')

@ex.automain
def run_experiment():
    run_filter_bench()
