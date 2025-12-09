# Ski-Rental Augmented Adaptive Filters

This repository extends the Adaptive Quotient Filter (AQF) to adapt only if a false positive exceeds a threshold.
The threshold is determined by applying a ski-rental analysis on the false-positive detection (renting) and adaptation (buying) costs.  

The filters are provided in two variants

* SkiQF (variants/dski_adaptive.hpp)
* SampledSkiQF(variants/sample_detect_adaptive.hpp)

The SkiQF is the more robust version of two, and guarantees a 2-competitive performance on the total I/O.
The SampledSkiQF uses a sampling phase to determine whether it is optimal to adapt immediately or not. 

For code-samples on how to use the filters, please refer to `test/bench_variants.cc`. 


Experiments
-------

The experiments run in our paper can be triggered by the running the test script.

```bash
 $ ./run_tests
```
The script will run the experiments and output the results in the directory `./paper`. Each experiment will output its result in a sub-directory under `./paper`.

