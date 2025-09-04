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
  bool shouldCollectDbStats;
  uint64_t storageCacheSizeMB;
  uint64_t reverseMapCacheSizeMB;
  bool shouldSort;
};

#endif
