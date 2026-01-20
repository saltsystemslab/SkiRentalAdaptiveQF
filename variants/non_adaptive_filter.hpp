
#ifndef NON_ADAPTIVE_FILTER
#define NON_ADAPTIVE_FILTER

#include <cstddef>

extern "C" {
#if USE_CQF
#include "other_filters/cqf/include/gqf.h"
#else
#include "include/gqf.h"
#endif
#include "include/test_driver.h"
}

#include "qf_filter.hpp"

class NonAdaptiveFilter {
public:
  int construct(BenchmarkParams params) {
    QFilterConfig config = params.qfConfig;
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
      #if USE_CQF
      int ret = qf_insert(
          &qf, keys[i], 0, count, QF_NO_LOCK | QF_KEY_IS_HASH);
      #else
      qf_insert_result result;
      int ret = qf_insert_using_ll_table(
          &qf, keys[i], count, &result, QF_NO_LOCK | QF_KEY_IS_HASH);
      #endif
      if (ret < 0) {
        return -1;
      }
    }
    return 0;
  }

  int queryFilter(uint64_t queryKey, QFilterQueryResult *result) {
    int minirun_rank;
    uint64_t hash;
    #if USE_CQF
    uint64_t value;
    result->key_present = qf_query(&qf, queryKey, &value, QF_NO_LOCK | QF_KEY_IS_HASH);
    result->hash = 0;
    result->minirun_rank = 0;
    #else
    if ((minirun_rank = qf_query_using_ll_table(
             &qf, queryKey, &hash, QF_KEY_IS_HASH)) >= 0) {
      result->key_present = 1;
    } else {
      result->key_present = 0;
    }
    result->hash = hash;
    result->minirun_rank = minirun_rank;
    #endif
    return 0;
  }

  int adapt(uint64_t queryKey, QFilterQueryResult *filterResult) {
    // Do nothing, this is a non-adaptive filter.
    return 0;
  }

  double loadFactor() {
    return (double)qf.metadata->noccupied_slots / qf.metadata->nslots;
  }

  int close() {
    return 0;
  }

  uint64_t sizeInBytes() {
    return qf.metadata->total_size_in_bytes;
  }

  double getAdaptiveMACost() {
    return 0.0;
  }

  double getNonAdaptiveMACost() {
    return 0.0;
  }

private:
  QF qf;
};

#endif
