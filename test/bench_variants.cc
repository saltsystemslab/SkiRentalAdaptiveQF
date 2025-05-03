#include <chrono>
#include <iostream>
#include <stdio.h>

#include "cxxopts.hpp"
#include "dski_adaptive_filter.hpp"
#include "dummy_backing_store.hpp"
#include "mono_adaptive_filter.hpp"
#include "non_adaptive_filter.hpp"
#include "qf_filter.hpp"
#include "rski_adaptive_filter.hpp"
#include "splinter_backing_store.hpp"
#include "wiredtiger_backing_store.hpp"

struct BenchmarkParams {
  QFilterConfig qfConfig;
  uint64_t *insertSet;
  uint64_t numInserts;
  uint64_t *querySet;
  uint64_t numQueries;
  uint64_t numRounds;
  std::string output_file;
};

template <typename DbStorageEngine, typename QFilter>
int run_benchmark(BenchmarkParams params) {
  QFilter qf;
  QFilterConfig qfConfig = params.qfConfig;
  uint64_t *insertSet = params.insertSet;
  uint64_t numInserts = params.numInserts;
  uint64_t *querySet = params.querySet;
  uint64_t numQueries = params.numQueries;
  uint64_t numRounds = params.numRounds;
  std::string output_file = params.output_file;

  std::cout << "Writing to " << output_file << std::endl;
  FILE *rounds_file = fopen(output_file.c_str(), "w");
  fprintf(
      rounds_file,
      "round round_thput round_fp cumulative_thput cumulative_fp "
      "load_factor\n");

  int ret = 0;
  ret = qf.construct(qfConfig);
  if (ret < 0) {
    abort();
  }
  ret = qf.bulkLoad(insertSet, numInserts);
  if (ret < 0) {
    abort();
  }
  DbStorageEngine db;
  db.init("database", qfConfig.qbits + qfConfig.rbits);
  for (uint64_t i = 0; i < numInserts; i++) {
    db.insertKV(insertSet[i], insertSet[i], 0);
  }

  uint64_t fpCount = 0;
  QFilterQueryResult qfFilterQueryResult;
  auto bench_start = std::chrono::high_resolution_clock::now();
  for (uint32_t r = 0; r < numRounds; r++) {
    auto round_start = std::chrono::high_resolution_clock::now();
    uint64_t roundFpCount = 0;
    for (uint64_t i = 0; i < numQueries; i++) {
      qf.queryFilter(querySet[i], &qfFilterQueryResult);
      if (qfFilterQueryResult.key_present) {
        if (!db.searchKV(querySet[i])) {
          fpCount++;
          roundFpCount++;
          if (qf.adapt(querySet[i], &qfFilterQueryResult)) {
            return -1;
          }
        }
      }
    }
    auto round_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::micro> round_duration =
        round_end - round_start;
    double roundThroughput = ((double)numQueries) / round_duration.count();
    std::chrono::duration<double, std::micro> overall_duration =
        round_end - bench_start;
    double cumulativeThroughput =
        ((double)(r + 1) * (double)numQueries) / overall_duration.count();
    fprintf(
        rounds_file,
        "%d %f %lu %f %lu %f\n",
        r,
        roundThroughput,
        roundFpCount,
        cumulativeThroughput,
        fpCount,
        qf.loadFactor());
  }
  db.close();
  return 0;
}

template <typename DbStorageEngine, typename ReverseMapEngine>
int run_benchmark_with_storage_engine(
    std::string filterType, BenchmarkParams params) {
  int ret = -1;
  if (filterType == "adaptive") {
    ret = run_benchmark<
        DbStorageEngine,
        MonotonicAdaptiveFilter<ReverseMapEngine>>(params);
  }
  if (filterType == "nonAdaptive") {
    ret = run_benchmark<DbStorageEngine, NonAdaptiveFilter>(params);
  }
  if (filterType == "dSkiAdaptive") {
    ret = run_benchmark<DbStorageEngine, DSkiAdaptiveFilter<ReverseMapEngine>>(
        params);
  }
  if (filterType == "rSkiAdaptive") {
    ret = run_benchmark<DbStorageEngine, RSkiAdaptiveFilter<ReverseMapEngine>>(
        params);
  }
  return ret;
}

