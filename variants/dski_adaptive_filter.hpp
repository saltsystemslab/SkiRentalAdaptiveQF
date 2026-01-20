#ifndef DSKI_ADAPTIVE_FILTER
#define DSKI_ADAPTIVE_FILTER

#include "qf_filter.hpp"
#include <cstddef>
#include <unordered_map>

#define DEBUG 0

extern "C" {
#include "include/gqf.h"
#include "include/test_driver.h"
}

template <typename ReverseMap> class DSkiAdaptiveFilter {
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
    reverseMap.init("reverseMap", qf.metadata->quotient_remainder_bits, params.reverseMapCacheSizeMB, false, true);
    breakEvenCount = config.breakEvenCount;
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

#if DEBUG
      std::pair<uint64_t, uint64_t> fingerprint(
          result.minirun_id, result.minirun_rank);
      if (fingerprintCount.find(fingerprint) == fingerprintCount.end()) {
        fingerprintCount[fingerprint] = 1;
      } else {
        fingerprintCount[fingerprint]++;
      }
#endif

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
    reverseMap.init("reverseMap", qf.metadata->quotient_remainder_bits, benchmarkParams.reverseMapCacheSizeMB, benchmarkParams.shouldCollectDbStats, false);
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

#if DEBUG
    if (result->key_present) {
      std::pair<uint64_t, uint64_t> fingerprint(
          result->hash, result->minirun_rank);
      if (fingerprintCount.find(fingerprint) == fingerprintCount.end()) {
        // printf("fingerprint entry not found, bug in filter %lu %lu:
        // expected:%lu not in map\n", result->hash, result->minirun_rank,
        // minirun_count);
      } else if (fingerprintCount[fingerprint] != minirun_count) {
        // printf("fingerprint entry not found, bug in filter %lu %lu:
        // expected:%lu in_filter:%lu\n", result->hash, result->minirun_rank,
        // fingerprintCount[fingerprint], result->minirun_count);
        minirun_count = qf_get_count_using_ll_table_with_index(
            &qf, queryKey, &hash, &minirun_rank, &hash_index, QF_KEY_IS_HASH);
      }
    }
#endif
    return 0;
  }

  int adapt(uint64_t queryKey, QFilterQueryResult *filterResult) {
    if (qf.metadata->noccupied_slots >= fullPoint) {
      return -1; // Don't have space to adapt more.
    }
    if (filterResult->minirun_count < breakEvenCount) {
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
    } else {
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

#if DEBUG
      uint8_t old_minirun_rank = filterResult->minirun_rank;
      queryFilter(queryKey, filterResult);
      if (filterResult->key_present) {
        // Bug -> should not hapen.
        printf(
            "Adapting failed %lu %lu\n",
            filterResult->hash,
            filterResult->minirun_rank);
        queryFilter(queryKey, filterResult); // FOr debugging.
      }
#endif
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
    return 0.0;
  }

  double getNonAdaptiveMACost() {
    return 0.0;
  }

private:
  QF qf;
  ReverseMap reverseMap;
  size_t fullPoint;
  int breakEvenCount;
  BenchmarkParams benchmarkParams;
  QFilterConfig config;

#if DEBUG
  std::map<std::pair<uint64_t, uint64_t>, uint64_t> fingerprintCount;
#endif
};

#endif
