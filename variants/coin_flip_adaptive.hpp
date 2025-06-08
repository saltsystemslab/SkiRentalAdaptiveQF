#ifndef COIN_ADAPTIVE_FILTER
#define COIN_ADAPTIVE_FILTER

#include "qf_filter.hpp"
#include <cstddef>
#include <random>

extern "C" {
#include "include/gqf.h"
#include "include/test_driver.h"
}

// TODO(chesetti): Figure out how to do this without globals
static std::random_device rd2{};
static std::mt19937 gen2{rd2()};
static std::uniform_real_distribution<> dis2{0.0, 1.0};

template <typename ReverseMap> class CoinFlipAdaptiveFilter {
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
    fullPoint = config.max_load_factor * num_slots;
    reverseMap.init("reverseMap", config.qbits + config.rbits, params.reverseMapCacheSizeMB, false, true);
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
      // Insert key into reverse map.
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

  int adapt(uint64_t queryKey, QFilterQueryResult *filterResult) {
    if (qf.metadata->noccupied_slots >= fullPoint) {
      return -1; // Don't have space to adapt more.
    }
    if (coin_flip()) {
      uint64_t origKey;
      uint64_t fingerprint = filterResult->hash;

      int ret = reverseMap.getFingerprint(
            fingerprint, filterResult->minirun_rank, &origKey);
      if (ret) {
          printf("fingerprint fetch failed\n");
          return -1;
      }
      ret = qf_adapt_using_ll_table(
          &qf, origKey, queryKey, filterResult->minirun_rank, QF_KEY_IS_HASH);
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

private:
  bool coin_flip() {
    double flip = dis2(gen2);
    if (flip < 0.5) return 0;
    else return 1;
  }

  QF qf;
  ReverseMap reverseMap;
  size_t fullPoint;
  size_t breakEvenCount;
  QFilterConfig config;
  BenchmarkParams benchParams;

};

#endif
