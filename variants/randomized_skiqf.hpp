#ifndef RANDOMIZED_SKIQF_FILTER
#define RANDOMIZED_SKIQF_FILTER

#include "base_adaptive_filter.hpp"
#include "qf_filter.hpp"
#include "randomized_ski_rental_policy.hpp"

#include <cstddef>
#include <cstdint>

extern "C" {
#include "include/gqf.h"
#include "include/test_driver.h"
}

template <typename ReverseMap>
class RandomizedSkiQF : public BaseAdaptiveFilter<ReverseMap> {
public:
  int construct(BenchmarkParams params) {
    if (params.qfConfig.breakEvenCount < 1) {
      return -1;
    }

    // Keep policy randomness deterministic and independent from workload
    // generation. The constant is a fixed stream discriminator.
    policy.seed(params.randomSeed ^ 0x6a09e667f3bcc909ULL);
    return BaseAdaptiveFilter<ReverseMap>::construct(params);
  }

  static double adaptationProbability(uint64_t count, uint64_t breakEven) {
    return RandomizedSkiRentalPolicy::adaptationProbability(count, breakEven);
  }

  int adapt(uint64_t queryKey, QFilterQueryResult *filterResult) {
    if (this->qf.metadata->noccupied_slots >= this->fullPoint) {
      return -1;
    }

    const uint64_t breakEven =
        static_cast<uint64_t>(this->config.breakEvenCount);
    if (!policy.shouldAdapt(filterResult->minirun_count, breakEven)) {
      uint64_t retHash;
      uint64_t retOtherHash;
      insert_and_extend(
          &this->qf,
          filterResult->hash_index,
          filterResult->hash,
          1,
          filterResult->hash,
          &retHash,
          &retOtherHash,
          QF_KEY_IS_HASH);
      return 0;
    }

    uint64_t originalKey;
    const int lookupResult = this->reverseMap.getFingerprint(
        filterResult->hash,
        filterResult->minirun_rank,
        &originalKey);
    if (lookupResult) {
      printf("fingerprint fetch failed\n");
      return -1;
    }

    qf_adapt_using_ll_table(
        &this->qf,
        originalKey,
        queryKey,
        filterResult->minirun_rank,
        QF_KEY_IS_HASH);
    return 1;
  }

private:
  RandomizedSkiRentalPolicy policy;
};

#endif
