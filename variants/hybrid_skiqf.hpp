#ifndef HYBRID_SKIQF_ADAPTIVE_FILTER
#define HYBRID_SKIQF_ADAPTIVE_FILTER

#include "qf_filter.hpp"
#include "base_adaptive_filter.hpp"
#include <cstddef>
#include <bitset>

extern "C" {
#include "include/gqf.h"
#include "include/test_driver.h"
}

template <typename ReverseMap> 
class HybridSkiQF: public BaseAdaptiveFilter <ReverseMap> {
public:

  int queryFilter(uint64_t queryKey, QFilterQueryResult *result) {
    int minirun_rank;
    uint64_t hash;
    int ext_len = 0;
    if ((minirun_rank = qf_query_using_ll_table_with_ext_len(
             &this->qf, queryKey, &hash, &ext_len, QF_KEY_IS_HASH)) >= 0) {
      result->key_present = 1;
    } else {
      result->key_present = 0;
      windowProgress++;
      if (ext_len > 0) {
        windowCorrectedFPs++;
      }
      if (windowProgress == windowSize) {
        avgRepeatFPsPerWindow = (1.0 - smoothingFactor) * avgRepeatFPsPerWindow + smoothingFactor * windowRepeatFPs;
        avgCorrectedFPsPerWindow = (1.0 - smoothingFactor) * avgCorrectedFPsPerWindow + smoothingFactor * windowCorrectedFPs;
        correctedFpThreshold = (uint64_t)(2 * windowSize * numAdapts / (1ull << (this->config.qbits + this->config.rbits)));
        windowProgress = 0;
        windowRepeatFPs = 0;
        windowCorrectedFPs = 0;
        repeatDetectorFilter.reset();
      }
    }
    result->hash = hash;
    result->minirun_rank = minirun_rank;
    return 0;
  }

  int adapt(uint64_t queryKey, QFilterQueryResult *filterResult) {
    uint64_t tempKey = queryKey;
    uint64_t repeatDetectorFilterHash[4];
    bool isRepeatedFp = true;
    for (uint64_t i=0; i<4; i++) {
      repeatDetectorFilterHash[i] = (tempKey) & ((1<<16)-1);
      tempKey = tempKey >> 16;
      isRepeatedFp = isRepeatedFp && (repeatDetectorFilter.test(repeatDetectorFilterHash[i]));
    }

    if (isRepeatedFp) {
      windowRepeatFPs++;
    } else {
      for (uint64_t i=0; i<4; i++) {
        repeatDetectorFilter.set(repeatDetectorFilterHash[i]);
      }
    }

    windowProgress++;
    if (windowProgress == windowSize) {
        avgRepeatFPsPerWindow = (1.0 - smoothingFactor) * avgRepeatFPsPerWindow + smoothingFactor * windowRepeatFPs;
        avgCorrectedFPsPerWindow = (1.0 - smoothingFactor) * avgCorrectedFPsPerWindow + smoothingFactor * windowCorrectedFPs;
        correctedFpThreshold = (uint64_t)(2 * windowSize * numAdapts / (1ull << (this->config.qbits + this->config.rbits)));
        windowProgress = 0;
        windowRepeatFPs = 0;
        windowCorrectedFPs = 0;
        repeatDetectorFilter.reset();
    }

    int adapted;
    if (avgRepeatFPsPerWindow > repeatedFpThreshold || avgCorrectedFPsPerWindow > correctedFpThreshold) {
      uint64_t origKey;
      uint64_t fingerprint = filterResult->hash;
      this->reverseMap.getFingerprint(
      fingerprint, filterResult->minirun_rank, &origKey);
      qf_adapt_using_ll_table(
          &this->qf, origKey, queryKey, filterResult->minirun_rank, QF_KEY_IS_HASH);
      adapted = 1;
    } else {
      adapted = 0;
    }
    numAdapts += adapted;
    return adapted;
  }

private:
  std::bitset<65536> repeatDetectorFilter; // Repeat-Detector Filter.

  uint64_t windowProgress=0; // Window progress is measured in number of
                             // queries for non-existent keys (true negatives
                             // AND false positives).
  uint64_t windowRepeatFPs=0;   // Number of repeated false positives (collided
                                // with the repeat-detector bloom filter)
  uint64_t windowCorrectedFPs=0; // Number of fixed false positives from
                                 // adaptations in a window.
  double avgRepeatFPsPerWindow = 0;   // Moving average of repeated false
                                      // positives per window
  double avgCorrectedFPsPerWindow = 0.0; // Moving average of corrected false
                                         // positives per window.
  uint64_t numAdapts = 0;   // Number of adaptations performed so far.
  uint64_t correctedFpThreshold = 0; // Threshold of number of queries expected
                                     // to collide with adapted fingerprints
                                     // under a uniform random workload.
                                     // Exceeding this implies adaptations have
                                     // been useful, justifying further
                                     // adaptations.
  double smoothingFactor = 0.7;
  uint64_t windowSize = 1000000;
  // On uniform random workloads, repeated false positives should be 0 mostly.
  // However, the repeat-detector BF has its own false positive, so the
  // threshold needs to compensate for that.
  uint64_t repeatedFpThreshold=6;
};

#endif
