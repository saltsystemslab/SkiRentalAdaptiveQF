#ifndef DUMMY_BACKING_STORE_H
#define DUMMY_BACKING_STORE_H

extern "C" {
#include "include/splinter_util.h"
#include "include/test_driver.h"
}

class DummyDBBackingStore {
public:
  int init(char *dbName, int quotient_remainder_bits, int cache_size_mb, int collectDBStats) {
    this->quotient_remainder_bits = quotient_remainder_bits;
    return 0;
  }

  int insertFingerprint(uint64_t fingerprint, uint64_t rank, uint64_t key) {
    return 0;
  }

  int getFingerprint(uint64_t fingerprint, int rank, uint64_t *value) {
    uint64_t mask = -1;
    uint64_t rank_offset = rank;
    rank_offset = rank_offset << (64 - quotient_remainder_bits);

    mask = mask << (64 - quotient_remainder_bits);
    mask = mask - rank_offset;

    *value = fingerprint | mask; // Fill the higher order bits with 1's
    return 0;
  }

  int insertKV(uint64_t key, uint64_t value, int isUpdate) { return 0; }

  int searchKV(uint64_t key) {
    return 0; // Always false.
  }

  int close() { 
    return 0; 
  }

private:
  int quotient_remainder_bits;
};

#endif
