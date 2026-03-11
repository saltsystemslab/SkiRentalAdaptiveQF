#ifndef WT_REVERSE_MAP_LSM_H
#define WT_REVERSE_MAP_LSM_H

#include "wiredtiger.h"
#include <thread>
#include <chrono>

class WiredTigerReverseMapLsm {
public:
  int init(std::string dbName, int quotient_remainder_bits, int cache_size_mb, int collectStats, bool clearOld, uint64_t delayUs) {
    this->queryDelayUs = delayUs;
    dbName = dbName + "_wiredTiger";
    if (std::filesystem::exists(dbName) && clearOld)
      std::filesystem::remove_all(dbName);
    std::filesystem::create_directory(dbName);

    char table_schema[max_schema_len];
    char connection_config[max_conn_config_len];
    sprintf(table_schema, "type=lsm,key_format=Q,value_format=Q");
    if (collectStats) {
      sprintf(
        connection_config,
        "create,statistics=(all),direct_io=[data],cache_size=%dMB,statistics_log=(wait=30,json=true,on_close=true)",
        cache_size_mb);
    } else {
      sprintf(
        connection_config,
        "create,direct_io=[data],cache_size=%dMB",
        cache_size_mb);
    }

    printf("ConnectionConfig: %s\n", connection_config);
    printf("TableSchema: %s\n", table_schema);
    error_check(
        wiredtiger_open(dbName.c_str(), NULL, connection_config, &conn));
    error_check(conn->open_session(conn, NULL, NULL, &session));
    error_check(session->create(session, "table:bm", table_schema));
    error_check(session->open_cursor(session, "table:bm", NULL, NULL, &cursor));

    printf("DB created!\n");
    this->quotient_remainder_bits = quotient_remainder_bits;
    return 0;
  }

  int insertKV(uint64_t key, uint64_t value, int isUpdate) {
    abort();
    return 0;
  }

  int searchKV(uint64_t key) {
    abort();
    return 1;
  }

  int insertFingerprint(uint64_t fingerprint, int rank, uint64_t key) {
    uint64_t minirunBitmask = (1ULL << quotient_remainder_bits) - 1;
    fingerprint = (fingerprint & minirunBitmask)
                  << (64 - quotient_remainder_bits);
    fingerprint = fingerprint + rank;
    fingerprints.push_back(std::pair<uint64_t, uint64_t>(fingerprint, key));
    return 0;
  }

  int insertAndCommitFingerprint(uint64_t fingerprint, int rank, uint64_t key) {
    uint64_t minirunBitmask = (1ULL << quotient_remainder_bits) - 1;
    fingerprint = (fingerprint & minirunBitmask)
                  << (64 - quotient_remainder_bits);

    fingerprint = fingerprint + rank;
    cursor->set_key(cursor, fingerprint);
    cursor->set_value(cursor, key);
    error_check(cursor->insert(cursor));

    return 0;
  }

  void commitFingerprints() {
    sort(fingerprints.begin(), fingerprints.end());
    for (uint64_t i=0; i < fingerprints.size(); i++) {
      cursor->reset(cursor);
      cursor->set_key(cursor, fingerprints[i].first);
      cursor->set_value(cursor, fingerprints[i].second);
      error_check(cursor->insert(cursor));
    }
  }

  int getFingerprint(uint64_t fingerprint, int rank, uint64_t *value) {
		if (queryDelayUs) {
				std::this_thread::sleep_for(std::chrono::microseconds(queryDelayUs)); 
		}
    uint64_t minirunBitmask = (1ULL << quotient_remainder_bits) - 1;
    fingerprint = (fingerprint & minirunBitmask)
                  << (64 - quotient_remainder_bits);
    fingerprint = fingerprint + rank;
    cursor->set_key(cursor, fingerprint);
    if (cursor->search(cursor) == WT_NOTFOUND) {
      return -1;
    }
    error_check(cursor->get_value(cursor, value));
    return 0;
  }

  int close() {
    error_check(conn->close(conn, NULL)); /* Close all handles. */
    /*! [access example close] */
    return 0;
  }

private:
  std::vector<std::pair<uint64_t, uint64_t>> fingerprints;
  WT_CONNECTION *conn;
  WT_SESSION *session;
  WT_CURSOR *cursor;

  const int key_len = 8, val_len = 8;
  const uint32_t buffer_pool_size_mb = 512;
  const uint32_t max_schema_len = 128;
  const uint32_t max_conn_config_len = 128;
	uint64_t queryDelayUs;

  int quotient_remainder_bits;

  static inline void error_check(int ret) {
    if (ret != 0) {
      std::cerr << "WiredTiger Error: " << wiredtiger_strerror(ret)
                << std::endl;
      exit(ret);
    }
  }
};

#endif
