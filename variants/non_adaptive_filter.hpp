
#ifndef NON_ADAPTIVE_FILTER
#define NON_ADAPTIVE_FILTER

#include <cstddef>

extern "C" {
#include "include/gqf.h"
#include "include/test_driver.h"
}

#include "qf_filter.hpp"

class NonAdaptiveFilter {
public:
  int construct(QFilterConfig config) {
    size_t num_slots = 1ull << config.qbits;
    if (!qf_malloc(
            &qf,
            num_slots,
            config.qbits + config.rbits,
            0,
            QF_HASH_INVERTIBLE,
            0)) {
      return -1;
    }
    return 0;
  }

  int bulkLoad(uint64_t *keys, uint64_t numKeys) {
    int count = 1;
    for (uint64_t i = 0; i < numKeys; i++) {
      qf_insert_result result;
      int ret = qf_insert_using_ll_table(
          &qf, keys[i], count, &result, QF_NO_LOCK | QF_KEY_IS_HASH);
      if (ret < 0) {
        return -1;
      }
    }
    return 0;
  }

  int queryFilter(uint64_t queryKey, QFilterQueryResult *result) {
    int minirun_rank;
    uint64_t hash;
    if ((minirun_rank = qf_query_using_ll_table(
             &qf, queryKey, &hash, QF_KEY_IS_HASH)) >= 0) {
      result->key_present = 1;
    } else {
      result->key_present = 0;
    }
    result->hash = hash;
    result->minirun_rank = minirun_rank;
    return 0;
  }

  int adapt(uint64_t queryKey, QFilterQueryResult *filterResult) {
    // Do nothing, this is a non-adaptive filter.
    return 0;
  }

private:
  QF qf;
};

#endif