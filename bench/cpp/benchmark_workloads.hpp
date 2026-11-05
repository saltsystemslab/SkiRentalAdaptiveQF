#ifndef BENCHMARK_WORKLOADS_HPP
#define BENCHMARK_WORKLOADS_HPP

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <initializer_list>
#include <list>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "benchmark_params.hpp"
#include "benchmark_output.hpp"

using BenchmarkClock = std::chrono::high_resolution_clock;

enum class QueryOutcome { TrueNegative, TruePositive, FalsePositive };

// Perfect recognition of an ordinary query's correction outcome, modeling
// the timing side channel. No filter strategy or detector state is exposed.
struct QueryFeedback {
  int adaptationResult = 0; // 0: tolerated, 1: corrected, -1: failed (FP only).
};

struct RoundStats {
  uint64_t falsePositives = 0;
  uint64_t truePositives = 0;
  uint64_t adapts = 0;
};

struct WorkloadStats {
  uint64_t falsePositives = 0;
  uint64_t adapts = 0;
  uint64_t adversarialQueries = 0;
  std::vector<AdversaryEvent> adversaryEvents;
#ifdef PERF
  std::vector<uint64_t> dbMissLatencies;
  std::vector<uint64_t> adaptLatencies;
  std::unordered_map<uint64_t, uint64_t> fpFrequency;
#endif
};

template <typename QFilter, typename DbStorageEngine>
QueryOutcome execute_query(
    QFilter& qf,
    DbStorageEngine& db,
    uint64_t queryKey,
    RoundStats& roundStats,
    WorkloadStats& stats,
    QueryFeedback* feedback = nullptr) {
  if (feedback) *feedback = {};
  QFilterQueryResult result;
  qf.queryFilter(queryKey, &result);
  if (!result.key_present) {
    return QueryOutcome::TrueNegative;
  }

#ifdef PERF
  const bool sampleQuery = rand() < INT_MAX;
  const auto dbStart = sampleQuery ? BenchmarkClock::now() : BenchmarkClock::time_point{};
#endif
  const int dbResult = db.lookupKey(queryKey);
#ifdef PERF
  if (sampleQuery) {
    stats.dbMissLatencies.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(
        BenchmarkClock::now() - dbStart).count());
  }
#endif
  if (dbResult) {
    roundStats.truePositives++;
    return QueryOutcome::TruePositive;
  }

#ifdef PERF
  stats.fpFrequency[queryKey]++;
#endif
  roundStats.falsePositives++;
  stats.falsePositives++;
#ifdef PERF
  const auto adaptStart = sampleQuery ? BenchmarkClock::now() : BenchmarkClock::time_point{};
#endif
  const int adaptResult = qf.adapt(queryKey, &result);
  if (feedback) feedback->adaptationResult = adaptResult;
#ifdef PERF
  if (sampleQuery) {
    stats.adaptLatencies.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(
        BenchmarkClock::now() - adaptStart).count());
  }
#endif
  if (adaptResult != -1) {
    roundStats.adapts += adaptResult;
    stats.adapts += adaptResult;
  }
  return QueryOutcome::FalsePositive;
}

// Evenly split the complete query stream; widen the product to avoid overflow.
inline uint64_t query_boundary(uint64_t numQueries, uint64_t numParts, uint64_t completedParts) {
  return static_cast<__uint128_t>(numQueries) * completedParts / numParts;
}

class WorkloadMeasurements {
public:
  RoundStats intervalStats;

  WorkloadMeasurements(const BenchmarkParams& params, FILE* roundsFile)
      : numQueries(params.numQueries), numRounds(params.numRounds), roundsFile(roundsFile),
        nextBoundary(query_boundary(numQueries, numRounds, 1)),
        benchStart(BenchmarkClock::now()), intervalStart(benchStart) {}

