#include <chrono>
#include <iostream>
#include <stdio.h>
#include <algorithm>
#include <queue>
#include <random>

#include "cxxopts.hpp"
#include "qf_filter.hpp"

#if USE_CQF
#include "non_adaptive_filter.hpp"
#else
#include "dski_adaptive_filter.hpp"
#include "repeat_detect_adaptive.hpp"
#include "sample_detect_adaptive.hpp"
#include "mono_adaptive_filter.hpp"
#include "rski_adaptive_filter.hpp"
#include "coin_flip_adaptive.hpp"
#include "non_adaptive_filter.hpp"
#include "block_counter_adaptive.hpp"
#endif

#include "splinter_backing_store.hpp"
#include "dummy_backing_store.hpp"
#include "wiredtiger_backing_store.hpp"
#include "wiredtiger_reverse_map.hpp"

void printProgressBar(int current, int total, int barWidth = 50) {
    float progress = (float)current / total;
    int pos = barWidth * progress;
    
    std::cout << "[";
    for (int i = 0; i < barWidth; ++i) {
        if (i < pos) std::cout << "=";
        else if (i == pos) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] " << int(progress * 100.0) << "%\r";
    std::cout.flush();
}

void write_latencies_to_file(std::string output_file_name, std::vector<uint64_t> fp_miss_latencies, std::vector<uint64_t> adapt_latencies) {
  sort(fp_miss_latencies.begin(), fp_miss_latencies.end());
  sort(adapt_latencies.begin(), adapt_latencies.end());

  FILE *latency_file = fopen(output_file_name.c_str(), "w");
  fprintf(latency_file,"metric,numSamples,min,50p,99p,99.99p,max\n");

  {
  int p50 = fp_miss_latencies.size() / 2;
  int p99 = fp_miss_latencies.size() * 0.99;
  int p9999 = fp_miss_latencies.size() * 0.9999;
  int max = fp_miss_latencies.size()-1;
  fprintf(latency_file,"DbMiss,%lu,%lu,%lu,%lu,%lu,%lu\n", fp_miss_latencies.size(), fp_miss_latencies[0], fp_miss_latencies[p50], fp_miss_latencies[p99], fp_miss_latencies[p9999], fp_miss_latencies[max]);
  }

  {
  int p50 = adapt_latencies.size() / 2;
  int p99 = adapt_latencies.size() * 0.99;
  int p9999 = adapt_latencies.size() * 0.9999;
  int max = adapt_latencies.size()-1;
  fprintf(latency_file,"Adapt,%lu,%lu,%lu,%lu,%lu,%lu\n", adapt_latencies.size(), adapt_latencies[0], adapt_latencies[p50], adapt_latencies[p99], adapt_latencies[p9999], adapt_latencies[max]);
  }

  fclose(latency_file);
}

void write_microbench_to_file(std::string output_file_name, uint64_t numInserts, uint64_t tpTimeUs, uint64_t numQueries, uint64_t queryTimeUs, uint64_t filterInsertUs, uint64_t systemInsertUs, double load_factor, double fpr, uint64_t sizeBytes) {
  FILE *microbench_file = fopen(output_file_name.c_str(), "w");
  fprintf(microbench_file,"Load Factor:%lf\nFPR:%lf\n", load_factor, fpr);
  fprintf(microbench_file,"NumInserts: %lu\nFilterInsertTime(us): %lu\nFilterInsert Thput:%lf\n", numInserts, filterInsertUs, (1.0*numInserts)/filterInsertUs);
  fprintf(microbench_file,"SystemInsertTime(us): %lu\nSystemInsert Thput:%lf\n", systemInsertUs, (1.0*numInserts)/systemInsertUs);
  fprintf(microbench_file,"True Queries: %lu\nTrue Queries(us): %lu\nTrue Queries Thput: %lf\n", numInserts, tpTimeUs, (1.0*numQueries)/(tpTimeUs));
  fprintf(microbench_file,"Queries: %lu\nQueries(us): %lu\nQueries Thput: %lf\n", numQueries, queryTimeUs, (1.0*numQueries)/(queryTimeUs));
  fprintf(microbench_file,"Size(B): %lu Size(MB): %lf\n", sizeBytes, sizeBytes/(1024.0 * 1024.0));;
  fclose(microbench_file);
}

