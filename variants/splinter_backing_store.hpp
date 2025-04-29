#ifndef SPLINTER_BACKING_STORE_H
#define SPLINTER_BACKING_STORE_H

extern "C" {
#include "include/splinter_util.h"
#include "include/test_driver.h"
}

class SplinterDBBackingStore {
public:
  int init(char *dbName) {
    data_cfg = qf_data_config_init();
    splinterdb_cfg = qf_splinterdb_config_init(dbName, &data_cfg);
    remove(splinterdb_cfg.filename);
    if (splinterdb_create(&splinterdb_cfg, &db)) {
      return -1;
    }
    splinterdb_lookup_result_init(db, &db_result, 0, NULL);
    return 0;
  }

  int insertKV(uint64_t key, uint64_t value, int isUpdate) {
    if (!db_insert(db, &key, sizeof(key), &value, sizeof(value), isUpdate, 0)) {
      uint64_t queryKey = key;
      slice db_query =
          padded_slice(&queryKey, MAX_KEY_SIZE, sizeof(key), buffer, 0);
      splinterdb_lookup(db, db_query, &db_result);
      slice result_val;
      splinterdb_lookup_result_value(&db_result, &result_val);
      // TODO(chesetti): Check if result at rank exists.
      if (!splinterdb_lookup_found(&db_result)) {
        return -1;
      }
      return 0;
    } else
      return -1;
  }

  int getKeyAtRank(uint64_t key, int rank, int quotient_remainder_bits, uint64_t *value) {
    uint64_t queryKey = key;
    slice db_query =
        padded_slice(&queryKey, MAX_KEY_SIZE, sizeof(key), buffer, 0);
    splinterdb_lookup(db, db_query, &db_result);
    slice result_val;
    splinterdb_lookup_result_value(&db_result, &result_val);
    // TODO(chesetti): Check if result at rank exists.
    if (!splinterdb_lookup_found(&db_result)) {
      return -1;
    }
    memcpy(
        value, slice_data(result_val) + rank * MAX_KEY_SIZE, sizeof(uint64_t));
    return 0;
  }

  int keyExists(uint64_t key) {
    slice db_query = padded_slice(&key, MAX_KEY_SIZE, sizeof(key), buffer, 0);
    splinterdb_lookup(db, db_query, &db_result);
    return splinterdb_lookup_found(&db_result);
  }

private:
  data_config data_cfg;
  splinterdb_config splinterdb_cfg;
  splinterdb *db;
  splinterdb_lookup_result db_result;
  char buffer[10 * MAX_VAL_SIZE];
};

#endif