  template <typename QFilter>
  void report_if_due(uint64_t completedQueries, const WorkloadStats& stats, QFilter& qf) {
    if (completedQueries < nextBoundary) {
      return;
    }
    const auto intervalEnd = BenchmarkClock::now();
    const std::chrono::duration<double, std::micro> intervalDuration = intervalEnd - intervalStart;
    const std::chrono::duration<double, std::micro> overallDuration = intervalEnd - benchStart;
    const uint64_t intervalQueries = completedQueries - previousBoundary;
    const double throughput = intervalQueries / intervalDuration.count();
    const double cumulativeThroughput = completedQueries / overallDuration.count();
    const double loadFactor = qf.loadFactor();
    for (FILE* file : {roundsFile, stdout}) {
      fprintf(
          file,
          "%lu %f %lu %lu %f %lu %lu %lu %lu %f\n",
          round,
          throughput,
          intervalStats.falsePositives,
          intervalStats.truePositives,
          cumulativeThroughput,
          stats.falsePositives,
          stats.adversarialQueries,
          intervalStats.adapts,
          stats.adapts,
          loadFactor);
    }
    intervalStats = {};
    previousBoundary = completedQueries;
    round++;
    if (round < numRounds) {
      nextBoundary = query_boundary(numQueries, numRounds, round + 1);
    }
    intervalStart = BenchmarkClock::now();
  }

private:
  uint64_t numQueries;
  uint64_t numRounds;
  FILE* roundsFile;
  uint64_t nextBoundary;
  uint64_t previousBoundary = 0;
  uint64_t round = 0;
  BenchmarkClock::time_point benchStart;
  BenchmarkClock::time_point intervalStart;
};

template <typename QFilter, typename DbStorageEngine>
WorkloadStats run_standard_workload(
    QFilter& qf, DbStorageEngine& db, const BenchmarkParams& params, FILE* roundsFile) {
  WorkloadStats stats;
  WorkloadMeasurements measurements(params, roundsFile);
  for (uint64_t queryIdx = 0; queryIdx < params.numQueries; queryIdx++) {
    execute_query(qf, db, params.querySet[queryIdx], measurements.intervalStats, stats);
    measurements.report_if_due(queryIdx + 1, stats, qf);
  }
  return stats;
}

template <typename QFilter, typename DbStorageEngine>
WorkloadStats run_adversarial_workload(
    QFilter& qf, DbStorageEngine& db, const BenchmarkParams& params, FILE* roundsFile) {
  WorkloadStats stats;
  std::deque<uint64_t> candidates;
  WorkloadMeasurements measurements(params, roundsFile);
  for (uint64_t queryIdx = 0; queryIdx < params.numQueries; queryIdx++) {
    uint64_t queryKey = params.querySet[queryIdx];
    if (queryIdx % params.adversarial_freq == 0 && !candidates.empty()) {
      queryKey = candidates.front();
      candidates.pop_front();
      stats.adversarialQueries++;
    }
    if (execute_query(qf, db, queryKey, measurements.intervalStats, stats) == QueryOutcome::FalsePositive) {
      // A replay returns to the queue only while it remains a false positive.
      candidates.push_back(queryKey);
    }
    measurements.report_if_due(queryIdx + 1, stats, qf);
  }
  return stats;
}

template <typename QFilter, typename DbStorageEngine>
WorkloadStats run_phased_adversarial_workload(
    QFilter& qf, DbStorageEngine& db, const BenchmarkParams& params, FILE* roundsFile) {
  WorkloadStats stats;
  std::deque<uint64_t> candidates;
  WorkloadMeasurements measurements(params, roundsFile);
  uint64_t queryIdx = 0;
  for (int phase = 0; phase < params.numPhases; phase++) {
    const bool adversarialPhase = phase % 2 == 0
        ? params.startWithAdversarialPhase : !params.startWithAdversarialPhase;
    const uint64_t phaseEnd = query_boundary(params.numQueries, params.numPhases, phase + 1);
    const uint64_t phaseStartRound = phase * (params.numRounds / params.numPhases);
    printf("Round %lu is_adversarial: %d\n", phaseStartRound, adversarialPhase);
    for (; queryIdx < phaseEnd; queryIdx++) {
      uint64_t queryKey = params.querySet[queryIdx];
      if (adversarialPhase && queryIdx % params.adversarial_freq == 0 && !candidates.empty()) {
        queryKey = candidates.front();
        candidates.pop_front();
        stats.adversarialQueries++;
      }
      // Keep collecting candidates during uniform phases for the next adversarial phase.
      if (execute_query(qf, db, queryKey, measurements.intervalStats, stats) == QueryOutcome::FalsePositive) {
        candidates.push_back(queryKey);
      }
      measurements.report_if_due(queryIdx + 1, stats, qf);
    }
  }
  return stats;
}