void write_fp_stats_to_file(std::string output_file_name, std::unordered_map<uint64_t, uint64_t> fp_freq) {
  FILE *fp_stats_file = fopen(output_file_name.c_str(), "w");
  fprintf(fp_stats_file,"freq count\n");

  std::map<uint64_t, uint64_t> freq_dist;
  for (const auto& it: fp_freq) {
    freq_dist[it.second]++;
  }

  for (const auto &it: freq_dist) {
    fprintf(fp_stats_file, "%lu %lu\n", it.first, it.second);
  }

  fclose(fp_stats_file);
}

template <typename DbStorageEngine, typename QFilter>
int run_benchmark(BenchmarkParams params) {
  QFilter qf;
  QFilterConfig qfConfig = params.qfConfig;
  uint64_t *insertSet = params.insertSet;
  uint64_t numInserts = params.numInserts;
  uint64_t *querySet = params.querySet;
  uint64_t numQueries = params.numQueries;
  uint64_t numRounds = params.numRounds;
  bool shouldSort = params.shouldSort;
  std::string output_file = params.output_file + ".csv";
  int is_adversarial = params.is_adversarial;
  int adversarial_freq = params.adversarial_freq;
  bool sortAndInsertFingerprints = params.sortAndInsertFingerprints;

  int isPhasedTest = params.isPhasedTest;
  bool startWithAdversarialPhase = params.startWithAdversarialPhase;
  int numPhases = params.numPhases;
  int num_rounds_per_phase = numRounds / numPhases;


#ifdef PERF
  double sampleRate = 0.9;
  uint64_t sampleThreshold = INT_MAX * sampleRate;
  std::vector<uint64_t> fp_miss_latencies;
  std::unordered_map<uint64_t, uint64_t> fp_freq;
  std::vector<uint64_t> adapt_latencies;
#endif

  std::cout << "Writing to " << output_file << std::endl;
  FILE *rounds_file = fopen(output_file.c_str(), "w");
  fprintf(
      rounds_file,
      "round round_thput round_fp round_tp cumulative_thput cumulative_fp cumulative_adversarial_queries "
      "round_adapts cumulative_adapts load_factor\n");

  int ret = 0;
  ret = qf.construct(params);
  if (ret < 0) {
    abort();
  }
  fprintf(stderr, "Sorting keys\n");
  if (shouldSort) std::sort(insertSet, insertSet + numInserts);
  fprintf(stderr, "Beginning bulkLoad\n");
  auto filter_insert_start = std::chrono::high_resolution_clock::now();
  auto system_insert_start = std::chrono::high_resolution_clock::now();
  ret = qf.bulkLoad(insertSet, numInserts );
  auto filter_insert_end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::micro> filter_insert_duration =
        filter_insert_end - filter_insert_start;

  if (ret < 0) {
    abort();
  }
  DbStorageEngine db;
  fprintf(stderr, "Loading database\n");
  db.init("database", qfConfig.qbits + qfConfig.rbits, params.storageCacheSizeMB, false /*collectStats*/, true /* clear OldDB*/);
  for (uint64_t i = 0; i < numInserts; i++) {
    if (i % 1000000 == 0)printProgressBar(i, numInserts);
    db.insertKV(insertSet[i], insertSet[i], 0);
  }
  db.close();
  auto system_insert_end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::micro> system_insert_duration =
        system_insert_end - system_insert_start;

  fprintf(stderr, "Done. starting test\n");
  db.init("database", qfConfig.qbits + qfConfig.rbits, params.storageCacheSizeMB, params.shouldCollectDbStats, false /* clear OldDB*/);

  QFilterQueryResult qfFilterQueryResult;
  uint64_t fpCount = 0;
  uint64_t adaptCount = 0;
  uint64_t numQueriesPerRound = numQueries / numRounds;
  uint64_t total_adversarial_queries_count = 0;
	uint64_t *adv_queries = (uint64_t *)malloc(numQueries * sizeof(uint64_t));
  uint64_t cur_adv_query = 0;
  uint64_t adv_query_size = 0;

  if (isPhasedTest && startWithAdversarialPhase) {
    is_adversarial = 0; // Round 0 will switch it to adversarial
  } else if (isPhasedTest) {
    is_adversarial = 1;
  }

  auto bench_start = std::chrono::high_resolution_clock::now();
  for (uint32_t r = 0; r < numRounds; r++) {
    if (isPhasedTest) {
      if (r % num_rounds_per_phase == 0) {
        is_adversarial = 1 - is_adversarial;
        printf("Round %d is_adversarial: %d\n", r, is_adversarial);
      }
    }

    auto round_start = std::chrono::high_resolution_clock::now();
    uint64_t roundFpCount = 0;
    uint64_t roundAdaptCount = 0;
    uint64_t roundTruePositive = 0;
    for (uint64_t i = 0; i < numQueriesPerRound; i++) {
      uint64_t queryIdx = r * numQueriesPerRound + i;
      uint64_t queryKey = querySet[queryIdx];

      if (is_adversarial && queryIdx % adversarial_freq == 0 && adv_query_size > 0) {
        total_adversarial_queries_count++;
        if (cur_adv_query == adv_query_size)cur_adv_query=0;
        queryKey = adv_queries[cur_adv_query];
        cur_adv_query++;
      }

#ifdef PERF
      std::chrono::time_point<std::chrono::high_resolution_clock> dbQueryStart, dbQueryEnd, adaptStart, adaptEnd;
#endif
      qf.queryFilter(queryKey, &qfFilterQueryResult);
      if (qfFilterQueryResult.key_present) {
        if ((is_adversarial || isPhasedTest) && queryIdx % adversarial_freq != 0) {
          adv_queries[adv_query_size] = queryKey;
          adv_query_size++;
        }
#ifdef PERF
        fp_freq[queryKey]++;
        bool sampleQuery = rand() < sampleThreshold;
        if (sampleQuery && r==0) {
          dbQueryStart = std::chrono::high_resolution_clock::now();
        }
#endif
        int dbQueryResult = db.searchKV(queryKey);
#ifdef PERF
        if (sampleQuery && r==0) {
          dbQueryEnd = std::chrono::high_resolution_clock::now();
          uint64_t timeElapsed = (dbQueryEnd - dbQueryStart).count();
          fp_miss_latencies.push_back(timeElapsed);
        }

#endif
        if (!dbQueryResult) {
          fpCount++;
          roundFpCount++;
#ifdef PERF
          if (sampleQuery && r==0) {
            adaptStart = std::chrono::high_resolution_clock::now();
          }
#endif
          int adaptRetCode = qf.adapt(queryKey, &qfFilterQueryResult);
#ifdef PERF
          if (sampleQuery && r==0) {
            adaptEnd = std::chrono::high_resolution_clock::now();
            adapt_latencies.push_back((adaptEnd - adaptStart).count());
          }
#endif
          if (adaptRetCode == -1) {
            continue;
          } else {
            roundAdaptCount += adaptRetCode;
            adaptCount += adaptRetCode;
          }
        } else {
          roundTruePositive++;
        }
      }
    }
    auto round_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::micro> round_duration =
        round_end - round_start;
    double roundThroughput = ((double)numQueriesPerRound) / round_duration.count();
    std::chrono::duration<double, std::micro> overall_duration =
        round_end - bench_start;
    double cumulativeThroughput =
        ((double)(r + 1) * (double)numQueriesPerRound) / overall_duration.count();
    fprintf(
        rounds_file,
        "%d %f %lu %lu %f %lu %lu %lu %lu %f\n",
        r,
        roundThroughput,
        roundFpCount,
        roundTruePositive,
        cumulativeThroughput,
        fpCount,
        total_adversarial_queries_count,
        roundAdaptCount,
        adaptCount,
        qf.loadFactor());
    fprintf(
        stdout,
        "%d %f %lu %lu %f %lu %lu %lu %lu %f\n",
        r,
        roundThroughput,
        roundFpCount,
        roundTruePositive,
        cumulativeThroughput,
        fpCount,
        total_adversarial_queries_count,
        roundAdaptCount,
        adaptCount,
        qf.loadFactor());
  }
#ifdef PERF
  std::string latency_file_name = params.output_file + "_latency.csv";
  write_latencies_to_file(latency_file_name.c_str(), fp_miss_latencies, adapt_latencies);

  std::string fp_stats_file = params.output_file + "_fp_stats.csv";
  write_fp_stats_to_file(fp_stats_file.c_str(), fp_freq);
#endif
  db.close();
  printf("Done with the test\n");

// Run Microbenchmarks with only in-memory operations at end of the load test
{
// True queries: Query the insert set.
  std::random_device rd;
  std::mt19937 g(rd());
  std::shuffle(insertSet, insertSet + numInserts, g);
  uint64_t numTp = 0;
  auto tp_query_start = std::chrono::high_resolution_clock::now();
  for (uint64_t i=0; i < numInserts; i++) {
      if (i%1000000==0)printProgressBar(i, numInserts);
      qf.queryFilter(insertSet[i], &qfFilterQueryResult);
      if (qfFilterQueryResult.key_present) numTp++;
    #ifdef CORRECTNESS
      else {
        printf("Key[%lu] not found: %lu\n", i, insertSet[i]);
        abort(); // Should not happen.... This means filter returned false negative.
      }
    #endif
  }
  auto tp_query_end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::micro> tp_overall_duration =
        tp_query_end - tp_query_start;

  #if 0
// Negative queries: Query false positives again, but only measure the throughput.
  uint64_t numFp=0;
  auto fp_query_start = std::chrono::high_resolution_clock::now();
  for (uint64_t i=0; i < numQueries; i++) {
      qf.queryFilter(querySet[i], &qfFilterQueryResult);
      if (qfFilterQueryResult.key_present) numFp++;
  }
  auto fp_query_end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::micro> fp_overall_duration =
        fp_query_end - fp_query_start;
  #endif
  }

  {
  // Force the filter to adapt to get it to 95% load factor.
  #if 0
  auto fp_query_start = std::chrono::high_resolution_clock::now();
  for (uint64_t i=0; i < numQueries; i++) {
      qf.queryFilter(querySet[i], &qfFilterQueryResult);
      if (qfFilterQueryResult.key_present) {
        qf.adapt(querySet[i], &qfFilterQueryResult);
      }
  }
  #endif

// True queries: Query the insert set.
  auto tp_query_start = std::chrono::high_resolution_clock::now();
  for (uint64_t i=0; i < numInserts; i++) {
      if (i%1000000==0)printProgressBar(i, numInserts);
      qf.queryFilter(insertSet[i], &qfFilterQueryResult);
      if (qfFilterQueryResult.key_present); 
    #ifdef CORRECTNESS
      else {
        printf("Key[%lu] not found: %lu\n", i, insertSet[i]);
        abort(); // Should not happen.... This means filter returned false negative.
      }
    #endif
  }
  auto tp_query_end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::micro> tp_overall_duration =
        tp_query_end - tp_query_start;

// Negative queries: Query false positives again, but only measure the throughput.
  uint64_t numFp=0;
  auto fp95_query_start = std::chrono::high_resolution_clock::now();
  for (uint64_t i=0; i < numQueries; i++) {
      qf.queryFilter(querySet[i], &qfFilterQueryResult);
      if (qfFilterQueryResult.key_present) numFp++;
  }
  double overall_fpr = (1.0*fpCount) / numQueries;
  auto fp95_query_end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::micro> fp_overall_duration =
        fp95_query_end - fp95_query_start;
  std::string micro_file_name = params.output_file + "_summary.csv";
  write_microbench_to_file(micro_file_name.c_str(), numInserts, tp_overall_duration.count(), numQueries, fp_overall_duration.count(), filter_insert_duration.count(), system_insert_duration.count(), qf.loadFactor(), overall_fpr, qf.sizeInBytes());
  }
  qf.close();
  return 0;
}

