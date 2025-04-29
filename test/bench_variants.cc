#include <chrono>
#include <iostream>
#include <stdio.h>

#include "cxxopts.hpp"
#include "dski_adaptive_filter.hpp"
#include "mono_adaptive_filter.hpp"
#include "non_adaptive_filter.hpp"
#include "qf_filter.hpp"
#include "rski_adaptive_filter.hpp"
#include "splinter_backing_store.hpp"
#include "dummy_backing_store.hpp"

template <typename QFilter>
int run_benchmark_with_no_db(
    QFilter qf,
    QFilterConfig qfConfig,
    uint64_t *insertSet,
    uint64_t numInserts,
    uint64_t *querySet,
    uint64_t numQueries,
    uint64_t numRounds,
    std::string output_file) {
  FILE *rounds_file = fopen(output_file.c_str(), "w");
  fprintf(
      rounds_file,
      "round round_thput round_fp, cumulative_thput cumulative_fp "
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
  uint64_t fpCount = 0;
  auto bench_start = std::chrono::high_resolution_clock::now();
  QFilterQueryResult qfFilterQueryResult;
  for (uint64_t r = 0; r < numRounds; r++) {
    auto round_start = std::chrono::high_resolution_clock::now();
    uint64_t roundFpCount = 0;
    for (uint64_t i = 0; i < numQueries; i++) {
      qf.queryFilter(querySet[i], &qfFilterQueryResult);
      if (qfFilterQueryResult.key_present) {
        fpCount++;
        roundFpCount++;
        qf.adapt(querySet[i], &qfFilterQueryResult);
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
  return 0;
}

template <typename QFilter>
int run_benchmark(
    QFilter qf,
    QFilterConfig qfConfig,
    uint64_t *insertSet,
    uint64_t numInserts,
    uint64_t *querySet,
    uint64_t numQueries,
    uint64_t numRounds,
    std::string output_file) {
  FILE *rounds_file = fopen(output_file.c_str(), "w");
  fprintf(
      rounds_file,
      "round round_thput round_fp, cumulative_thput cumulative_fp "
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
  SplinterDBBackingStore db;
  db.init("database");
  for (uint64_t i = 0; i < numInserts; i++) {
    db.insertKV(insertSet[i], insertSet[i], 0);
  }
  uint64_t fpCount = 0;
  QFilterQueryResult qfFilterQueryResult;
  auto bench_start = std::chrono::high_resolution_clock::now();
  for (uint64_t r = 0; r < numRounds; r++) {
    auto round_start = std::chrono::high_resolution_clock::now();
    uint64_t roundFpCount = 0;
    for (uint64_t i = 0; i < numQueries; i++) {
      qf.queryFilter(querySet[i], &qfFilterQueryResult);
      if (qfFilterQueryResult.key_present) {
        if (!db.keyExists(querySet[i])) {
          fpCount++;
          roundFpCount++;
          qf.adapt(querySet[i], &qfFilterQueryResult);
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
  return 0;
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

  size_t numInserts = (1ull << qbits) * 0.9f; // strtoull(argv[3], NULL, 10);
  size_t minirun_id_bitmask = (1ull << (qbits + rbits)) - 1;

  uint64_t *insertSet = (uint64_t *)malloc(numInserts * sizeof(uint64_t));
  RAND_bytes((unsigned char *)insertSet, numInserts * sizeof(uint64_t));
  uint64_t *querySet = (uint64_t *)malloc(numQueries * sizeof(uint64_t));
  RAND_bytes((unsigned char *)querySet, numQueries * sizeof(uint64_t));

  std::cout << "Testing filter: " << filterType
            << " with workload: " << queryWorkload << std::endl;

  if (queryWorkload == "false-positive") {
    for (uint64_t i = 0; i < numQueries; i++) {
      // Zero out bits in the insert set to force a false positive.
      querySet[i] = (insertSet[i % numInserts] & minirun_id_bitmask);
    }
  } else {
    abort();
  }

  QFilterConfig qfConfig;
  qfConfig.qbits = qbits;
  qfConfig.rbits = rbits;
  qfConfig.max_load_factor = 0.95;
  qfConfig.breakEvenCount = 15;

  if (result["microBench"].as<bool>()) {
    if (filterType == "adaptive") {
      MonotonicAdaptiveFilter<DummyDBBackingStore> f;
      run_benchmark_with_no_db<MonotonicAdaptiveFilter<DummyDBBackingStore>>(
          f,
          qfConfig,
          insertSet,
          numInserts,
          querySet,
          numQueries,
          numRounds,
          "mono.csv");
    }
    if (filterType == "nonAdaptive") {
      NonAdaptiveFilter f;
      run_benchmark_with_no_db<NonAdaptiveFilter>(
          f,
          qfConfig,
          insertSet,
          numInserts,
          querySet,
          numQueries,
          numRounds,
          "non.csv");
    }
    if (filterType == "dSkiAdaptive") {
      DSkiAdaptiveFilter<DummyDBBackingStore> f;
      run_benchmark_with_no_db<DSkiAdaptiveFilter<DummyDBBackingStore>>(
          f,
          qfConfig,
          insertSet,
          numInserts,
          querySet,
          numQueries,
          numRounds,
          "dski.csv");
    }
    if (filterType == "rSkiAdaptive") {
      RSkiAdaptiveFilter<DummyDBBackingStore> f;
      run_benchmark_with_no_db<RSkiAdaptiveFilter<DummyDBBackingStore>>(
          f,
          qfConfig,
          insertSet,
          numInserts,
          querySet,
          numQueries,
          numRounds,
          "rski.csv");
    }
  } else {
    if (filterType == "adaptive") {
      MonotonicAdaptiveFilter<SplinterDBBackingStore> f;
      run_benchmark<MonotonicAdaptiveFilter<SplinterDBBackingStore>>(
          f,
          qfConfig,
          insertSet,
          numInserts,
          querySet,
          numQueries,
          numRounds,
          "mono.csv");
    }
    if (filterType == "nonAdaptive") {
      NonAdaptiveFilter f;
      run_benchmark<NonAdaptiveFilter>(
          f,
          qfConfig,
          insertSet,
          numInserts,
          querySet,
          numQueries,
          numRounds,
          "non.csv");
    }
    if (filterType == "dSkiAdaptive") {
      DSkiAdaptiveFilter<SplinterDBBackingStore> f;
      run_benchmark<DSkiAdaptiveFilter<SplinterDBBackingStore>>(
          f,
          qfConfig,
          insertSet,
          numInserts,
          querySet,
          numQueries,
          numRounds,
          "dski.csv");
    }
    if (filterType == "rSkiAdaptive") {
      RSkiAdaptiveFilter<SplinterDBBackingStore> f;
      run_benchmark<RSkiAdaptiveFilter<SplinterDBBackingStore>>(
          f,
          qfConfig,
          insertSet,
          numInserts,
          querySet,
          numQueries,
          numRounds,
          "rski.csv");
    }
  }

  return 0;
}