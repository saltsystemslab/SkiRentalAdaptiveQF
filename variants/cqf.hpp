#ifndef CQF
#define CQF

#include "qf_filter.hpp"
#include "base_adaptive_filter.hpp"
#include <cstddef>

extern "C" {
#include "include/gqf.h"
#include "include/test_driver.h"
}

template <typename ReverseMap> 
class NonAdaptiveFilter : public BaseAdaptiveFilter<ReverseMap> {
public:
  int adapt(uint64_t queryKey, QFilterQueryResult *filterResult) {
    return 0;
  }
};

#endif
