#include <chrono>
#include <iostream>
#include <stdio.h>
#include <algorithm>

#include "cxxopts.hpp"
#include "benchmark_workloads.hpp"
#include "benchmark_flags.hpp"

#if USE_CQF
#include "cqf.hpp"
#else
#include "skiqf.hpp"
#include "adaptiveqf.hpp"
#include "coin_flip_adaptive.hpp"
#include "cqf.hpp"
#include "hybrid_skiqf.hpp"
#include "randomized_skiqf.hpp"
#endif

#include "db/splinter_db.hpp"
#include "db/mock_db.hpp"
#include "db/wiredtiger_backing_store.hpp"
#include "db/wiredtiger_reverse_map.hpp"
#include "db/wiredtiger_reverse_map_lsm.hpp"

template <typename DbStorageEngine, typename QFilter>
int run_benchmark(BenchmarkParams params) {
  QFilter qf;
  QFilterConfig qfConfig = params.qfConfig;
  uint64_t *insertSet = params.insertSet;
  uint64_t numInserts = params.numInserts;
  bool shouldSort = params.shouldSort;
  std::string output_file = params.output_file + ".csv";
  uint64_t storageSleepUs = params.storageSleepUs;

  std::cout << "Writing to " << output_file << std::endl;
  FILE *rounds_file = fopen(output_file.c_str(), "w");
  if (rounds_file == nullptr) {
    perror("Failed to open benchmark output file");
    return -1;
  }
  fprintf(
      rounds_file,
      "round round_thput round_fp round_tp cumulative_thput cumulative_fp cumulative_adversarial_queries "
      "round_adapts cumulative_adapts load_factor\n");

  int ret = 0;
  ret = qf.construct(qfConfig);
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
  db.init("database", qfConfig.qbits + qfConfig.rbits, params.storageCacheSizeMB, false /*collectStats*/, true /* clear OldDB*/, 0 /* Sleep Delay */);
  for (uint64_t i = 0; i < numInserts; i++) {
    if (i % 1000000 == 0)printProgressBar(i, numInserts);
    db.insertKV(insertSet[i], insertSet[i], 0);
  }
  db.close();
  auto system_insert_end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::micro> system_insert_duration =
        system_insert_end - system_insert_start;

  fprintf(stderr, "Done. starting test\n");
  db.init("database", qfConfig.qbits + qfConfig.rbits, params.storageCacheSizeMB, params.shouldCollectDbStats, false /* clear OldDB*/, storageSleepUs);

  WorkloadStats stats;
  if (params.isAdaptiveAdversary) {
    try {
      stats = run_adaptive_adversarial_workload(qf, db, params, rounds_file);
    } catch (const std::exception& error) {
      fprintf(stderr, "%s\n", error.what());
      fclose(rounds_file);
      db.close();
      qf.close();
      return -1;
    }
  } else if (params.isPhasedTest) {
    stats = run_phased_adversarial_workload(qf, db, params, rounds_file);
  } else if (params.is_adversarial) {
    stats = run_adversarial_workload(qf, db, params, rounds_file);
  } else {
    stats = run_standard_workload(qf, db, params, rounds_file);
  }
  fclose(rounds_file);
  if (params.isAdaptiveAdversary) {
    write_adversary_events(params.output_file + "_adversary.csv", stats.adversaryEvents);
  }
#ifdef PERF
  std::string latency_file_name = params.output_file + "_latency.csv";
  write_latencies_to_file(latency_file_name.c_str(), stats.dbMissLatencies, stats.adaptLatencies);

  std::string fp_stats_file = params.output_file + "_fp_stats.csv";
  write_fp_stats_to_file(fp_stats_file.c_str(), stats.fpFrequency);
#endif
  printf("Done with the test\n");
  db.close();
  printf("Close DB\n");

  const auto microStats = run_microbenchmark_workload(qf, params);
  write_microbench_to_file(
      params.output_file + "_summary.csv", params.numInserts,
      microStats.trueQueryUs, params.numQueries, microStats.queryUs,
      filter_insert_duration.count(), system_insert_duration.count(),
      qf.loadFactor(), static_cast<double>(stats.falsePositives) / params.numQueries,
      qf.sizeInBytes());
  qf.close();
  return 0;
}

