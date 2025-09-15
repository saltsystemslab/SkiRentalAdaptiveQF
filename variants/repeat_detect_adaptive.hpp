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
    numFpQueries = 0;
    numUniqueFp = 0;
    numEmptyQueries = 0;
    numCollisions = 0;
    // usefulAdapts = 0;
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

      #if 0
      uint64_t tempKey = queryKey;
      uint64_t bf_hash[4];
      bool isInBf = true;
      for (uint64_t i=0; i<4; i++) {
        bf_hash[i] = (tempKey) & ((1<<16)-1);
        tempKey = tempKey >> 16;
        isInBf = isInBf && (bf.test(bf_hash[i]));
      }
      if (isInBf) {
        usefulAdapts++; // Why is this a useful adapt?
      }
      #endif
    }
    result->hash = hash;
    result->minirun_rank = minirun_rank;
    return 0;
  }

  int adapt(uint64_t queryKey, QFilterQueryResult *filterResult) {
    numFpQueries++;
    numEmptyQueries++;
    int adapted = 0;
    if (qf.metadata->noccupied_slots >= full_point) {
      return -1; // Don't have space to adapt more.
    }
    uint64_t tempKey = queryKey;
    uint64_t bf_hash[4];
    bool isInBf = true;
    for (uint64_t i=0; i<4; i++) {
      bf_hash[i] = (tempKey) & ((1<<16)-1);
      tempKey = tempKey >> 16;
      isInBf = isInBf && (bf.test(bf_hash[i]));
    }

    // If within a window you find a repeating FP, adapt immediately.
    if (shouldAdaptNow || isInBf) {
        uint64_t origKey;
        uint64_t fingerprint = filterResult->hash;
        reverseMap.getFingerprint(
        fingerprint, filterResult->minirun_rank, &origKey);
        qf_adapt_using_ll_table(
            &qf, origKey, queryKey, filterResult->minirun_rank, QF_KEY_IS_HASH);
        adapted = 1;
    } 
    // If query key already exists, then false positive queries are repeating
    if (isInBf) {
      numCollisions++;
    } else {
      numUniqueFp++;
    }

    // Why 6? The BF can handle 3000 inserts at 0.1% FPR
    // If the query workload is uniform random, you would have 3 collision (expected). Choose 6 to be safe.
    // After 3000 false positives, numCollisions is reset.
    if (numCollisions >= 6) {
      shouldAdaptNow = 1;
    }

    // Insert query key into bloom filter.
    for (uint64_t i=0; i<4; i++) {
      bf.set(bf_hash[i]);
    }

    // Window size of a million, reset bloom filter.
    if (numFpQueries > 3209) { 
      // If the FPR is too high, continue adapting (even though the repeats weren't detected.)
      if (numEmptyQueries < 880000) {
        printf("Total: %lu numCollisions: %lu\n", numEmptyQueries, numCollisions);
        shouldAdaptNow = 1;
      }
      else if (numCollisions <= 4) {
        printf("Switching off, Total: %lu numCollisions: %lu\n", numEmptyQueries, numCollisions);
        shouldAdaptNow = 0;
      }
      numUniqueFp = 0;
      numFpQueries = 0;
      numCollisions = 0;
      numEmptyQueries = 0;
      // usefulAdapts = 0;
      bf.reset();
    }
    return adapted; // Not adapting.
  }

  double loadFactor() {
    return (double)qf.metadata->noccupied_slots / qf.metadata->nslots;
  }

  int close() {
    reverseMap.close();
    return 0;
  }

  uint64_t sizeInBytes() {
    return qf.metadata->total_size_in_bytes + bf.size()/8 + sizeof(uint64_t) * 5;
  }

private:
  QF qf;
  ReverseMap reverseMap;
  size_t full_point;
  BenchmarkParams benchParams;
  QFilterConfig config;

  bool shouldAdaptNow;
  uint64_t numFpQueries;
  uint64_t numEmptyQueries;
  uint64_t numUniqueFp;
  uint64_t numCollisions;
  // uint64_t usefulAdapts;
  uint64_t bfLimit;
  std::bitset<65536> bf;
};

#endif
