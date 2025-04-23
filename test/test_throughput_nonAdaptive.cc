#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <openssl/rand.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

extern "C" {
#include "include/hashutil.h"
#include "include/rand_util.h"
#include "include/splinter_util.h"
#include "include/test_driver.h"
}

test_results_t run_adversarial_test(
    size_t qbits,
    size_t rbits,
    uint64_t *insert_set,
    size_t insert_set_len,
    uint64_t *query_set,
    size_t query_set_len,
    uint64_t cache_size,
    uint64_t adv_freq,
    uint64_t adv_set_max_size,
    int verbose,
    char *queries_outfile) {
  test_results_t results;
  init_test_results(&results);

  size_t num_slots = 1ull << qbits;
  size_t minirun_id_bitmask = (1ull << (qbits + rbits)) - 1;

  data_config data_cfg = qf_data_config_init();
  splinterdb_config splinterdb_cfg = qf_splinterdb_config_init("db", &data_cfg);
  splinterdb_cfg.cache_size = cache_size * Mega;
  remove(splinterdb_cfg.filename);
  splinterdb *db;
  if (splinterdb_create(&splinterdb_cfg, &db)) {
    results.exit_code = -1;
    return results;
  }
  splinterdb_lookup_result db_result;
  splinterdb_lookup_result_init(db, &db_result, 0, NULL);

  data_config bm_data_cfg = qf_data_config_init();
  splinterdb_config backing_cfg = qf_splinterdb_config_init("bm", &bm_data_cfg);
  remove(backing_cfg.filename);
  splinterdb *bm;
  if (splinterdb_create(&backing_cfg, &bm)) {
    results.exit_code = -1;
    return results;
  }
  splinterdb_lookup_result bm_result;
  splinterdb_lookup_result_init(bm, &bm_result, 0, NULL);

  QF qf;
  if (!qf_malloc(&qf, num_slots, qbits + rbits, 0, QF_HASH_INVERTIBLE, 0)) {
    results.exit_code = -1;
    return results;
  }

  double target_load = 0.9f;
  size_t max_inserts = num_slots * target_load;
  size_t num_inserts = insert_set_len > max_inserts ? max_inserts : insert_set_len;
  size_t i;

  size_t measure_freq = 100, curr_interval = 0;
  size_t measure_point = num_inserts * (curr_interval + 1) / measure_freq, prev_point = 0;

  if (verbose)
    fprintf(stderr, "Performing insertions... 0.00%%");
  clock_t start_clock = clock(), end_clock;
  struct timeval tv;
  gettimeofday(&tv, NULL);
  uint64_t start_time = tv.tv_sec * 1000000 + tv.tv_usec, end_time, interval_time = start_time;
  for (i = 0; qf.metadata->noccupied_slots < num_inserts; i++) {
    int ret = qf_splinter_insert_split(&qf, db, bm, insert_set[i], i);
    if (ret == 1)
      continue;
    if (ret == 0)
      break;

    if (qf.metadata->noccupied_slots >= measure_point) {
      gettimeofday(&tv, NULL);
      if (verbose)
        fprintf(stderr, "\rPerforming insertions... %.2f%%", (double)(curr_interval + 1) / measure_freq * 100);

      curr_interval++;
      prev_point = i;
      measure_point = num_inserts * (curr_interval + 1) / measure_freq;

      gettimeofday(&tv, NULL);
      interval_time = tv.tv_sec * 1000000 + tv.tv_usec;
    }
  }
  gettimeofday(&tv, NULL);
  end_time = tv.tv_sec * 1000000 + tv.tv_usec;
  end_clock = clock();

  if (verbose)
    fprintf(stderr, "\rPerforming insertions... 100.00%%\n");

  if (verbose) {
    printf("Number of inserts:     %lu\n", i);
    printf("Time for inserts:      %f\n", (double)(end_time - start_time) / 1000000);
    printf("Insert throughput:     %f ops/sec\n", (double)i * 1000000 / (end_time - start_time));
    printf("CPU time for inserts:  %f\n", (double)(end_clock - start_clock) / CLOCKS_PER_SEC);
  }
  results.insert_throughput = (double)i * 1000000 / (end_time - start_time);

  size_t adv_set_size = 0, curr_adv_query = 0;
  uint64_t *adv_queries = (uint64_t *)malloc(adv_set_max_size * sizeof(uint64_t));

  double final_throughput = 0;

  curr_interval = 0;
  measure_point = query_set_len * (curr_interval + 1) / measure_freq;
  prev_point = 0;

  FILE *queries_file = queries_outfile ? fopen(queries_outfile, "w") : NULL;
  fprintf(queries_file, "queries through fprate\n");

  int still_have_space = 1;
  size_t full_point = num_slots * 0.95f;
  char buffer[10 * MAX_VAL_SIZE];
  uint64_t hash;
  int minirun_rank;
  uint64_t fp_count = 0;
  uint64_t adv_succ = 0, adv_fail = 0, tp_count = 0;

  if (verbose)
    fprintf(stderr, "Performing queries... 0.00%%");
  start_clock = clock();
  gettimeofday(&tv, NULL);
  start_time = interval_time = tv.tv_sec * 1000000 + tv.tv_usec;
  for (i = 0; i < query_set_len; i++) {
    if (i % adv_freq == 0 && adv_set_size > 0) {
      if (curr_adv_query >= adv_set_size)
        curr_adv_query = 0;
      if ((minirun_rank = qf_query_using_ll_table(&qf, adv_queries[curr_adv_query], &hash, QF_KEY_IS_HASH)) >= 0) {
        hash = (hash & minirun_id_bitmask) << (64 - qf.metadata->quotient_remainder_bits);
        slice db_query = padded_slice(&query_set[i], MAX_KEY_SIZE, sizeof(query_set[i]), buffer, 0);
        splinterdb_lookup(db, db_query, &db_result);
        if (!splinterdb_lookup_found(&db_result)) {
          fp_count++;
        } else
          tp_count++;
        adv_succ++;
        curr_adv_query++;
      } else {
        adv_fail++;
        adv_queries[curr_adv_query] = adv_queries[--adv_set_size];
      }
    } else {
      if ((minirun_rank = qf_query_using_ll_table(&qf, query_set[i], &hash, QF_KEY_IS_HASH)) >= 0) {
        hash = (hash & minirun_id_bitmask) << (64 - qf.metadata->quotient_remainder_bits);
        slice db_query = padded_slice(&query_set[i], MAX_KEY_SIZE, sizeof(query_set[i]), buffer, 0);
        splinterdb_lookup(db, db_query, &db_result);
        if (!splinterdb_lookup_found(&db_result)) {
          fp_count++;
        } else
          tp_count++;

        if (adv_set_size < adv_set_max_size) {
          adv_queries[adv_set_size++] = query_set[i];
        }
      }
    }

    if (i >= measure_point) {
      gettimeofday(&tv, NULL);

      final_throughput = (double)(i - prev_point) * 1000000 / (tv.tv_sec * 1000000 + tv.tv_usec - interval_time);
      if (queries_file)
        fprintf(queries_file, "%lu %f %f\n", i, final_throughput, (double)fp_count / i);
      if (verbose)
        fprintf(stderr, "\rPerforming queries... %.2f%%", (double)curr_interval / measure_freq * 100);

      curr_interval++;
      prev_point = i;
      measure_point = query_set_len * (curr_interval + 1) / measure_freq;

      gettimeofday(&tv, NULL);
      interval_time = tv.tv_sec * 1000000 + tv.tv_usec;
    }
  }
  gettimeofday(&tv, NULL);
  end_time = tv.tv_sec * 1000000 + tv.tv_usec;
  end_clock = clock();

  if (queries_file)
    fprintf(
        queries_file, "%lu %f %f\n", i, (double)(i - prev_point) * 1000000 / (end_time - interval_time),
        (double)fp_count / i);
  if (verbose)
    fprintf(stderr, "\rPerforming queries... 100.00%%\n");

  if (queries_file)
    fclose(queries_file);

  if (verbose) {
    printf("Time for queries:     %f s\n", (double)(end_time - start_time) / 1000000);
    printf("Query throughput:     %f ops/sec\n", (double)query_set_len * 1000000 / (end_time - start_time));
    printf("CPU time for queries: %f s\n", (double)(end_clock - start_clock) / CLOCKS_PER_SEC);

    printf("False positives:      %lu\n", fp_count);
    printf("False positive rate:  %f%%\n", 100. * fp_count / query_set_len);

    printf("Adversarial attacks:  %lu\n", adv_succ + adv_fail);
    printf("Successful attacks:   %lu\n", adv_succ);
    printf("Failed attacks:       %lu\n", adv_fail);
    printf("True positives:       %lu\n", tp_count);
  }
  results.query_throughput = (double)i * 1000000 / (end_time - start_time);
  results.false_positive_rate = (double)fp_count / query_set_len;
  results.final_query_throughput = final_throughput;

  free(adv_queries);
  splinterdb_close(&db);
  qf_free(&qf);
  return results;
}

