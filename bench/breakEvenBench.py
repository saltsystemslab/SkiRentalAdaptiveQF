from sacred import Experiment
import os

ex = Experiment()

@ex.config
def test_config():
    quotient_bits=20
    remainder_bits=9
    num_queries=1000000
    filter = 'adaptive' # [a]daptive or [n]on-adaptive, [r]everse-map, [d]atabase 

@ex.capture
def run_filter_bench(quotient_bits, remainder_bits, num_queries, filter='adaptive'):
    os.system('make clean && make bench_breakeven_ratio')
    os.system('./bench_breakeven_ratio %s %s %s %s > output.txt' % (quotient_bits, remainder_bits, num_queries, filter[0]));


@ex.automain
def run_experiment():
    run_filter_bench()
    ex.add_artifact('output.txt')


