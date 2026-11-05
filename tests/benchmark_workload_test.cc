// I don't think we need this file, it seems overkill.
#include "bench/cpp/benchmark_workloads.hpp"

#include <iostream>
#include <sstream>
#include <unordered_set>

namespace {

bool expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

struct FakeFilter {
  std::unordered_set<uint64_t> positiveKeys;
  std::unordered_map<uint64_t, uint64_t> adaptAfter;
  std::unordered_map<uint64_t, uint64_t> occurrences;
  std::vector<uint64_t> queries;
  std::vector<uint64_t> adaptCalls;
  std::vector<uint64_t> measurementQueryCounts;
  int unresolvedAdaptResult = 0;

  void queryFilter(uint64_t key, QFilterQueryResult* result) {
    queries.push_back(key);
    result->key_present = positiveKeys.count(key);
  }

  int adapt(uint64_t key, QFilterQueryResult*) {
    adaptCalls.push_back(key);
    const uint64_t occurrence = ++occurrences[key];
    auto threshold = adaptAfter.find(key);
    if (threshold != adaptAfter.end() && occurrence >= threshold->second) {
      positiveKeys.erase(key);
      return 1;
    }
    return unresolvedAdaptResult;
  }

  double loadFactor() {
    measurementQueryCounts.push_back(queries.size());
    return 0.9;
  }
};

struct FakeDb {
  std::unordered_set<uint64_t> presentKeys;
  std::unordered_map<uint64_t, uint64_t> hitOnLookup;
  std::unordered_map<uint64_t, uint64_t> lookupCounts;
  std::vector<uint64_t> lookups;