test_results_t run_throughput_test(
    size_t qbits,
    size_t rbits,
    uint64_t *insert_set,
    size_t insert_set_len,
    uint64_t *query_set,
    size_t query_set_len,
    int total_rounds,
    int verbose,
    char *inserts_outfile,
    char *queries_outfile,
    char *round_outfile) {
  test_results_t results;
  init_test_results(&results);

  size_t num_slots = 1ull << qbits;
  size_t minirun_id_bitmask = (1ull << (qbits + rbits)) - 1;

  data_config db_data_cfg = qf_data_config_init();
  splinterdb_config splinterdb_cfg = qf_splinterdb_config_init("db", &db_data_cfg);
  remove(splinterdb_cfg.filename);
  splinterdb *db;
  if (splinterdb_create(&splinterdb_cfg, &db)) {
    results.exit_code = -1;
    return results;
  }
  splinterdb_lookup_result db_result;
  splinterdb_lookup_result_init(db, &db_result, 0, NULL);

  data_config bm_data_cfg = qf_data_config_init();
  splinterdb_config backing_cfg = qf_splinterdb_config_init("bm", &bm_data_cfg);
  remove(backing_cfg.filename);
  splinterdb *bm;
  if (splinterdb_create(&backing_cfg, &bm)) {
    results.exit_code = -1;
    return results;
  }
  splinterdb_lookup_result bm_result;
  splinterdb_lookup_result_init(bm, &bm_result, 0, NULL);

  QF qf;
  if (!qf_malloc(&qf, num_slots, qbits + rbits, 0, QF_HASH_INVERTIBLE, 0)) {
    results.exit_code = -1;
    return results;
  }

  double target_load = 0.9f;
  size_t max_inserts = num_slots * target_load;
  size_t num_inserts = insert_set_len > max_inserts ? max_inserts : insert_set_len;
  size_t i;

  size_t measure_freq = 100, curr_interval = 0;
  size_t measure_point = num_inserts * (curr_interval + 1) / measure_freq, prev_point = 0;

  FILE *inserts_file = inserts_outfile ? fopen(inserts_outfile, "w") : NULL;
  if (inserts_file)
    fprintf(inserts_file, "fill through\n");
  FILE *rounds_file = round_outfile ? fopen(round_outfile, "w") : NULL;
  if (rounds_file)
    fprintf(rounds_file, "round thput cumulative_thput fp\n");

  if (verbose)
    fprintf(stderr, "Performing insertions... 0.00%%");
  uint64_t num_updates = 0;
  clock_t start_clock = clock(), end_clock;
  struct timeval tv;
  gettimeofday(&tv, NULL);
  uint64_t start_time = tv.tv_sec * 1000000 + tv.tv_usec, end_time, interval_time = start_time;
  for (i = 0; qf.metadata->noccupied_slots < num_inserts; i++) {
    int ret = qf_splinter_insert_split(&qf, db, bm, insert_set[i], 1);
    // int ret = qf_splinter_insert(&qf, bm, insert_set[i], 1);
    if (ret == 1)
      continue;
    if (ret == 0)
      break;
    num_updates++;

    if (qf.metadata->noccupied_slots >= measure_point) {
      gettimeofday(&tv, NULL);
      if (inserts_file)
        fprintf(
            inserts_file, "%.2f %f\n", (double)qf.metadata->noccupied_slots / num_slots * 100,
            (double)(i - prev_point) * 1000000 / (tv.tv_sec * 1000000 + tv.tv_usec - interval_time));
      if (verbose)
        fprintf(stderr, "\rPerforming insertions... %.2f%%", (double)(curr_interval + 1) / measure_freq * 100);

      curr_interval++;
      prev_point = i;
      measure_point = num_inserts * (curr_interval + 1) / measure_freq;

      gettimeofday(&tv, NULL);
      interval_time = tv.tv_sec * 1000000 + tv.tv_usec;
    }
  }
  gettimeofday(&tv, NULL);
  end_time = tv.tv_sec * 1000000 + tv.tv_usec;
  end_clock = clock();

  if (inserts_file)
    fprintf(
        inserts_file, "%.2f %f\n", (double)qf.metadata->noccupied_slots / num_slots * 100,
        (double)(i - prev_point) * 1000000 / (end_time - interval_time));
  if (verbose)
    fprintf(stderr, "\rPerforming insertions... 100.00%%\n");

  if (verbose) {
    printf("Number of inserts:     %lu\n", i);
    printf("Number of updates:     %lu\n", num_updates);
    printf("Time for inserts:      %f\n", (double)(end_time - start_time) / 1000000);
    printf("Insert throughput:     %f ops/sec\n", (double)i * 1000000 / (end_time - start_time));
    printf("CPU time for inserts:  %f\n", (double)(end_clock - start_clock) / CLOCKS_PER_SEC);
  }
  results.insert_throughput = (double)i * 1000000 / (end_time - start_time);

  curr_interval = 0;
  measure_point = query_set_len * (curr_interval + 1) / measure_freq;
  prev_point = 0;

  FILE *queries_file = queries_outfile ? fopen(queries_outfile, "w") : NULL;
  if (queries_file)
    fprintf(queries_file, "queries through fprate\n");

  size_t full_point = num_slots * 0.95f;
  char buffer[10 * MAX_VAL_SIZE];
  uint64_t fp_count = 0;
  uint64_t hash;
  int minirun_rank;

  if (verbose)
    fprintf(stderr, "Performing queries... 0.00%%");
  start_clock = clock();
  gettimeofday(&tv, NULL);
  start_time = interval_time = tv.tv_sec * 1000000 + tv.tv_usec;
  double round_thput[total_rounds];
  double round_cumulative_thput[total_rounds];
  for (int round = 0; round < total_rounds; round++) {
    uint64_t round_start_time = tv.tv_sec * 1000000 + tv.tv_usec;
    for (i = 0; i < query_set_len; i++) {
      if ((minirun_rank = qf_query_using_ll_table(&qf, query_set[i], &hash, QF_KEY_IS_HASH)) >= 0) {
        slice db_query = padded_slice(&query_set[i], MAX_KEY_SIZE, sizeof(query_set[i]), buffer, 0);
        splinterdb_lookup(db, db_query, &db_result);
        if (!splinterdb_lookup_found(&db_result)) {
          // if (true) {
          fp_count++;
        }
      }
    }
    gettimeofday(&tv, NULL);
    uint64_t round_end_time = tv.tv_sec * 1000000 + tv.tv_usec;
    round_thput[round] = (double)query_set_len * 1000000 / (round_end_time - round_start_time);
    round_cumulative_thput[round] = (double)query_set_len * (round + 1) * 1000000 / (round_end_time - start_time);
    fprintf(rounds_file, "%d %f %f %lu\n", round, round_thput[round], round_cumulative_thput[round], fp_count);
    fprintf(stderr, "Round %d total false positive:     %lu\n", round, fp_count);
    fprintf(stderr, "Round %d throughput:     %f ops/sec\n", round, round_thput[round]);
    fprintf(stderr, "Cumulative Round %d throughput:     %f ops/sec\n", round, round_cumulative_thput[round]);
  }
  gettimeofday(&tv, NULL);
  end_time = tv.tv_sec * 1000000 + tv.tv_usec;
  end_clock = clock();

  if (queries_file)
    fprintf(
        queries_file, "%lu %f %f\n", i, (double)(i - prev_point) * 1000000 / (end_time - interval_time),
        (double)fp_count / i);
  if (verbose)
    fprintf(stderr, "\rPerforming queries... 100.00%%\n");

  if (verbose) {
    printf("Time for queries:     %f s\n", (double)(end_time - start_time) / 1000000);
    printf("Query throughput:     %f ops/sec\n", (double)query_set_len * 1000000 / (end_time - start_time));
    printf("CPU time for queries: %f s\n", (double)(end_clock - start_clock) / CLOCKS_PER_SEC);

    printf("False positives:      %lu\n", fp_count);
    printf("False positive rate:  %f%%\n", 100. * fp_count / query_set_len);
    for (i = 0; i < total_rounds; i++) {
      printf("Round %d throughput:     %f ops/sec\n", i, round_thput[i]);
    }
    for (i = 0; i < total_rounds; i++) {
      printf("Cumulative %d throughput:     %f ops/sec\n", i, round_cumulative_thput[i]);
    }
  }
  results.query_throughput = (double)i * 1000000 / (end_time - start_time);
  results.false_positive_rate = (double)fp_count / query_set_len;

  splinterdb_close(&db);
  // splinterdb_close(&bm);
  qf_free(&qf);
  return results;
}

