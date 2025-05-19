from sacred import Experiment
import os

ex = Experiment()
filters = ['adaptive', 'nonAdaptive', 'dSkiAdaptive', 'rSkiAdaptive']

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

@ex.capture
def run_filter_bench(quotient_bits, remainder_bits, num_queries, num_rounds, microbench,storage_engine, reverse_map_engine, query_workload, adv_freq, max_adv_repeat, break_even):
    os.system('make clean && make bench_variants')
    argDict = {
        '-q': quotient_bits,
        '-r': remainder_bits,
        '--numQueries': num_queries,
        '--numRounds': num_rounds,
        '--queryWorkload': query_workload,
        '--storageEngine': storage_engine,
        '--reverseMapEngine': reverse_map_engine,
        '--advFreq': adv_freq,
        '--maxAdvRepeat': max_adv_repeat,
        '--breakEven': break_even
    }
    for filter in filters:
        cmd = "./bench_variants --filter %s " % filter
        for arg_name in argDict:
            cmd = cmd + (' %s %s' % (arg_name, argDict[arg_name]))
        if microbench:
            cmd = cmd + ' --microBench=True'
        print(cmd)
        os.system(cmd)


@ex.automain
def run_experiment():
    run_filter_bench()
    for filter in filters:
        ex.add_artifact('%s.csv' % filter)
    pass