template <typename DbStorageEngine, typename ReverseMapEngine>
int run_benchmark_with_storage_engine(
    std::string filterType, BenchmarkParams params) {
  int ret = -1;
  #if USE_CQF
  if (filterType == "nonAdaptive") {
    ret = run_benchmark<DbStorageEngine, NonAdaptiveFilter<MockDB>>(params);
  }
  #else
  if (filterType == "nonAdaptive") {
    // The baseline has no reverse map. Its database receives that cache budget.
    ret = run_benchmark<DbStorageEngine, NonAdaptiveFilter<MockDB>>(params);
  }
  if (filterType == "adaptive") {
    ret = run_benchmark<
        DbStorageEngine,
        AdaptiveQF<ReverseMapEngine>>(params);
  }
  if (filterType == "skiQF") {
    ret = run_benchmark<DbStorageEngine, SkiQF<ReverseMapEngine>>(
        params);
  }
  if (filterType == "randomizedSkiQF") {
    ret = run_benchmark<DbStorageEngine, RandomizedSkiQF<ReverseMapEngine>>(
        params);
  }
  if (filterType == "coinFlip") {
    ret = run_benchmark<DbStorageEngine, CoinFlipAdaptiveFilter<ReverseMapEngine>>(
        params);
  }
  if (filterType == "hybridSkiQF") {
    ret = run_benchmark<DbStorageEngine, HybridSkiQF<ReverseMapEngine>>(
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
      cxxopts::value<uint64_t>()->default_value("0"))(

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
      "uniform, false-positive, zipfian, adversarial, adaptive-adversarial, cyclic",
      cxxopts::value<std::string>()->default_value("false-positive"))(

      "filter",
      "nonAdaptive, adaptive, skiQF, randomizedSkiQF, hybridSkiQF",
      cxxopts::value<std::string>()->default_value("nonAdaptive"))(

      "storageEngine",
      "splinterDB, wiredTiger",
      cxxopts::value<std::string>()->default_value("splinterDB"))(

      "reverseMapEngine",
      "splinterDB, wiredTiger,wiredTigerLsm",
      cxxopts::value<std::string>()->default_value("splinterDB"))(

      "storageCacheSizeMB",
      "Size of database cache size in MB",
      cxxopts::value<uint64_t>()->default_value("64"))(

      "reverseMapCacheSizeMB",
      "Size of reverse map cache size in MB",
      cxxopts::value<uint64_t>()->default_value("64"))(

      "storageSleepUs",
      "Artificial sleep in microseconds to add to storage DB calls",
      cxxopts::value<uint64_t>()->default_value("0"))(

      "reverseSleepUs",
      "Artificial sleep in microseconds to add to reverse map calls",
      cxxopts::value<uint64_t>()->default_value("0"))(

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

      "sortAndInsertKeys",
      "Sort Keys before inserting into reverse map, speeds up wiredTiger inserts in DB",
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
      cxxopts::value<int>()->default_value("1"))(

      "hybridWindowSize",
      "HybridSkiQF detector window in negative-key queries",
      cxxopts::value<uint64_t>()->default_value("1000000"));


  auto result = options.parse(argc, argv);
  BenchmarkParams params;
  std::vector<uint64_t> insertKeys;
  std::vector<uint64_t> queryKeys;
  try {
    params = buildBenchmarkParams(result);
    insertKeys = read_key_set("insertSet");
    queryKeys = read_key_set("querySet");
    params.insertSet = insertKeys.data();
    params.numInserts = insertKeys.size();
    params.querySet = queryKeys.data();
    params.numQueries = queryKeys.size();
    if (params.numQueries < params.numRounds) {
      throw std::invalid_argument("The query set must contain at least one query per round");
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << std::endl;
    return -1;
  }

  const std::string filterType = params.output_file;
  const std::string storageEngine = result["storageEngine"].as<std::string>();
  const std::string reverseMapEngine = result["reverseMapEngine"].as<std::string>();
  const bool microBench = result["microBench"].as<bool>();
  std::cout << "Testing filter: " << filterType
            << " with workload: " << result["queryWorkload"].as<std::string>()
            << " startWithAdversarialPhase " << params.startWithAdversarialPhase
            << " sortAndInsertFingerprints " << params.qfConfig.reverseMapConfig.sortAndInsertFingerprints
            << std::endl;

  int ret = -1;
  if (microBench) {
    ret = run_benchmark_with_storage_engine<
        MockDB,
        MockDB>(filterType, params);
  } else if (
      storageEngine == "splinterDB" && reverseMapEngine == "wiredTiger") {
    ret = run_benchmark_with_storage_engine<
        SplinterDB,
        WiredTigerReverseMap>(filterType, params);
  } else if (
      storageEngine == "splinterDB" && reverseMapEngine == "splinterDB") {
    ret = run_benchmark_with_storage_engine<
        SplinterDB,
        SplinterDB>(filterType, params);
  } else if (
      storageEngine == "wiredTiger" && reverseMapEngine == "wiredTiger") {
    ret = run_benchmark_with_storage_engine<
        WiredTigerBackingStore,
        WiredTigerReverseMap>(filterType, params);
  } else if (
      storageEngine == "wiredTiger" && reverseMapEngine == "wiredTigerLsm") {
    ret = run_benchmark_with_storage_engine<
        WiredTigerBackingStore,
        WiredTigerReverseMapLsm>(filterType, params);
  } else if (
      storageEngine == "wiredTiger" && reverseMapEngine == "splinterDB") {
    ret = run_benchmark_with_storage_engine<
        WiredTigerBackingStore,
        SplinterDB>(filterType, params);
  }
  if (ret) {
    std::cout << "Test failed" << std::endl;
    return -1;
  }
  return 0;
}