int main(int argc, char **argv) {
  if (argc < 5) {
    fprintf(
        stderr, "Please specify \nthe log of the number of slots in the QF [eg. "
                "20]\nthe number of remainder bits in the QF [eg. 9]\nthe number of "
                "queries [eg. 100000000]\nthe query distribution [u]niform, [z]ipfian");
    exit(1);
  }

  size_t qbits = atoi(argv[1]);
  size_t rbits = atoi(argv[2]);
  size_t cache_size = 128;
  char dist = argv[4][0];

  size_t num_inserts = (1ull << qbits) * 0.9f; // strtoull(argv[3], NULL, 10);
  size_t num_queries = strtoull(argv[3], NULL, 10);
  size_t adv_freq = (100 / 5); // 5% of queries
  fprintf(stderr, "%ld\n", adv_freq);

  test_results_t ret;

  uint64_t *insert_set = (uint64_t *)malloc(num_inserts * sizeof(uint64_t));
  RAND_bytes((unsigned char *)insert_set, num_inserts * sizeof(uint64_t));
  uint64_t *query_set = (uint64_t *)malloc(num_queries * sizeof(uint64_t));
  RAND_bytes((unsigned char *)query_set, num_queries * sizeof(uint64_t));
  uint64_t *random_set = (uint64_t *)malloc(num_queries * sizeof(uint64_t));
  RAND_bytes((unsigned char *)random_set, num_queries * sizeof(uint64_t));
  int total_rounds = 50;
  if (dist == 'u') {
    ret = run_throughput_test(
        qbits, rbits, insert_set, num_inserts, query_set, num_queries, total_rounds, 1, "unif_i.csv", "unif_q.csv",
        "rounds.csv");
  }
  if (dist == 't') { // Only true positives.
    size_t minirun_id_bitmask = (1ull << (qbits + rbits)) - 1;
    for (uint64_t i = 0; i < num_queries; i++) {
      // Force a true positive by querying for inserted keys.
      query_set[i] = (insert_set[(i % num_inserts)]);
    }
    ret = run_throughput_test(
        qbits, rbits, insert_set, num_inserts, query_set, num_queries, total_rounds, 1, "unif_i.csv", "unif_q.csv",
        "rounds.csv");
  }
  if (dist == 'f') {
    size_t minirun_id_bitmask = (1ull << (qbits + rbits)) - 1;
    for (uint64_t i = 0; i < num_queries; i++) {
      // Force a false positive by zeroing out the non-fingerprint bits.
      query_set[i] = (insert_set[(i % num_inserts)] & minirun_id_bitmask);
      // Add some random bits to the start.
      query_set[i] |= (random_set[i] << (qbits + rbits));
    }
    ret = run_throughput_test(
        qbits, rbits, insert_set, num_inserts, query_set, num_queries, total_rounds, 1, "unif_i.csv", "unif_q.csv",
        "rounds.csv");
  }
  if (dist == 'z') {
    for (uint64_t ii = 0; ii < num_queries; ii++) {
      query_set[ii] = (uint64_t)rand_zipfian(1.5f, 1000000ull, query_set[ii]);
    }
    ret = run_throughput_test(
        qbits, rbits, insert_set, num_inserts, query_set, num_queries, total_rounds, 1, "unif_i.csv", "unif_q.csv",
        "rounds.csv");
  }
  if (dist == 'a') {
    ret = run_adversarial_test(
        qbits, rbits, insert_set, num_inserts, query_set, num_queries, cache_size, adv_freq, 1000000, 1, "query.csv");
  }
  if (ret.exit_code) {
    printf("Test failed\n");
  }
  return 0;
}
