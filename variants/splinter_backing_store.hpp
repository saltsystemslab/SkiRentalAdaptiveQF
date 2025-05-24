#ifndef SPLINTER_BACKING_STORE_H
#define SPLINTER_BACKING_STORE_H

extern "C" {
#include "include/splinter_util.h"
#include "include/test_driver.h"
}

class SplinterDBBackingStore {
public:
  int init(std::string dbName, int quotient_remainder_bits, int cache_size_mb, int collectStats, bool clearOld) {
    dbName = dbName + "_splinterDb";
    data_cfg = qf_data_config_init();
    splinterdb_cfg = qf_splinterdb_config_init(dbName.c_str(), &data_cfg);
    splinterdb_cfg.cache_size = cache_size_mb * Mega;
    
    if (clearOld) {
      remove(splinterdb_cfg.filename);
    }
    if (splinterdb_create(&splinterdb_cfg, &db)) {
      return -1;
    }
    splinterdb_lookup_result_init(db, &db_result, 0, NULL);
    this->quotient_remainder_bits = quotient_remainder_bits;
    if (collectStats) {
      fprintf(stderr, "SplinterDB does not support collecting stats\n");
      abort();
    }
    return 0;
  }

  int insertKV(uint64_t key, uint64_t value, int isUpdate) {
    if (!db_insert(db, &key, sizeof(key), &value, sizeof(value), isUpdate, 0)) {
      return -1;
    }
    return 0;
  }

  int searchKV(uint64_t key) {
    slice db_query = padded_slice(&key, MAX_KEY_SIZE, sizeof(key), buffer, 0);
    splinterdb_lookup(db, db_query, &db_result);
    return splinterdb_lookup_found(&db_result);
  }

  int insertFingerprint(uint64_t fingerprint, int rank, uint64_t key) {
    int isUpdate = (rank > 0);
    uint64_t minirunBitmask = (1ULL << quotient_remainder_bits) - 1;
    uint64_t queryFingerprint = (fingerprint & minirunBitmask)
                                << (64 - quotient_remainder_bits);
    return db_insert(
        db,
        &queryFingerprint,
        sizeof(queryFingerprint),
        &key,
        sizeof(key),
        isUpdate,
        0);
  }

  int getFingerprint(uint64_t fingerprint, int rank, uint64_t *value) {
    uint64_t minirunBitmask = (1ULL << quotient_remainder_bits) - 1;
    uint64_t queryFingerprint = (fingerprint & minirunBitmask)
                                << (64 - quotient_remainder_bits);
    slice db_query = padded_slice(
        &queryFingerprint, MAX_KEY_SIZE, sizeof(queryFingerprint), buffer, 0);
    splinterdb_lookup(db, db_query, &db_result);
    slice result_val;
    splinterdb_lookup_result_value(&db_result, &result_val);
    // TODO(chesetti): Check if result at rank exists.
    if (splinterdb_lookup_found(&db_result)) {
      memcpy(
          value,
          slice_data(result_val) + rank * MAX_KEY_SIZE,
          sizeof(uint64_t));
      return 0;
    }
    return -1;
  }

  int close() { return 0; }

private:
  data_config data_cfg;
  splinterdb_config splinterdb_cfg;
  splinterdb *db;
  splinterdb_lookup_result db_result;
  char buffer[10 * MAX_VAL_SIZE];
  int quotient_remainder_bits;
};

#endif