template <typename QFilter, typename DbStorageEngine>
WorkloadStats run_adaptive_adversarial_workload(
    QFilter& qf, DbStorageEngine& db, const BenchmarkParams& params, FILE* roundsFile) {
  WorkloadStats stats;
  std::list<uint64_t> candidates;
  std::unordered_map<uint64_t, std::list<uint64_t>::iterator> active;
  WorkloadMeasurements measurements(params, roundsFile);
  uint64_t queryIdx = 0;
  bool repeatMode = false;
  auto retire = [&](uint64_t key) {
    const auto found = active.find(key);
    if (found != active.end()) {
      candidates.erase(found->second);
      active.erase(found);
    }
  };
  for (uint64_t performed = 0; performed < params.numQueries; performed++) {
    const bool repeatBefore = repeatMode;
    const bool replay = repeatMode && performed % params.adversarial_freq == 0
        && !candidates.empty();
    const uint64_t key = replay ? candidates.front() : params.querySet[queryIdx++];
    if (replay) stats.adversarialQueries++;
    QueryFeedback feedback;
    const QueryOutcome outcome = execute_query(
        qf, db, key, measurements.intervalStats, stats, &feedback);
    const char* observed = outcome == QueryOutcome::TrueNegative ? "negative" : "present";
    if (outcome == QueryOutcome::FalsePositive) {
      if (feedback.adaptationResult < 0) {
        throw std::runtime_error("Adaptive-adversary correction failed at query "
            + std::to_string(performed + 1));
      }
      repeatMode = feedback.adaptationResult == 0;
      observed = repeatMode ? "tolerated" : "adapted";
      if (!repeatMode) {
        retire(key);
      } else if (!active.count(key)) {
        candidates.push_back(key);
        active.emplace(key, std::prev(candidates.end()));
      } else if (replay) {
        candidates.splice(candidates.end(), candidates, active.at(key));
      }
    } else if (replay) {
      // Collateral corrections can invalidate a candidate. Retire it on its
      // ordinary replay without changing our belief about the current policy.
      retire(key);
    }
    if (outcome == QueryOutcome::FalsePositive || replay) {
      stats.adversaryEvents.push_back(
          {performed + 1, key, replay, observed, repeatBefore, repeatMode, active.size()});
    }
    measurements.report_if_due(performed + 1, stats, qf);
  }
  return stats;
}

struct MicrobenchmarkStats {
  double trueQueryUs;
  double queryUs;
};

// Measure the filter after the main workload, without database lookups or adaptation.
template <typename QFilter>
MicrobenchmarkStats run_microbenchmark_workload(QFilter& qf, const BenchmarkParams& params) {
  std::mt19937_64 generator(params.qfConfig.randomSeed);
  std::shuffle(params.insertSet, params.insertSet + params.numInserts, generator);
  QFilterQueryResult result;
  auto query_insert_set = [&]() {
    for (uint64_t i = 0; i < params.numInserts; i++) {
      if (i % 1000000 == 0) printProgressBar(i, params.numInserts);
      qf.queryFilter(params.insertSet[i], &result);
#ifdef CORRECTNESS
      if (!result.key_present) {
        printf("Key[%lu] not found: %lu\n", i, params.insertSet[i]);
        abort();
      }
#endif
    }
  };

  query_insert_set(); // Warm up before measuring true-positive queries.
  const auto trueQueryStart = BenchmarkClock::now();
  query_insert_set();
  const std::chrono::duration<double, std::micro> trueQueryDuration =
      BenchmarkClock::now() - trueQueryStart;

  const auto queryStart = BenchmarkClock::now();
  for (uint64_t i = 0; i < params.numQueries; i++) {
    qf.queryFilter(params.querySet[i], &result);
  }
  const std::chrono::duration<double, std::micro> queryDuration =
      BenchmarkClock::now() - queryStart;
  return {trueQueryDuration.count(), queryDuration.count()};
}

#endif
