from sacred import Experiment
import os

ex = Experiment()

@ex.config
def test_config():
    quotient_bits=22
    remainder_bits=8
    num_queries=20000
    microbench=False
    num_rounds=100

@ex.capture
def run_filter_bench(quotient_bits, remainder_bits, num_queries, num_rounds, microbench):
    os.system('make clean && make bench_variants')
    os.system('./bench_variants --filter adaptive -q %s -r %s --numQueries %s --numRounds %s --microBench=%s > output.txt' % (quotient_bits, remainder_bits, num_queries, num_rounds, microbench));
    os.system('./bench_variants --filter nonAdaptive -q %s -r %s --numQueries %s --numRounds %s --microBench=%s > output.txt' % (quotient_bits, remainder_bits, num_queries, num_rounds, microbench));
    os.system('./bench_variants --filter dSkiAdaptive -q %s -r %s --numQueries %s --numRounds %s --microBench=%s > output.txt' % (quotient_bits, remainder_bits, num_queries, num_rounds, microbench));


@ex.automain
def run_experiment():
    run_filter_bench()
    ex.add_artifact('mono.csv')
    ex.add_artifact('non.csv')
    ex.add_artifact('dski.csv')
    pass


