#ifndef BENCHMARK_PARAMS_HPP
#define BENCHMARK_PARAMS_HPP

#include <cstdint>
#include <string>

#include "qf_filter.hpp"

struct BenchmarkParams {
  QFilterConfig qfConfig;
  uint64_t *insertSet;
  uint64_t numInserts;
  uint64_t *querySet;
  uint64_t numQueries;
  uint64_t numRounds;
  std::string output_file;
  int is_adversarial;
  int adversarial_freq;
  int isPhasedTest; // Alternates between adversarial and workload
  int numPhases; // Total number of phases in cyclic workload. Each phase switch will alternate between adversarial and given query workload.
  bool startWithAdversarialPhase; // Start with adversarial in cyclic workload. Otherwise, start with query workload.
  bool shouldCollectDbStats;
  uint64_t storageCacheSizeMB;
  uint64_t storageSleepUs;
  bool isAdaptiveAdversary;
  bool shouldSort;
};

#endif