int main(int argc, char **argv) {
  cxxopts::Options options("Bench adaptive filter variants");

  options.add_options()(
      "q,quotient",
      "Quotient bits to use in filter",
      cxxopts::value<int>()->default_value("22"))(

      "r,remainder",
      "Remainder bits to use in filter",
      cxxopts::value<int>()->default_value("8"))(

      "queryWorkload",
      "uniform, false-positive, zipfian, adversarial",
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

      "numQueries",
      "Number of queries ",
      cxxopts::value<uint64_t>()->default_value("20000"))(

      "numRounds",
      "Number of rounds",
      cxxopts::value<int>()->default_value("100"))(

      "microBench",
      "Only query filter. Only valid for false-positive",
      cxxopts::value<bool>()->default_value("false"));

  auto result = options.parse(argc, argv);
  int qbits = result["q"].as<int>();
  int rbits = result["r"].as<int>();
  uint64_t numQueries = result["numQueries"].as<uint64_t>();
  int numRounds = result["numRounds"].as<int>();
  std::string queryWorkload = result["queryWorkload"].as<std::string>();
  std::string filterType = result["filter"].as<std::string>();
  std::string storageEngine = result["storageEngine"].as<std::string>();
  std::string reverseMapEngine = result["reverseMapEngine"].as<std::string>();
  bool microBench = result["microBench"].as<bool>();
  size_t numInserts = (1ull << qbits) * 0.9f; // strtoull(argv[3], NULL, 10);

  uint64_t *insertSet = (uint64_t *)malloc(numInserts * sizeof(uint64_t));
  RAND_bytes((unsigned char *)insertSet, numInserts * sizeof(uint64_t));
  uint64_t *querySet = (uint64_t *)malloc(numQueries * sizeof(uint64_t));
  RAND_bytes((unsigned char *)querySet, numQueries * sizeof(uint64_t));

  std::cout << "Testing filter: " << filterType
            << " with workload: " << queryWorkload << std::endl;

  uint64_t minirun_bitmask = (1ULL << qbits + rbits) - 1;
  if (queryWorkload == "false-positive") {
    for (uint64_t i = 0; i < numQueries; i++) {
      // Zero out bits in the insert set to force a false positive.
      querySet[i] = (insertSet[i % numInserts]) & minirun_bitmask;
      if (querySet[i] == insertSet[i % numInserts]) {
        querySet[i] |= (1ULL << (qbits + rbits + 1));
      }
    }
  } else {
    abort();
  }

  QFilterConfig qfConfig;
  qfConfig.qbits = qbits;
  qfConfig.rbits = rbits;
  qfConfig.max_load_factor = 0.95;
  qfConfig.breakEvenCount = 15;

  BenchmarkParams params;
  params.qfConfig = qfConfig;
  params.insertSet = insertSet;
  params.numInserts = numInserts;
  params.querySet = querySet;
  params.numQueries = numQueries;
  params.numRounds = numRounds;
  params.output_file = filterType + ".csv";

  int ret = -1;
  if (microBench) {
    ret = run_benchmark_with_storage_engine<
        DummyDBBackingStore,
        DummyDBBackingStore>(filterType, params);
  } else if (
      storageEngine == "splinterDB" && reverseMapEngine == "wiredTiger") {
    ret = run_benchmark_with_storage_engine<
        SplinterDBBackingStore,
        WiredTigerBackingStore>(filterType, params);
  } else if (
      storageEngine == "splinterDB" && reverseMapEngine == "splinterDB") {
    ret = run_benchmark_with_storage_engine<
        SplinterDBBackingStore,
        SplinterDBBackingStore>(filterType, params);
  } else if (
      storageEngine == "wiredTiger" && reverseMapEngine == "wiredTiger") {
    ret = run_benchmark_with_storage_engine<
        WiredTigerBackingStore,
        WiredTigerBackingStore>(filterType, params);
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