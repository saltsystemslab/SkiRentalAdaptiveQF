#ifndef ADAPTIVEQF
#define ADAPTIVEQF

#include "qf_filter.hpp"
#include "base_adaptive_filter.hpp"
#include <cstddef>

extern "C" {
#include "include/gqf.h"
#include "include/test_driver.h"
}

template <typename ReverseMap> 
class AdaptiveQF : public BaseAdaptiveFilter<ReverseMap> {
public:

  int adapt(uint64_t queryKey, QFilterQueryResult *filterResult) {
    if (this->qf.metadata->noccupied_slots >= this->fullPoint) {
      return -1; // Don't have space to adapt more.
    }
    uint64_t origKey;
    uint64_t fingerprint = filterResult->hash;
    int ret = this->reverseMap.getFingerprint( fingerprint,
        filterResult->minirun_rank, &origKey);
    if (ret) {
      return -1;
    }
    ret = qf_adapt_using_ll_table(
        &this->qf, origKey, queryKey, filterResult->minirun_rank, QF_KEY_IS_HASH);
    return 1;
  }
};

#endif