  int lookupKey(uint64_t key) {
    lookups.push_back(key);
    const uint64_t count = ++lookupCounts[key];
    auto threshold = hitOnLookup.find(key);
    return presentKeys.count(key) ||
        (threshold != hitOnLookup.end() && count >= threshold->second);
  }
};

BenchmarkParams parameters(std::vector<uint64_t>& queries) {
  BenchmarkParams params{};
  params.querySet = queries.data();
  params.numQueries = queries.size();
  params.numRounds = 2;
  params.adversarial_freq = 1;
  params.numPhases = 2;
  return params;
}

bool check_round_output(
    FILE* output, uint64_t expectedRounds, const WorkloadStats& stats,
    uint64_t expectedTruePositives, const std::vector<uint64_t>& expectedRoundFp) {
  rewind(output);
  char line[512];
  uint64_t rows = 0;
  uint64_t totalFp = 0;
  uint64_t totalTp = 0;
  uint64_t totalAdapts = 0;
  uint64_t lastReplayCount = 0;
  bool passed = true;
  while (fgets(line, sizeof(line), output)) {
    std::istringstream values(line);
    uint64_t round, fp, tp, cumulativeFp, replays, adapts, cumulativeAdapts;
    double throughput, cumulativeThroughput, loadFactor;
    if (!(values >> round >> throughput >> fp >> tp >> cumulativeThroughput >>
          cumulativeFp >> replays >> adapts >> cumulativeAdapts >> loadFactor)) {
      return expect(false, "round output must have the original ten numeric columns");
    }
    std::string extra;
    passed &= expect(!(values >> extra), "round output has extra diagnostic columns");
    totalFp += fp;
    totalTp += tp;
    totalAdapts += adapts;
    passed &= expect(round == rows, "round indices must be consecutive");
    if (!expectedRoundFp.empty()) {
      passed &= expect(rows < expectedRoundFp.size() && fp == expectedRoundFp[rows],
          "incorrect false-positive count for a measurement interval");
    }
    passed &= expect(cumulativeFp == totalFp, "cumulative false positives disagree with rounds");
    passed &= expect(cumulativeAdapts == totalAdapts, "cumulative adapts disagree with rounds");
    passed &= expect(replays >= lastReplayCount, "cumulative replay count decreased");
    passed &= expect(loadFactor == 0.9, "round output lost the filter load factor");
    lastReplayCount = replays;
    rows++;
  }
  passed &= expect(rows == expectedRounds, "incorrect number of output rounds");
  passed &= expect(totalFp == stats.falsePositives, "output false positives disagree with workload totals");
  passed &= expect(totalTp == expectedTruePositives, "incorrect true-positive count");
  passed &= expect(totalAdapts == stats.adapts, "output adapts disagree with workload totals");
  passed &= expect(lastReplayCount == stats.adversarialQueries, "output replay count disagrees with workload totals");
  return passed;
}

using Workload = WorkloadStats (*)(FakeFilter&, FakeDb&, const BenchmarkParams&, FILE*);

WorkloadStats run(
    Workload workload, FakeFilter& filter, FakeDb& db,
    const BenchmarkParams& params, bool& passed, uint64_t truePositives = 0,
    const std::vector<uint64_t>& expectedRoundFp = {}) {
  FILE* output = tmpfile();
  if (!output) {
    perror("tmpfile");
    exit(1);
  }
  WorkloadStats stats = workload(filter, db, params, output);
  passed &= check_round_output(output, params.numRounds, stats, truePositives, expectedRoundFp);
  fclose(output);
  return stats;
}

// Check that ordinary workloads preserve query order, skip database access for negatives,
// and adapt only confirmed false positives while recording the corresponding statistics.
bool test_standard() {
  bool passed = true;
  std::vector<uint64_t> queries{1, 7, 8, 1, 7, 9};
  FakeFilter filter;
  filter.positiveKeys = {1, 7};
  FakeDb db;
  db.presentKeys = {7};
  const auto stats = run(run_standard_workload, filter, db, parameters(queries), passed, 2);
  passed &= expect(filter.queries == queries, "standard workload must use the supplied queries");
  passed &= expect(db.lookups == std::vector<uint64_t>({1, 7, 1, 7}), "true negatives must skip the database");
  passed &= expect(filter.adaptCalls == std::vector<uint64_t>({1, 1}), "only false positives should adapt");
  passed &= expect(stats.falsePositives == 2 && stats.adapts == 0 && stats.adversarialQueries == 0,
      "incorrect standard workload totals");
#ifdef PERF
  passed &= expect(stats.fpFrequency.at(1) == 2 && stats.fpFrequency.count(7) == 0,
      "extra stats must count confirmed false positives only");
  passed &= expect(stats.dbMissLatencies.size() == 4 && stats.adaptLatencies.size() == 2,
      "extra stats must retain sampled lookup and adaptation latencies");
#endif
  return passed;
}

// Check the fixed adversary's FIFO replay order and removal of corrected candidates,
// including the exact database accesses and replay/adaptation totals.
bool test_fifo_and_retirement() {
  bool passed = true;
  std::vector<uint64_t> queries{1, 2, 9, 9, 9, 9, 9, 9};
  auto params = parameters(queries);
  params.adversarial_freq = 2;
  FakeFilter filter;
  filter.positiveKeys = {1, 2};
  filter.adaptAfter[1] = 2;
  FakeDb db;
  const auto stats = run(run_adversarial_workload, filter, db, params, passed);
  passed &= expect(filter.queries == std::vector<uint64_t>({1, 2, 1, 9, 2, 9, 1, 9}),
      "replay must rotate FIFO across rounds and drop a resolved candidate");
  passed &= expect(db.lookups == std::vector<uint64_t>({1, 2, 1, 2}),
      "a resolved replay must skip the database");
  passed &= expect(stats.falsePositives == 4 && stats.adapts == 1 && stats.adversarialQueries == 3,
      "incorrect replay or adaptation totals");
  return passed;
}

// Preserve the fixed adversary's historical handling of repeated observations in its deque.
// Check the resulting replay order and frequency; this pool intentionally permits duplicates.
bool test_repeated_observations() {
  bool passed = true;
  std::vector<uint64_t> queries{1, 1, 2, 9, 9, 9, 9, 9, 9, 9};
  auto params = parameters(queries);
  params.adversarial_freq = 3;
  FakeFilter filter;
  filter.positiveKeys = {1, 2};
  FakeDb db;
  const auto stats = run(run_adversarial_workload, filter, db, params, passed);
  passed &= expect(filter.queries == std::vector<uint64_t>({1, 1, 2, 1, 9, 9, 1, 9, 9, 2}),
      "the deque must retain repeated observations without a membership set");
  passed &= expect(stats.adversarialQueries == 3, "incorrect repeat frequency");
  return passed;
}

// Ensure a filter-positive query found in the database never enters the replay pool.
// Only confirmed false positives may become adversarial candidates.
bool test_true_positives_not_admitted() {
  bool passed = true;
  std::vector<uint64_t> queries{7, 1, 9, 9};
  FakeFilter filter;
  filter.positiveKeys = {1, 7};
  FakeDb db;
  db.presentKeys = {7};
  const auto stats = run(run_adversarial_workload, filter, db, parameters(queries), passed, 1);
  passed &= expect(filter.queries == std::vector<uint64_t>({7, 1, 1, 1}),
      "database hits must not become replay candidates");
  passed &= expect(stats.adversarialQueries == 2, "incorrect replay count after database hit");
  return passed;
}

// Check both uniform-first and adversarial-first schedules against exact query sequences,
// so phase boundaries and replay counts remain unchanged during benchmark refactoring.
bool test_phases() {
  bool passed = true;
  std::vector<uint64_t> queries{1, 9, 2, 9, 9, 9, 9, 9};
  auto params = parameters(queries);
  params.numRounds = 4;
  FakeFilter filter;
  filter.positiveKeys = {1, 2};
  FakeDb db;
  const auto stats = run(run_phased_adversarial_workload, filter, db, params, passed);
  passed &= expect(filter.queries == std::vector<uint64_t>({1, 9, 2, 9, 1, 2, 1, 2}),
      "uniform phases must collect candidates for subsequent replay rounds");
  passed &= expect(stats.adversarialQueries == 4, "phased workload switched at the wrong round");

  params.startWithAdversarialPhase = true;
  params.numPhases = 4;
  FakeFilter startingAdversarial;
  startingAdversarial.positiveKeys = {1, 2};
  FakeDb otherDb;
  const auto otherStats = run(run_phased_adversarial_workload, startingAdversarial, otherDb, params, passed);
  passed &= expect(startingAdversarial.queries == std::vector<uint64_t>({1, 1, 2, 9, 1, 2, 9, 9}),
      "adversarial-first phases must preserve the queue across uniform phases");
  passed &= expect(otherStats.adversarialQueries == 3, "incorrect adversarial-first replay count");
  return passed;
}

// Check that reporting intervals cover every query when the budget is not divisible
// by the number of rounds, with correct boundaries and per-round false-positive counts.
bool test_uneven_measurements() {
  bool passed = true;
  std::vector<uint64_t> queries{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  auto params = parameters(queries);
  params.numRounds = 4;
  params.adversarial_freq = 100;
  for (Workload workload : {run_standard_workload<FakeFilter, FakeDb>,
                           run_adversarial_workload<FakeFilter, FakeDb>,
                           run_phased_adversarial_workload<FakeFilter, FakeDb>,
                           run_adaptive_adversarial_workload<FakeFilter, FakeDb>}) {
    FakeFilter filter;
    filter.positiveKeys.insert(queries.begin(), queries.end());
    FakeDb db;
    const auto stats = run(workload, filter, db, params, passed, 0, {2, 3, 2, 3});
    passed &= expect(filter.queries == queries, "uneven intervals must process every supplied query");
    passed &= expect(filter.measurementQueryCounts == std::vector<uint64_t>({2, 5, 7, 10}),
        "measurements must evenly cover the complete query stream");
    passed &= expect(stats.falsePositives == 10, "the last measurement must include remaining queries");
  }
  return passed;
}

// Check the minimum and maximum reporting frequencies without changing query execution.
// Verify boundary arithmetic also handles query counts near the uint64_t limit.
bool test_measurement_limits() {
  bool passed = true;
  std::vector<uint64_t> queries{1, 2, 3, 4};
  for (uint64_t numRounds : {1, 4}) {
    auto params = parameters(queries);
    params.numRounds = numRounds;
    FakeFilter filter;
    filter.positiveKeys = {1, 2, 3, 4};
    FakeDb db;
    const auto stats = run(run_standard_workload, filter, db, params, passed);
    const std::vector<uint64_t> expectedBoundaries = numRounds == 1
        ? std::vector<uint64_t>{4} : std::vector<uint64_t>{1, 2, 3, 4};
    passed &= expect(filter.measurementQueryCounts == expectedBoundaries,
        "single-interval and one-query-per-interval workloads must report at the right endpoints");
    passed &= expect(stats.falsePositives == 4, "measurement count must not change query count");
  }
  passed &= expect(query_boundary(UINT64_MAX, 4, 3) == UINT64_MAX - UINT64_MAX / 4 - 1,
      "large query counts must not overflow boundary calculations");
  passed &= expect(query_boundary(UINT64_MAX, 4, 4) == UINT64_MAX,
      "the final query boundary must include the complete stream");
  return passed;
}

// Check both phase orders with uneven query partitions, including the trailing queries.
// Measurement boundaries must not alter the intended phase-specific replay sequence.
bool test_uneven_phases() {
  bool passed = true;
  std::vector<uint64_t> queries{1, 9, 2, 9, 9, 9, 9, 9, 9, 9};
  auto params = parameters(queries);
  params.numRounds = 4;
  FakeFilter filter;
  filter.positiveKeys = {1, 2};
  FakeDb db;
  const auto stats = run(run_phased_adversarial_workload, filter, db, params, passed, 0, {1, 1, 2, 3});
  passed &= expect(filter.queries == std::vector<uint64_t>({1, 9, 2, 9, 9, 1, 2, 1, 2, 1}),
      "phase changes must occur at query-range boundaries, including uneven intervals");
  passed &= expect(filter.measurementQueryCounts == std::vector<uint64_t>({2, 5, 7, 10}),
      "phase boundaries must align with measurement boundaries");
  passed &= expect(stats.adversarialQueries == 5, "an uneven adversarial phase must execute all its queries");

  params.startWithAdversarialPhase = true;
  FakeFilter otherFilter;
  otherFilter.positiveKeys = {1, 2};
  FakeDb otherDb;
  run(run_phased_adversarial_workload, otherFilter, otherDb, params, passed, 0, {2, 3, 0, 0});
  passed &= expect(otherFilter.queries == std::vector<uint64_t>({1, 1, 1, 1, 1, 9, 9, 9, 9, 9}),
      "adversarial-first execution must switch at the same query boundary");
  return passed;
}

// Verify the filter-only microbenchmark queries the intended inserted and workload keys.
// It must not call adaptation, and its elapsed-time measurements must be nonnegative.
bool test_microbenchmark_workload() {
  std::vector<uint64_t> inserts = {1, 2, 3};
  std::vector<uint64_t> queries = {1, 9, 10, 11};
  auto params = parameters(queries);
  params.insertSet = inserts.data();
  params.numInserts = inserts.size();
  FakeFilter filter;
  filter.positiveKeys = {1, 2, 3};
  const auto stats = run_microbenchmark_workload(filter, params);
  std::unordered_map<uint64_t, uint64_t> counts;
  for (uint64_t key : filter.queries) counts[key]++;
  bool passed = expect(counts[1] == 3 && counts[2] == 2 && counts[3] == 2 &&
      counts[9] == 1 && counts[10] == 1 && counts[11] == 1,
      "microbenchmarks must warm up and measure inserts, then query the workload once");
  passed &= expect(filter.adaptCalls.empty(), "filter-only microbenchmarks must not adapt");
  passed &= expect(stats.trueQueryUs >= 0 && stats.queryUs >= 0,
      "microbenchmark timings must be nonnegative");
  return passed;
}

} // namespace

int main() {
  srand(1);
  bool passed = true;
  passed &= test_microbenchmark_workload();
  passed &= test_standard();
  passed &= test_fifo_and_retirement();
  passed &= test_repeated_observations();
  passed &= test_true_positives_not_admitted();
  passed &= test_phases();
  passed &= test_uneven_measurements();
  passed &= test_measurement_limits();
  passed &= test_uneven_phases();
  return passed ? 0 : 1;
}
