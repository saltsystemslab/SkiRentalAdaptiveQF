#ifndef SKIQF_FILTER
#define SKIQF_FILTER

#include "qf_filter.hpp"
#include "base_adaptive_filter.hpp"
#include <cstddef>

extern "C" {
#include "include/gqf.h"
#include "include/test_driver.h"
}

template <typename ReverseMap> 
class SkiQF: public BaseAdaptiveFilter<ReverseMap> {
public:

  int adapt(uint64_t queryKey, QFilterQueryResult *filterResult) {
    if (this->qf.metadata->noccupied_slots >= this->fullPoint) {
      return -1; // Don't have space to adapt more.
    }
    if (filterResult->minirun_count < this->config.breakEvenCount) {
      uint64_t hash = filterResult->hash;
      uint64_t hash_index = filterResult->hash_index;
      uint64_t ret_hash, ret_other_hash; // Unused, part of API.
      insert_and_extend(
          &this->qf,
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

      int ret = this->reverseMap.getFingerprint(
          fingerprint, filterResult->minirun_rank, &origKey);
      if (ret) {
        printf("fingerprint fetch failed\n");
        return -1;
      }
      ret = qf_adapt_using_ll_table(
          &this->qf, origKey, queryKey, filterResult->minirun_rank, QF_KEY_IS_HASH);
      return 1;
    }
  }

};

#endif
