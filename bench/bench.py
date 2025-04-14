from sacred import Experiment
import os

ex = Experiment()

@ex.config
def test_config():
    quotient_bits=20
    remainder_bits=9
    num_queries=1000000
    distribution = 'uniform' # [u]niform or [z]ipfian or [a]dversarial

@ex.capture
def run_filter_bench(quotient_bits, remainder_bits, num_queries, distribution, filter='adaptive'):
    d = distribution[0]
    if filter == 'adaptive':
        os.system('make clean && make test_throughput_adaptive')
        os.system('./test_throughput_adaptive %s %s %s %s > output.txt' % (quotient_bits, remainder_bits, num_queries, d));
    elif filter == 'nonAdaptive':
        os.system('make clean && make test_throughput_nonAdaptive')
        os.system('./test_throughput_nonAdaptive %s %s %s %s > output.txt' % (quotient_bits, remainder_bits, num_queries, d));
    elif filter == 'DAdaptive':
        os.system('make clean && make test_throughput_DskiAdaptive')
        os.system('./test_throughput_DskiAdaptive %s %s %s %s > output.txt' % (quotient_bits, remainder_bits, num_queries, d));


@ex.automain
def run_experiment():
    run_filter_bench()
    ex.add_artifact('output.txt')
    ex.add_artifact('unif_q.csv')
    ex.add_artifact('unif_i.csv')
    pass


