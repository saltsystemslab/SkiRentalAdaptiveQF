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

@ex.capture
def run_filter_bench(quotient_bits, remainder_bits, num_queries, num_rounds, microbench,storage_engine, reverse_map_engine):
    os.system('make clean && make bench_variants')
    for filter in filters:
        print('./bench_variants --filter %s -q %s -r %s --numQueries %s --numRounds %s --microBench=%s --storageEngine %s --reverseMapEngine %s > output.txt' % (filter, quotient_bits, remainder_bits, num_queries, num_rounds, microbench, storage_engine, reverse_map_engine));
        os.system('./bench_variants --filter %s -q %s -r %s --numQueries %s --numRounds %s --microBench=%s --storageEngine %s --reverseMapEngine %s > output.txt' % (filter, quotient_bits, remainder_bits, num_queries, num_rounds, microbench, storage_engine, reverse_map_engine));
        continue


@ex.automain
def run_experiment():
    run_filter_bench()
    for filter in filters:
        ex.add_artifact('%s.csv' % filter)
    pass


