#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <openssl/rand.h>

#include "include/hashutil.h"
#include "include/rand_util.h"
#include "include/splinter_util.h"
#include "include/test_driver.h"

test_results_t run_throughput_test_nonAdaptive(size_t qbits, size_t rbits, uint64_t *insert_set, size_t insert_set_len, uint64_t *query_set, size_t query_set_len, int verbose, char *inserts_outfile, char *queries_outfile) {
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
	if (inserts_file) fprintf(inserts_file, "fill through\n");

	if (verbose) fprintf(stderr, "Performing insertions... 0.00%%");
	uint64_t num_updates = 0;
	clock_t start_clock = clock(), end_clock;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	uint64_t start_time = tv.tv_sec * 1000000 + tv.tv_usec, end_time, interval_time = start_time;
	for (i = 0; qf.metadata->noccupied_slots < num_inserts; i++) {
		int ret = qf_splinter_insert_split(&qf, db, bm, insert_set[i], i);
		//int ret = qf_splinter_insert(&qf, bm, insert_set[i], 1);
		if (ret == 1) continue;
		if (ret == 0) break;
		num_updates++;

		if (qf.metadata->noccupied_slots >= measure_point) {
			gettimeofday(&tv, NULL);
			if (inserts_file) fprintf(inserts_file, "%.2f %f\n", (double)qf.metadata->noccupied_slots / num_slots * 100, (double)(i - prev_point) * 1000000 / (tv.tv_sec * 1000000 + tv.tv_usec - interval_time));
			if (verbose) fprintf(stderr, "\rPerforming insertions... %.2f%%", (double)(curr_interval + 1) / measure_freq * 100);

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

	if (inserts_file) fprintf(inserts_file, "%.2f %f\n", (double)qf.metadata->noccupied_slots / num_slots * 100, (double)(i - prev_point) * 1000000 / (end_time - interval_time));
	if (verbose) fprintf(stderr, "\rPerforming insertions... 100.00%%\n");

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
	if (queries_file) fprintf(queries_file, "queries through fprate\n");

	size_t full_point = num_slots * 0.95f;
	char buffer[10 * MAX_VAL_SIZE];
	uint64_t fp_count = 0;
	uint64_t hash;
	int minirun_rank;

	if (verbose) fprintf(stderr, "Performing queries... 0.00%%");
	start_clock = clock();
	gettimeofday(&tv, NULL);
	start_time = interval_time = tv.tv_sec * 1000000 + tv.tv_usec;
	for (i = 0; i < query_set_len; i++) {
		if ((minirun_rank = qf_query_using_ll_table(&qf, query_set[i], &hash, QF_KEY_IS_HASH)) >= 0) {
			slice db_query = padded_slice(&query_set[i], MAX_KEY_SIZE, sizeof(query_set[i]), buffer, 0);
			splinterdb_lookup(db, db_query, &db_result);
			if (!splinterdb_lookup_found(&db_result)) {
			//if (true) {
				fp_count++;
			}
		}

		if (i >= measure_point) {
			gettimeofday(&tv, NULL);

			if (queries_file) fprintf(queries_file, "%lu %f %f\n", i, (double)(i - prev_point) * 1000000 / (tv.tv_sec * 1000000 + tv.tv_usec - interval_time), (double)fp_count / i);
			if (verbose) fprintf(stderr, "\rPerforming queries... %.2f%%", (double)(curr_interval + 1) / measure_freq * 100);

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

	if (queries_file) fprintf(queries_file, "%lu %f %f\n", i, (double)(i - prev_point) * 1000000 / (end_time - interval_time), (double)fp_count / i);
	if (verbose) fprintf(stderr, "\rPerforming queries... 100.00%%\n");

	if (verbose) {
		printf("Time for queries:     %f s\n", (double)(end_time - start_time) / 1000000);
		printf("Query throughput:     %f ops/sec\n", (double)query_set_len * 1000000 / (end_time - start_time));
		printf("CPU time for queries: %f s\n", (double)(end_clock - start_clock) / CLOCKS_PER_SEC);

		printf("False positives:      %lu\n", fp_count);
		printf("False positive rate:  %f%%\n", 100. * fp_count / query_set_len);
	}
	results.query_throughput = (double)i * 1000000 / (end_time - start_time);
	results.false_positive_rate = (double)fp_count / query_set_len;
	
	splinterdb_close(&db);
	//splinterdb_close(&bm);
	qf_free(&qf);
	return results;
}



int main(int argc, char **argv)
{
	if (argc < 5) {
		fprintf(stderr, "Please specify \nthe log of the number of slots in the QF [eg. 20]\nthe number of remainder bits in the QF [eg. 9]\nthe number of queries [eg. 100000000]\nthe query distribution [u]niform, [z]ipfian");
		exit(1);
	}

	size_t qbits = atoi(argv[1]);
	size_t rbits = atoi(argv[2]);
	char dist = argv[4][0];

	size_t num_inserts = (1ull << qbits) * 0.9f;//strtoull(argv[3], NULL, 10);
	size_t num_queries = strtoull(argv[3], NULL, 10);

	test_results_t ret;

	uint64_t *insert_set = (uint64_t *)malloc(num_inserts * sizeof(uint64_t));
	RAND_bytes((unsigned char*)insert_set, num_inserts * sizeof(uint64_t));
	uint64_t *query_set = (uint64_t*) malloc(num_queries * sizeof(uint64_t));
	RAND_bytes((unsigned char*)query_set, num_queries * sizeof(uint64_t));

	if (dist == 'z') {
		for (uint64_t ii = 0; ii < num_queries; ii++) {
			query_set[ii] = (uint64_t)rand_zipfian(1.5f, 1000000ull, query_set[ii]);
		}
	}
	ret = run_throughput_test_nonAdaptive(qbits, rbits, insert_set, num_inserts, query_set, num_queries, 1, "unif_i.csv", "unif_q.csv");
	if (ret.exit_code) {
		printf("Test failed\n");
	}
	return 0;
}
