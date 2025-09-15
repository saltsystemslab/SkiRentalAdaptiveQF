#ifndef SD_ADAPTIVE_FILTER
#define SD_ADAPTIVE_FILTER

#include "qf_filter.hpp"
#include <cstddef>

extern "C" {
#include "include/gqf.h"
#include "include/test_driver.h"
}

template <typename ReverseMap> class SampleDetectAdaptiveFilter {
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
    full_point = config.max_load_factor * num_slots;
    reverseMap.init("reverseMap", config.qbits + config.rbits, params.reverseMapCacheSizeMB, false, true);

    shouldAdaptNow = false;
    numEmptyQueries = 0;
    numCollisions = 0;
    return 0;
  }

  int bulkLoad(uint64_t *keys, uint64_t numKeys) {
    int count = 1;
    for (uint64_t i = 0; qf.metadata->noccupied_slots < numKeys; i++) {
      // Insert key into filter.
      qf_insert_result result;
      result.minirun_rank = 0;
      int ret = qf_insert_using_ll_table(
          &qf, keys[i], count, &result, QF_NO_LOCK | QF_KEY_IS_HASH);
      if (ret < 0) {
        return -1;
      }
      uint64_t fingerprint = result.minirun_id;
      uint64_t value = keys[i];
      ret =
          reverseMap.insertFingerprint(fingerprint, result.minirun_rank, value);
      if (ret < 0) {
        return -1;
      }
    }
    reverseMap.commitFingerprints();
    reverseMap.close();
    reverseMap.init("reverseMap", config.qbits + config.rbits, benchParams.reverseMapCacheSizeMB, benchParams.shouldCollectDbStats, false);
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
      numEmptyQueries++;
    }
    result->hash = hash;
    result->minirun_rank = minirun_rank;
    return 0;
  }

  int adapt(uint64_t queryKey, QFilterQueryResult *filterResult) {
    numEmptyQueries++;
    int adapted = 0;
    if (qf.metadata->noccupied_slots >= full_point) {
      return -1; // Don't have space to adapt more.
    }

    if (shouldAdaptNow || numEmptyQueries >= sampleThreshold) {
      if (shouldAdaptNow) {
        uint64_t origKey;
        uint64_t fingerprint = filterResult->hash;
        reverseMap.getFingerprint(
        fingerprint, filterResult->minirun_rank, &origKey);
        qf_adapt_using_ll_table(
            &qf, origKey, queryKey, filterResult->minirun_rank, QF_KEY_IS_HASH);
        adapted = 1;
      } else {
        adapted = 0; // Don't adapt
      }
    }
    else if (numEmptyQueries < sampleThreshold) {
      uint64_t tempKey = queryKey;
      uint64_t bf_hash[4];
      bool isInBf = true;
      for (uint64_t i=0; i<4; i++) {
        bf_hash[i] = (tempKey) & ((1<<16)-1);
        tempKey = tempKey >> 16;
        isInBf = isInBf && (bf.test(bf_hash[i]));
      }
      if (isInBf) {
        numCollisions++;
        uint64_t origKey;
        uint64_t fingerprint = filterResult->hash;
        reverseMap.getFingerprint(
        fingerprint, filterResult->minirun_rank, &origKey);
        qf_adapt_using_ll_table(
            &qf, origKey, queryKey, filterResult->minirun_rank, QF_KEY_IS_HASH);
        adapted = 1;
      }     
      if (numCollisions >= bf_FpThreshold) {
        shouldAdaptNow = 1;
      }
      // Insert query key into bloom filter.
      for (uint64_t i=0; i<4; i++) {
        bf.set(bf_hash[i]);
      }
    }
    return adapted; // Not adapting.
  }

  double loadFactor() {
    return (double)qf.metadata->noccupied_slots / qf.metadata->nslots;
  }

  uint64_t sizeInBytes() {
    return qf.metadata->total_size_in_bytes + bf.size()/8 + sizeof(uint64_t) * 5;
  }

  int close() {
    reverseMap.close();
    return 0;
  }

private:
  QF qf;
  ReverseMap reverseMap;
  size_t full_point;
  BenchmarkParams benchParams;
  QFilterConfig config;

  bool shouldAdaptNow;
  uint64_t numEmptyQueries;
  uint64_t numCollisions;
  uint64_t sampleThreshold=1000000;
  uint64_t bf_FpThreshold=6;
  std::bitset<65536> bf;
};

#endif
