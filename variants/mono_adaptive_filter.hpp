#ifndef MONO_ADAPTIVE_FILTER
#define MONO_ADAPTIVE_FILTER

#include "qf_filter.hpp"
#include <cstddef>

extern "C" {
#include "include/gqf.h"
#include "include/test_driver.h"
}

template <typename ReverseMap> class MonotonicAdaptiveFilter {
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
    full_point = config.max_load_factor * num_slots;
    reverseMap.init("reverseMap");

    return 0;
  }

  int bulkLoad(uint64_t *keys, uint64_t numKeys) {
    int count = 1;
    for (uint64_t i = 0; qf.metadata->noccupied_slots < numKeys; i++) {
      // Insert key into filter.
      qf_insert_result result;
      int ret = qf_insert_using_ll_table(
          &qf, keys[i], count, &result, QF_NO_LOCK | QF_KEY_IS_HASH);
      if (ret < 0) {
        return -1;
      }

      // Insert key into reverse map.
      uint64_t fingerprint = result.minirun_id
                             << (64 - qf.metadata->quotient_remainder_bits);
      uint64_t value = keys[i];
      ret = reverseMap.insertKV(fingerprint, value, result.minirun_existed);
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
    if (qf.metadata->noccupied_slots >= full_point) {
      return -1; // Don't have space to adapt more.
    }
    uint64_t origKey;
    uint64_t fingerprint = filterResult->hash
                           << (64 - qf.metadata->quotient_remainder_bits);
    reverseMap.getKeyAtRank(fingerprint, filterResult->minirun_rank, &origKey);
    qf_adapt_using_ll_table(
        &qf, origKey, queryKey, filterResult->minirun_rank, QF_KEY_IS_HASH);
    return 0;
  }

  double loadFactor() {
    return (double)qf.metadata->noccupied_slots / qf.metadata->nslots;
  }

private:
  QF qf;
  ReverseMap reverseMap;
  size_t full_point;
};

#endif