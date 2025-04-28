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

#endif