#ifndef RD_ADAPTIVE_FILTER
#define RD_ADAPTIVE_FILTER

#include "qf_filter.hpp"
#include <cstddef>

extern "C" {
#include "include/gqf.h"
#include "include/test_driver.h"
}

template <typename ReverseMap> class RepeatDetectAdaptiveFilter {
public:
  int construct(BenchmarkParams params) {
    benchmarkParams = params;
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
    fullPoint = config.max_load_factor * num_slots;
    reverseMap.init("reverseMap", qf.metadata->quotient_remainder_bits, params.reverseMapCacheSizeMB, false, true, params.reverseSleepUs);
    breakEvenCount = config.breakEvenCount;

    numEmptyQueries = 0;
    numCollisions = 0;
    numCollisionsMA = 0;

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

      if (benchmarkParams.sortAndInsertFingerprints) {
        ret = reverseMap.insertFingerprint(
          result.minirun_id, result.minirun_rank, keys[i]);
      } else {
        ret = reverseMap.insertAndCommitFingerprint(
          result.minirun_id, result.minirun_rank, keys[i]);
      }

      if (ret) {
        return -1;
      }
    }
    reverseMap.commitFingerprints();
    reverseMap.close();
    reverseMap.init("reverseMap", qf.metadata->quotient_remainder_bits, benchmarkParams.reverseMapCacheSizeMB, benchmarkParams.shouldCollectDbStats, false, benchmarkParams.reverseSleepUs);
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
      numEmptyQueries++;
      if (numEmptyQueries == windowSize) {
          numCollisionsMA = (1.0 - smoothing_factor) * numCollisionsMA + smoothing_factor * numCollisions;
          numEmptyQueries = 0;
          numCollisions = 0;
      }
    }
    result->hash = hash;
    result->minirun_rank = minirun_rank;

    return 0;
  }

  int adapt(uint64_t queryKey, QFilterQueryResult *filterResult) {
    numEmptyQueries++;
    if (numEmptyQueries == windowSize) {
        numCollisionsMA = (1.0 - smoothing_factor) * numCollisionsMA + smoothing_factor * numCollisions;
        numEmptyQueries = 0;
        numCollisions = 0;
    }

    if (qf.metadata->noccupied_slots >= fullPoint) {
      return -1; // Don't have space to adapt more.
    }

    if (filterResult->minirun_count > 1) {
      numCollisions++;
    }

    if (filterResult->minirun_count >= breakEvenCount || numCollisionsMA > windowCollisionLimit) {
      uint64_t origKey;
      uint64_t fingerprint = filterResult->hash;

      int count = 0;
      int ret = reverseMap.getFingerprint(
          fingerprint, filterResult->minirun_rank, &origKey);
      if (ret) {
        printf("fingerprint fetch failed\n");
        return -1;
      }
      ret = qf_adapt_using_ll_table(
          &qf, origKey, queryKey, filterResult->minirun_rank, QF_KEY_IS_HASH);
      return 1;
    } 
    else {
      uint64_t hash = filterResult->hash;
      uint64_t hash_index = filterResult->hash_index;
      uint64_t ret_hash, ret_other_hash; // Unused, part of API.
      insert_and_extend(
          &qf,
          hash_index,
          hash,
          1, // increment by 1
          hash,
          &ret_hash,
          &ret_other_hash,
          QF_KEY_IS_HASH);
          return 0;
    } 
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

  double getAdaptiveMACost() {
    return numCollisions;
  }

  double getNonAdaptiveMACost() {
    return numCollisionsMA;
  }

private:
  QF qf;
  ReverseMap reverseMap;
  size_t fullPoint;
  int breakEvenCount;
  BenchmarkParams benchmarkParams;
  QFilterConfig config;

  double numCollisionsMA = 0;
  double smoothing_factor = 0.7;
  uint64_t windowSize = 1000000;
  uint64_t numCollisions;
  uint64_t numEmptyQueries;
  uint64_t windowCollisionLimit = 3;

#if DEBUG
  std::map<std::pair<uint64_t, uint64_t>, uint64_t> fingerprintCount;
#endif
};

#endif