template <typename DbStorageEngine, typename ReverseMapEngine>
int run_benchmark_with_storage_engine(
    std::string filterType, BenchmarkParams params) {
  int ret = -1;
  #if USE_CQF
  if (filterType == "nonAdaptive") {
    ret = run_benchmark<DbStorageEngine, NonAdaptiveFilter>(params);
  }
  #else
  if (filterType == "nonAdaptive") {
    ret = run_benchmark<DbStorageEngine, NonAdaptiveFilter>(params);
  }
  if (filterType == "adaptive") {
    ret = run_benchmark<
        DbStorageEngine,
        MonotonicAdaptiveFilter<ReverseMapEngine>>(params);
  }
  if (filterType == "dSkiAdaptive") {
    ret = run_benchmark<DbStorageEngine, DSkiAdaptiveFilter<ReverseMapEngine>>(
        params);
  }
  if (filterType == "rSkiAdaptive") {
    ret = run_benchmark<DbStorageEngine, RSkiAdaptiveFilter<ReverseMapEngine>>(
        params);
  }
  if (filterType == "coinFlip") {
    ret = run_benchmark<DbStorageEngine, CoinFlipAdaptiveFilter<ReverseMapEngine>>(
        params);
  }
  if (filterType == "blockCount") {
    ret = run_benchmark<DbStorageEngine, BlockCounterAdaptiveFilter<ReverseMapEngine>>(
        params);
  }
  if (filterType == "repeatDetect") {
    ret = run_benchmark<DbStorageEngine, RepeatDetectAdaptiveFilter<ReverseMapEngine>>(
        params);
  }
  if (filterType == "sampleDetect") {
    ret = run_benchmark<DbStorageEngine, SampleDetectAdaptiveFilter<ReverseMapEngine>>(
        params);
  }
  #endif
  return ret;
}

