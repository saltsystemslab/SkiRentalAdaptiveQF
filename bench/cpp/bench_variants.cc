#include <chrono>
#include <deque>
#include <iostream>
#include <stdio.h>
#include <algorithm>
#include <queue>
#include <random>
#include <unordered_set>

#include "cxxopts.hpp"
#include "qf_filter.hpp"

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
  fprintf(latency_file,"metric,numSamples,min,50p,99p,99.99p,max,avg\n");

  {
  int p50 = fp_miss_latencies.size() / 2;
  int p99 = fp_miss_latencies.size() * 0.99;
  int p9999 = fp_miss_latencies.size() * 0.9999;
  int max = fp_miss_latencies.size()-1;
  double den = 1.0 / fp_miss_latencies.size();
  double avg = 0.0;
  for (uint64_t i = 0; i < fp_miss_latencies.size(); i++) {
    avg = avg + den * fp_miss_latencies[i];
  }
  fprintf(latency_file,"DbMiss,%lu,%lu,%lu,%lu,%lu,%lu,%f\n", fp_miss_latencies.size(), fp_miss_latencies[0], fp_miss_latencies[p50], fp_miss_latencies[p99], fp_miss_latencies[p9999], fp_miss_latencies[max],avg);
  }

  {
  int p50 = adapt_latencies.size() / 2;
  int p99 = adapt_latencies.size() * 0.99;
  int p9999 = adapt_latencies.size() * 0.9999;
  int max = adapt_latencies.size()-1;
  double den = 1.0 / adapt_latencies.size();
  double avg = 0.0;
  for (uint64_t i = 0; i < adapt_latencies.size(); i++) {
    avg = avg + den * adapt_latencies[i];
  }
  fprintf(latency_file,"Adapt,%lu,%lu,%lu,%lu,%lu,%lu,%f\n", adapt_latencies.size(), adapt_latencies[0], adapt_latencies[p50], adapt_latencies[p99], adapt_latencies[p9999], adapt_latencies[max],avg);
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

class ActiveFalsePositivePool {
public:
  bool add(uint64_t queryKey) {
    if (!active.insert(queryKey).second) {
      return false;
    }
    queries.push_back(queryKey);
    return true;
  }

  bool empty() const {
    return queries.empty();
  }

  size_t size() const {
    return active.size();
  }

  uint64_t takeNext() {
    uint64_t queryKey = queries.front();
    queries.pop_front();
    return queryKey;
  }

  void keep(uint64_t queryKey) {
    if (active.count(queryKey)) {
      queries.push_back(queryKey);
    }
  }

  bool retire(uint64_t queryKey) {
    return active.erase(queryKey) > 0;
  }

private:
  std::deque<uint64_t> queries;
  std::unordered_set<uint64_t> active;
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
  bool shouldSort = params.shouldSort;
  std::string output_file = params.output_file + ".csv";
  int is_adversarial = params.is_adversarial;
  int adversarial_freq = params.adversarial_freq;
  bool sortAndInsertFingerprints = params.sortAndInsertFingerprints;
  uint64_t storageSleepUs = params.storageSleepUs;
  uint64_t reverseSleepUs = params.reverseSleepUs;
  bool isAdaptiveAdversary = params.isAdaptiveAdversary;
  uint64_t adversaryWindowSize = params.adversaryWindowSize;

  int isPhasedTest = params.isPhasedTest;
  bool startWithAdversarialPhase = params.startWithAdversarialPhase;
  int numPhases = params.numPhases;
  int num_rounds_per_phase = numRounds / numPhases;


#ifdef PERF
  double sampleRate = 1.0;
  uint64_t sampleThreshold = INT_MAX * sampleRate;
  printf("sampleThreshold: %lu", sampleThreshold);
  std::vector<uint64_t> fp_miss_latencies;
  std::unordered_map<uint64_t, uint64_t> fp_freq;
  std::vector<uint64_t> adapt_latencies;
#endif

  std::cout << "Writing to " << output_file << std::endl;
  FILE *rounds_file = fopen(output_file.c_str(), "w");
  if (rounds_file == nullptr) {
    perror("Failed to open benchmark output file");
    return -1;
  }
  fprintf(
      rounds_file,
      "round round_thput round_fp round_tp cumulative_thput cumulative_fp cumulative_adversarial_queries "
      "round_adapts cumulative_adapts load_factor adversary_repeat_mode "
      "boundary_probes adversary_switches active_adversarial_queries "
      "admitted_adversarial_queries retired_adversarial_queries "
      "uniform_adapts repeat_adapts\n");

  FILE *adversary_file = nullptr;
  if (isAdaptiveAdversary) {
    std::string adversary_file_name = params.output_file + "_adversary.csv";
    adversary_file = fopen(adversary_file_name.c_str(), "w");
    if (adversary_file == nullptr) {
      perror("Failed to open adaptive-adversary output file");
      fclose(rounds_file);
      return -1;
    }
    fprintf(
        adversary_file,
        "query_index,negative_queries,probe_key,probe_result,"
        "inferred_hybrid_policy,selected_mode,active_pool_size\n");
  }

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

  QFilterQueryResult qfFilterQueryResult;
  uint64_t fpCount = 0;
  uint64_t adaptCount = 0;
  uint64_t numQueriesPerRound = numQueries / numRounds;
  uint64_t total_adversarial_queries_count = 0;
  ActiveFalsePositivePool adversarialPool;
  uint64_t retiredAdversarialQueries = 0;
  uint64_t negativeQueryCount = 0;
  uint64_t nextAdversaryBoundary = adversaryWindowSize;
  uint64_t boundaryProbes = 0;
  uint64_t adversarySwitches = 0;
  uint64_t admittedAdversarialQueries = 0;
  uint64_t uniformModeAdapts = 0;
  uint64_t repeatModeAdapts = 0;
  bool adaptiveRepeatMode = false;
  const bool shouldCollectAdversarialCandidates =
      is_adversarial || isPhasedTest || isAdaptiveAdversary;

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
      bool isReplayQuery = false;
      bool isBoundaryProbe = false;
      bool queryStartedInRepeatMode = adaptiveRepeatMode;

      if (
          isAdaptiveAdversary &&
          negativeQueryCount >= nextAdversaryBoundary) {
        nextAdversaryBoundary += adversaryWindowSize;
        if (!adversarialPool.empty()) {
          queryKey = adversarialPool.takeNext();
          isReplayQuery = true;
          isBoundaryProbe = true;
          total_adversarial_queries_count++;
        } else if (adaptiveRepeatMode) {
          adaptiveRepeatMode = false;
          adversarySwitches++;
        }
      } else if (
          (is_adversarial || (isAdaptiveAdversary && adaptiveRepeatMode)) &&
          queryIdx % adversarial_freq == 0 &&
          !adversarialPool.empty()) {
        total_adversarial_queries_count++;
        queryKey = adversarialPool.takeNext();
        isReplayQuery = true;
      }

#ifdef PERF
      std::chrono::time_point<std::chrono::high_resolution_clock> dbQueryStart, dbQueryEnd, adaptStart, adaptEnd;
#endif
      qf.queryFilter(queryKey, &qfFilterQueryResult);
      if (qfFilterQueryResult.key_present) {
#ifdef PERF
        fp_freq[queryKey]++;
        bool sampleQuery = rand() < sampleThreshold;
        if (sampleQuery) {
          dbQueryStart = std::chrono::high_resolution_clock::now();
        }
#endif
        int dbQueryResult = db.lookupKey(queryKey);
#ifdef PERF
        if (sampleQuery) {
          dbQueryEnd = std::chrono::high_resolution_clock::now();
          uint64_t timeElapsed = (dbQueryEnd - dbQueryStart).count();
          fp_miss_latencies.push_back(timeElapsed);
        }

#endif
        if (!dbQueryResult) {
          negativeQueryCount++;
          fpCount++;
          roundFpCount++;
#ifdef PERF
          if (sampleQuery) {
            adaptStart = std::chrono::high_resolution_clock::now();
          }
#endif
          int adaptRetCode = qf.adapt(queryKey, &qfFilterQueryResult);
#ifdef PERF
          if (sampleQuery) {
            adaptEnd = std::chrono::high_resolution_clock::now();
            adapt_latencies.push_back((adaptEnd - adaptStart).count());
          }
#endif
          if (adaptRetCode != -1) {
            roundAdaptCount += adaptRetCode;
            adaptCount += adaptRetCode;
            if (isAdaptiveAdversary && adaptRetCode > 0) {
              if (queryStartedInRepeatMode) {
                repeatModeAdapts += adaptRetCode;
              } else {
                uniformModeAdapts += adaptRetCode;
              }
            }
          }

          if (isReplayQuery) {
            adversarialPool.keep(queryKey);
          } else if (
              shouldCollectAdversarialCandidates &&
              adversarialPool.add(queryKey)) {
            admittedAdversarialQueries++;
          }

          if (isBoundaryProbe) {
            const bool oldMode = adaptiveRepeatMode;
            adaptiveRepeatMode = true;
            boundaryProbes++;
            if (oldMode != adaptiveRepeatMode) {
              adversarySwitches++;
            }
            fprintf(
                adversary_file,
                "%lu,%lu,%lu,false_positive,tolerate,repeat,%lu\n",
                queryIdx,
                negativeQueryCount,
                queryKey,
                adversarialPool.size());
          }
        } else {
          if (isReplayQuery && adversarialPool.retire(queryKey)) {
            retiredAdversarialQueries++;
          }
          if (isBoundaryProbe) {
            const bool oldMode = adaptiveRepeatMode;
            adaptiveRepeatMode = false;
            boundaryProbes++;
            if (oldMode != adaptiveRepeatMode) {
              adversarySwitches++;
            }
            fprintf(
                adversary_file,
                "%lu,%lu,%lu,database_hit,adapt,uniform,%lu\n",
                queryIdx,
                negativeQueryCount,
                queryKey,
                adversarialPool.size());
          }
          roundTruePositive++;
        }
      } else {
        negativeQueryCount++;
        if (isReplayQuery && adversarialPool.retire(queryKey)) {
          retiredAdversarialQueries++;
        }
        if (isBoundaryProbe) {
          const bool oldMode = adaptiveRepeatMode;
          adaptiveRepeatMode = false;
          boundaryProbes++;
          if (oldMode != adaptiveRepeatMode) {
            adversarySwitches++;
          }
          fprintf(
              adversary_file,
              "%lu,%lu,%lu,true_negative,adapt,uniform,%lu\n",
              queryIdx,
              negativeQueryCount,
              queryKey,
              adversarialPool.size());
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
        "%d %f %lu %lu %f %lu %lu %lu %lu %f %d %lu %lu %lu %lu %lu %lu %lu\n",
        r,
        roundThroughput,
        roundFpCount,
        roundTruePositive,
        cumulativeThroughput,
        fpCount,
        total_adversarial_queries_count,
        roundAdaptCount,
        adaptCount,
        qf.loadFactor(),
        adaptiveRepeatMode,
        boundaryProbes,
        adversarySwitches,
        adversarialPool.size(),
        admittedAdversarialQueries,
        retiredAdversarialQueries,
        uniformModeAdapts,
        repeatModeAdapts
        );
    fprintf(
        stdout,
        "%d %f %lu %lu %f %lu %lu %lu %lu %f %d %lu %lu %lu %lu %lu %lu %lu\n",
        r,
        roundThroughput,
        roundFpCount,
        roundTruePositive,
        cumulativeThroughput,
        fpCount,
        total_adversarial_queries_count,
        roundAdaptCount,
        adaptCount,
        qf.loadFactor(),
        adaptiveRepeatMode,
        boundaryProbes,
        adversarySwitches,
        adversarialPool.size(),
        admittedAdversarialQueries,
        retiredAdversarialQueries,
        uniformModeAdapts,
        repeatModeAdapts
        );
  }
  if (adversary_file != nullptr) {
    fclose(adversary_file);
  }
  fclose(rounds_file);
#ifdef PERF
  std::string latency_file_name = params.output_file + "_latency.csv";
  write_latencies_to_file(latency_file_name.c_str(), fp_miss_latencies, adapt_latencies);

  std::string fp_stats_file = params.output_file + "_fp_stats.csv";
  write_fp_stats_to_file(fp_stats_file.c_str(), fp_freq);
#endif
  printf("Done with the test\n");
  db.close();
  printf("Close DB\n");

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

  }

  {
  // Force the filter to adapt to get it to 95% load factor.

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
    ret = run_benchmark<DbStorageEngine, NonAdaptiveFilter<ReverseMapEngine>>(params);
  }
  #else
  if (filterType == "nonAdaptive") {
    ret = run_benchmark<DbStorageEngine, NonAdaptiveFilter<ReverseMapEngine>>(params);
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

      "adversaryWindowSize",
      "Negative-key queries per adaptive-adversary observation window",
      cxxopts::value<uint64_t>()->default_value("100000"));


  auto result = options.parse(argc, argv);
  int seed = result["seed"].as<int>();
  int qbits = result["q"].as<int>();
  int rbits = result["r"].as<int>();
  uint64_t numQueries = result["numQueries"].as<uint64_t>();
  uint64_t storageCacheSizeMB = result["storageCacheSizeMB"].as<uint64_t>();
  uint64_t reverseMapCacheSizeMB = result["reverseMapCacheSizeMB"].as<uint64_t>();
  uint64_t storageSleepUs = result["storageSleepUs"].as<uint64_t>();
  uint64_t reverseSleepUs = result["reverseSleepUs"].as<uint64_t>();
  int numRounds = result["numRounds"].as<int>();
  int advFreq = result["advFreq"].as<int>();
  int breakEven = result["breakEven"].as<int>();
  std::string queryWorkload = result["queryWorkload"].as<std::string>();
  std::string filterType = result["filter"].as<std::string>();
  std::string storageEngine = result["storageEngine"].as<std::string>();
  std::string reverseMapEngine = result["reverseMapEngine"].as<std::string>();
  bool microBench = result["microBench"].as<bool>();
  bool sortAndInsertFingerprints = result["sortAndInsertFingerprints"].as<bool>();
  bool shouldSort = result["sortAndInsertKeys"].as<bool>();
  bool shouldCollectDbStats = result["dbStats"].as<bool>();
  bool isPhasedTest = result["phasedTest"].as<bool>();
  bool startWithAdversarialPhase = result["startWithAdversarialPhase"].as<bool>();
  int numPhases = result["numPhases"].as<int>();
  uint64_t adversaryWindowSize =
      result["adversaryWindowSize"].as<uint64_t>();
  size_t numInserts = (1ull << qbits) * 0.9f; // strtoull(argv[3], NULL, 10);
  uint64_t *insertSet; 
  uint64_t *querySet;

  if (breakEven < 1) {
    std::cerr << "breakEven must be at least 1" << std::endl;
    return -1;
  }
  if (advFreq < 1 || advFreq > 100) {
    std::cerr << "advFreq must be between 1 and 100" << std::endl;
    return -1;
  }
  if (numRounds < 1 || numPhases < 1) {
    std::cerr << "numRounds and numPhases must be at least 1" << std::endl;
    return -1;
  }
  if (
      isPhasedTest &&
      (numPhases > numRounds || numRounds % numPhases != 0)) {
    std::cerr
        << "Phased tests require numPhases <= numRounds and evenly dividing it"
        << std::endl;
    return -1;
  }
  if (queryWorkload == "adaptive-adversarial" && adversaryWindowSize == 0) {
    std::cerr << "adversaryWindowSize must be positive" << std::endl;
    return -1;
  }
  if (
      queryWorkload == "adaptive-adversarial" &&
      adversaryWindowSize >= 1000000) {
    std::cerr
        << "adversaryWindowSize must be smaller than HybridSkiQF's "
        << "1,000,000-query detection window"
        << std::endl;
    return -1;
  }
  if (queryWorkload == "adaptive-adversarial" && isPhasedTest) {
    std::cerr << "adaptive-adversarial and phasedTest are separate modes"
              << std::endl;
    return -1;
  }

  const std::unordered_set<std::string> knownFilters = {
      "nonAdaptive",
      "adaptive",
      "skiQF",
      "randomizedSkiQF",
      "coinFlip",
      "hybridSkiQF",
  };
  if (!knownFilters.count(filterType)) {
    std::cerr << "Unknown filter: " << filterType << std::endl;
    return -1;
  }

  if (microBench) {
    shouldSort = false;
  }


  std::cout << "Testing filter: " << filterType
            << " with workload: " << queryWorkload 
            << " startWithAdversarialPhase " << startWithAdversarialPhase
            << " sortAndInsertFingerprints " << sortAndInsertFingerprints
            << std::endl;

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

    off_t offset = 0;
    while (offset < numKeys * sizeof(uint64_t)) {
      ssize_t bytes_read = pread(fd, querySet+ offset, numKeys * sizeof(uint64_t), offset + sizeof(uint64_t));
      offset += bytes_read;
      fprintf(stdout, "%lu\n", offset);
    }
    close(fd);
  }

  if (numQueries < static_cast<uint64_t>(numRounds)) {
    std::cerr << "The query set must contain at least one query per round"
              << std::endl;
    return -1;
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
  params.storageSleepUs = storageSleepUs;
  params.reverseSleepUs = reverseSleepUs;
  params.randomSeed = static_cast<uint64_t>(seed);
  params.isAdaptiveAdversary = queryWorkload == "adaptive-adversarial";
  params.adversaryWindowSize = adversaryWindowSize;


  
  if (queryWorkload == "adversarial") {
    params.is_adversarial = 1;
  }
  // For cyclic workloads, the benchmark will handle dynamically changing is_adversarial.

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
