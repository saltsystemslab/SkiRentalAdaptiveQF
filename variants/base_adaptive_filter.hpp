#ifndef BASE_ADAPTIVE_FILTER
#define BASE_ADAPTIVE_FILTER

#include "qf_filter.hpp"
#include <cstddef>

extern "C" {
#include "include/gqf.h"
#include "include/test_driver.h"
}

template <typename ReverseMap> class BaseAdaptiveFilter {
public:
  int construct(BenchmarkParams params) {
    benchParams = params;
    config = params.qfConfig;
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
    reverseMap.init("reverseMap", config.qbits + config.rbits, params.reverseMapCacheSizeMB, false, true, params.reverseSleepUs);
    fullPoint = this->config.max_load_factor * (1ULL << this->config.qbits);
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
      if (benchParams.sortAndInsertFingerprints) {
        ret = reverseMap.insertFingerprint(
          result.minirun_id, result.minirun_rank, keys[i]);
      } else {
        ret = reverseMap.insertAndCommitFingerprint(
          result.minirun_id, result.minirun_rank, keys[i]);
      }
      if (ret < 0) {
        return -1;
      }
    }
    reverseMap.commitFingerprints();
    reverseMap.close();
    reverseMap.init("reverseMap", config.qbits + config.rbits, benchParams.reverseMapCacheSizeMB, benchParams.shouldCollectDbStats, false, benchParams.reverseSleepUs);
    return 0;
  }

  int queryFilter(uint64_t queryKey, QFilterQueryResult *result) {
    uint8_t minirun_rank, minirun_count;
    uint64_t hash;
    uint64_t hash_index;
    if ((minirun_count = qf_get_count_using_ll_table_with_index(
             &qf,
             queryKey,
             &hash,
             &minirun_rank,
             &hash_index,
             QF_KEY_IS_HASH)) > 0) {
      result->key_present = 1;
      result->minirun_count = minirun_count;
      result->hash_index = hash_index;
    } else {
      result->key_present = 0;
    }
    result->hash = hash;
    result->minirun_rank = minirun_rank;
    return 0;
  }

  double loadFactor() {
    return (double)qf.metadata->noccupied_slots / qf.metadata->nslots;
  }

  int close() {
    reverseMap.close();
    return 0;
  }

  uint64_t sizeInBytes() {
    return qf.metadata->total_size_in_bytes;
  }

protected:
  QF qf;
  ReverseMap reverseMap;
  QFilterConfig config;
  BenchmarkParams benchParams;
  size_t fullPoint;

};

#endif