int main(int argc, char **argv) {
  cxxopts::Options options("Bench adaptive filter variants");

  options.add_options()(
      "seed",
      "random seed",
      cxxopts::value<int>()->default_value("0"))(

      "q,quotient",
      "Quotient bits to use in filter",
      cxxopts::value<int>()->default_value("22"))(

      "r,remainder",
      "Remainder bits to use in filter",
      cxxopts::value<int>()->default_value("8"))(

      "k,breakEven",
      "Break Even Ratio",
      cxxopts::value<int>()->default_value("5"))(

      "queryWorkload",
      "uniform, false-positive, zipfian, adversarial,cyclic",
      cxxopts::value<std::string>()->default_value("false-positive"))(

      "filter",
      "nonAdaptive, MonoAdaptive, DSki, RSki",
      cxxopts::value<std::string>()->default_value("nonAdaptive"))(

      "storageEngine",
      "splinterDB, wiredTiger",
      cxxopts::value<std::string>()->default_value("splinterDB"))(

      "reverseMapEngine",
      "splinterDB, wiredTiger",
      cxxopts::value<std::string>()->default_value("splinterDB"))(

      "storageCacheSizeMB",
      "Size of database cache size in MB",
      cxxopts::value<uint64_t>()->default_value("64"))(

      "reverseMapCacheSizeMB",
      "Size of reverse map cache size in MB",
      cxxopts::value<uint64_t>()->default_value("64"))(

      "numQueries",
      "Number of total queries ",
      cxxopts::value<uint64_t>()->default_value("20000"))(

      "numRounds",
      "Number of rounds",
      cxxopts::value<int>()->default_value("100"))(

      "microBench",
      "Use a mock DB and main-memory operations",
      cxxopts::value<bool>()->default_value("false"))(

      "sortAndInsertFingerprints",
      "Sort all fingerprints before inserting into reverse map, speeds up wiredTiger as reverse map creation",
      cxxopts::value<bool>()->default_value("true"))(

      "dbStats",
      "Collect DB Stats (WiredTiger)",
      cxxopts::value<bool>()->default_value("false"))(

      "advFreq",
      "adversarialFreq",
      cxxopts::value<int>()->default_value("5"))(

      "phasedTest",
      "Run phased test that alternates between adversarial and workload mode",
      cxxopts::value<bool>()->default_value("false"))(

      "startWithAdversarialPhase",
      "Starts with Adversarial phase in phased test",
      cxxopts::value<bool>()->default_value("false"))(

      "numPhases",
      "Number of adversarial phase switches in Cyclic test",
      cxxopts::value<int>()->default_value("1"));


  auto result = options.parse(argc, argv);
  int seed = result["seed"].as<int>();
  int qbits = result["q"].as<int>();
  int rbits = result["r"].as<int>();
  uint64_t numQueries = result["numQueries"].as<uint64_t>();
  uint64_t storageCacheSizeMB = result["storageCacheSizeMB"].as<uint64_t>();
  uint64_t reverseMapCacheSizeMB = result["reverseMapCacheSizeMB"].as<uint64_t>();
  int numRounds = result["numRounds"].as<int>();
  int advFreq = result["advFreq"].as<int>();
  int breakEven = result["breakEven"].as<int>();
  std::string queryWorkload = result["queryWorkload"].as<std::string>();
  std::string filterType = result["filter"].as<std::string>();
  std::string storageEngine = result["storageEngine"].as<std::string>();
  std::string reverseMapEngine = result["reverseMapEngine"].as<std::string>();
  bool microBench = result["microBench"].as<bool>();
  bool sortAndInsertFingerprints = result["sortAndInsertFingerprints"].as<bool>();
  bool shouldSort = !microBench;
  bool shouldCollectDbStats = result["dbStats"].as<bool>();
  bool isPhasedTest = result["phasedTest"].as<bool>();
  bool startWithAdversarialPhase = result["startWithAdversarialPhase"].as<bool>();
  int numPhases = result["numPhases"].as<int>();
  size_t numInserts = (1ull << qbits) * 0.9f; // strtoull(argv[3], NULL, 10);
  uint64_t *insertSet; 
  uint64_t *querySet;


  std::cout << "Testing filter: " << filterType
            << " with workload: " << queryWorkload 
            << " startWithAdversarialPhase " << startWithAdversarialPhase
            << " sortAndInsertFingerprints " << sortAndInsertFingerprints
            << std::endl;

  uint64_t minirun_bitmask = (1ULL << qbits + rbits) - 1;
  QFilterConfig qfConfig;
  qfConfig.qbits = qbits;
  qfConfig.rbits = rbits;
  qfConfig.max_load_factor = 0.95;
  qfConfig.breakEvenCount = breakEven;

  {
    int fd = open("insertSet", O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
      perror("Failed to open insert set file");
    }
    uint64_t numKeys;
    pread(fd, &numKeys, sizeof(uint64_t), 0);
    fprintf(stdout, "numKeys in insertSet %lu\n", numKeys);
    numInserts = numKeys;
    insertSet = (uint64_t *)malloc(numInserts * sizeof(uint64_t));
    char *buf = (char *)insertSet;
    off_t offset = 0;
    while (offset < numKeys * sizeof(uint64_t)) {
      ssize_t bytes_read = pread(fd, insertSet + offset, numKeys * sizeof(uint64_t), offset + sizeof(uint64_t));
      offset += bytes_read;
      fprintf(stdout, "%lu\n", offset);
    }
    close(fd);
  }
  {
    int fd = open("querySet", O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
      perror("Failed to open query set file");
    }
    uint64_t numKeys;
    pread(fd, &numKeys, sizeof(uint64_t), 0);
    fprintf(stdout, "numKeys in querySet %lu\n", numKeys);
    numQueries = numKeys;
    querySet = (uint64_t *)malloc(numQueries * sizeof(uint64_t));

    char *buf = (char *)querySet;
    off_t offset = 0;
    while (offset < numKeys * sizeof(uint64_t)) {
      ssize_t bytes_read = pread(fd, buf+ offset, numKeys * sizeof(uint64_t), offset + sizeof(uint64_t));
      offset += bytes_read;
      fprintf(stdout, "%lu\n", offset);
    }
    close(fd);
  }

  BenchmarkParams params;
  params.qfConfig = qfConfig;
  params.insertSet = insertSet;
  params.numInserts = numInserts;
  params.querySet = querySet;
  params.numQueries = numQueries;
  params.numRounds = numRounds;
  params.output_file = filterType;
  params.is_adversarial = 0;
  params.adversarial_freq = 100 / advFreq;
  params.shouldCollectDbStats = shouldCollectDbStats;
  params.storageCacheSizeMB = storageCacheSizeMB;
  params.reverseMapCacheSizeMB = reverseMapCacheSizeMB;
  params.shouldSort = shouldSort;
  params.isPhasedTest  = isPhasedTest;
  params.numPhases = numPhases;
  params.startWithAdversarialPhase = startWithAdversarialPhase;
  params.sortAndInsertFingerprints = sortAndInsertFingerprints;

  
  if (queryWorkload == "adversarial") {
    params.is_adversarial = 1;
  }
  // For cyclic workloads, the benchmark will handle dynamically changing is_adversarial.

  int ret = -1;
  if (microBench) {
    ret = run_benchmark_with_storage_engine<
        DummyDBBackingStore,
        DummyDBBackingStore>(filterType, params);
  } else if (
      storageEngine == "splinterDB" && reverseMapEngine == "wiredTiger") {
    ret = run_benchmark_with_storage_engine<
        SplinterDBBackingStore,
        WiredTigerReverseMap>(filterType, params);
  } else if (
      storageEngine == "splinterDB" && reverseMapEngine == "splinterDB") {
    ret = run_benchmark_with_storage_engine<
        SplinterDBBackingStore,
        SplinterDBBackingStore>(filterType, params);
  } else if (
      storageEngine == "wiredTiger" && reverseMapEngine == "wiredTiger") {
    ret = run_benchmark_with_storage_engine<
        WiredTigerBackingStore,
        WiredTigerReverseMap>(filterType, params);
  } else if (
      storageEngine == "wiredTiger" && reverseMapEngine == "splinterDB") {
    ret = run_benchmark_with_storage_engine<
        WiredTigerBackingStore,
        SplinterDBBackingStore>(filterType, params);
  }
  if (ret) {
    std::cout << "Test failed" << std::endl;
    return -1;
  }
  return 0;
}
