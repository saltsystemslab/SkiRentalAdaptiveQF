#ifndef RSKI_ADAPTIVE_FILTER
#define RSKI_ADAPTIVE_FILTER

#include "qf_filter.hpp"
#include <random>
#include <cstddef>

extern "C" {
#include "include/gqf.h"
#include "include/test_driver.h"
}

// TODO(chesetti): Figure out how to do this without globals
static std::random_device rd{};
static std::mt19937 gen{rd()};
static std::uniform_real_distribution<> dis{0.0, 1.0};

template <typename ReverseMap> class RSkiAdaptiveFilter {
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
    fullPoint = config.max_load_factor * num_slots;
    reverseMap.init("reverseMap");
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
    if (coin_flip(filterResult->minirun_count)) {
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
    } else {
      uint64_t origKey;
      uint64_t fingerprint = filterResult->hash
                             << (64 - qf.metadata->quotient_remainder_bits);
      reverseMap.getKeyAtRank(
          fingerprint, filterResult->minirun_rank, &origKey);
      qf_adapt_using_ll_table(
          &qf, origKey, queryKey, filterResult->minirun_rank, QF_KEY_IS_HASH);
    }
    return 0;
  }

private:
/*
TODO(chesetti): Double check coinflip logic.
Hard-coded using below python script
 b = 15                                                                                                                                                                                            
 ep = (1+1/b)**b
 a = ep/(ep-1) - 1.0
 probs = []
 for i in range(1, b+1):
     prob = a/b * (((b+1)/b)**(i-1))
     probs.append(prob)
 print(probs)

 Also only works for breakEvenDay=15.
*/
 const double prob_distribution[15] = {
      0.04082769035010176,
      0.043549536373441874,
      0.046452838798338,
      0.049549694718227205,
      0.05285300769944235,
      0.056376541546071836,
      0.06013497764914329,
      0.06414397615908618,
      0.06842024123635858,
      0.07298159065211583,
      0.07784703002892354,
      0.08303683203085178,
      0.08857262083290857,
      0.09447746222176913,
      0.10077595970322041};

  bool coin_flip(int day) {
    if (day >= 15)
      return 1;
    double flip = dis(gen);
    return flip <= prob_distribution[day];
  }

  QF qf;
  ReverseMap reverseMap;
  size_t fullPoint;
  size_t breakEvenCount;



};

#endif