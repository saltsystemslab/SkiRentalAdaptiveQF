#ifndef QF_FILTER
#define QF_FILTER

#include <cstddef>
#include <cstdint>

struct ReverseMapConfig {
  uint64_t cacheSizeMB;
  uint64_t sleepUs;
  bool shouldCollectDbStats;
  bool sortAndInsertFingerprints;
};

struct QFilterConfig {
  size_t qbits;
  size_t rbits;
  int breakEvenCount;
  double max_load_factor;
  ReverseMapConfig reverseMapConfig;
  uint64_t randomSeed;
};

struct QFilterQueryResult {
  int key_present;
  int minirun_rank;
  int minirun_count;
  uint64_t hash;
  uint64_t hash_index;
};

#endif
