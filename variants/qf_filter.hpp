#ifndef QF_FILTER
#define QF_FILTER

struct QFilterConfig {
  size_t qbits;
  size_t rbits;
  int breakEvenCount;
  double max_load_factor;
};

struct QFilterQueryResult {
  int key_present;
  int minirun_rank;
  int minirun_count;
  uint64_t hash;
  uint64_t hash_index;
};

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
  uint64_t reverseMapCacheSizeMB;
  bool shouldSort;
  bool sortAndInsertFingerprints;
};

#endif
