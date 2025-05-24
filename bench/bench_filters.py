from sacred import Experiment
import os

ex = Experiment()
filters = ['adaptive', 'nonAdaptive', 'dSkiAdaptive', 'rSkiAdaptive', 'coinFlip']

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

@ex.capture
def run_filter_bench(quotient_bits, remainder_bits, num_queries, num_rounds, microbench,storage_engine, reverse_map_engine, query_workload, adv_freq, max_adv_repeat, break_even, collect_db_stats, hash_again_for_zipfian, _seed):
    os.system('make clean && make bench_variants workload_gen')
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
        '--advFreq': adv_freq,
        '--breakEven': break_even,
    }
    for filter in filters:
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

        if collect_db_stats:
            os.system('jq . database_wiredTiger/WiredTigerStat* > %s_db_stats.json' % filter)
            ex.add_artifact('%s_db_stats.json' % filter)
            if filter != 'nonAdaptive':
                os.system('jq . reverseMap_wiredTiger/WiredTigerStat* > %s_rm_stats.json' % filter)
                ex.add_artifact('%s_rm_stats.json' % filter)
    os.system('python3 ./bench/parse_db_stats.py .')
    ex.add_artifact('db_stats.csv')
    ex.add_artifact('rm_stats.csv')

@ex.automain
def run_experiment():
    run_filter_bench()
