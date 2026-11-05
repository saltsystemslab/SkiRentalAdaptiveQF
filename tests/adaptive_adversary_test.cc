#include "bench/cpp/benchmark_workloads.hpp"

#include <functional>
#include <iostream>
#include <memory>
#include <unordered_set>

struct Filter {
  std::unordered_set<uint64_t> positive;
  std::unordered_map<uint64_t, int> thresholds, occurrences;
  std::vector<uint64_t> queries, boundaries;
  std::function<int(size_t)> policy;
  size_t disappearOnQuery = 0;
  int fallback = 0;
  void queryFilter(uint64_t key, QFilterQueryResult* result) {
    queries.push_back(key);
    if (queries.size() == disappearOnQuery) positive.erase(key);
    result->key_present = positive.count(key);
  }
  int adapt(uint64_t key, QFilterQueryResult*) {
    const int seen = ++occurrences[key];
    const int result = policy ? policy(queries.size()) :
        (thresholds.count(key) && seen >= thresholds[key] ? 1 : fallback);
    if (result == 1) positive.erase(key);
    return result;
  }
  double loadFactor() { boundaries.push_back(queries.size()); return 0.9; }
};

struct Database {
  std::unordered_set<uint64_t> present;
  std::unordered_map<uint64_t, int> lookups;
  uint64_t becomesPresent = 0;
  int lookupKey(uint64_t key) {
    return present.count(key) || (++lookups[key] > 1 && key == becomesPresent);
  }
};

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

WorkloadStats run(std::vector<uint64_t> keys, Filter& filter, Database& db,
                  int frequency = 1, uint64_t rounds = 1) {
  BenchmarkParams params{};
  params.querySet = keys.data(); params.numQueries = keys.size();
  params.numRounds = rounds; params.adversarial_freq = frequency;
  std::unique_ptr<FILE, decltype(&fclose)> output(tmpfile(), fclose);
  require(bool(output), "Cannot open temporary rounds file");
  auto stats = run_adaptive_adversarial_workload(filter, db, params, output.get());
  rewind(output.get());
  char line[512];
  uint64_t count = 0, fpSum = 0, adaptSum = 0, lastReplay = 0;
  while (fgets(line, sizeof(line), output.get())) {
    unsigned long round, fp, tp, cumulativeFp, replay, adapts, cumulativeAdapts;
    double throughput, cumulativeThroughput, load;
    require(sscanf(line, "%lu %lf %lu %lu %lf %lu %lu %lu %lu %lf", &round,
        &throughput, &fp, &tp, &cumulativeThroughput, &cumulativeFp, &replay,
        &adapts, &cumulativeAdapts, &load) == 10, "Malformed round output");
    fpSum += fp; adaptSum += adapts; lastReplay = replay;
    require(round == count++, "Wrong round index");
    require(cumulativeFp == fpSum && cumulativeAdapts == adaptSum, "Round totals disagree");
  }
  require(count == rounds && fpSum == stats.falsePositives && adaptSum == stats.adapts
      && lastReplay == stats.adversarialQueries, "Workload accounting disagrees");
  require(filter.queries.size() == keys.size(), "Queries escaped the requested budget");
  return stats;
}

// Use scripted corrections to check that feedback changes the very next eligible query.
// Replays preserve the fresh-query cursor, and reporting frequency cannot change execution.
void immediate_feedback_and_cursor() {
  for (uint64_t rounds : {1, 2, 4, 8}) {
    Filter filter; Database db;
    filter.positive = {1, 2, 3}; filter.thresholds = {{1, 2}, {2, 1}, {3, 2}};
    auto stats = run({1, 2, 3, 4, 5, 6, 7, 8}, filter, db, 1, rounds);
    require(filter.queries == std::vector<uint64_t>({1, 1, 2, 3, 3, 4, 5, 6}),
        "Feedback must affect the next query and preserve the fresh cursor");
    require(stats.falsePositives == 5 && stats.adapts == 3 && stats.adversarialQueries == 2,
        "Incorrect second-occurrence/always-adapting counts");
    require(stats.adversaryEvents[0].repeatAfter && !stats.adversaryEvents[1].repeatAfter
        && stats.adversaryEvents[1].activeCandidates == 0, "Correction was not retired immediately");
    for (size_t i = 0; i < rounds; ++i)
      require(filter.boundaries[i] == 8 * (i + 1) / rounds, "Reporting changed query execution");
  }
}

// Check that a filter correcting every false positive keeps replay disabled throughout.
// Corrected keys must never be queued or require extra confirmation probes.
void always_adapts() {
  Filter filter; Database db;
  filter.positive = {1, 2, 3, 4}; filter.policy = [](size_t) { return 1; };
  auto stats = run({1, 2, 3, 4}, filter, db);
  require(filter.queries == std::vector<uint64_t>({1, 2, 3, 4}) && !stats.adversarialQueries,
      "Immediate corrections must keep replay disabled without confirmation probes");
  for (const auto& e : stats.adversaryEvents)
    require(!e.repeatBefore && !e.repeatAfter && !e.activeCandidates, "Corrected key was queued");
}

// Check replay-slot spacing and FIFO rotation while the filter tolerates false positives.
// Repeated observations must be deduplicated, and ordinary negatives must retain the replay flag.
void tolerance_and_fifo() {
  Filter filter; Database db; filter.positive = {1, 2};
  auto stats = run({1, 1, 2, 9, 9, 9, 9, 9, 9, 9}, filter, db, 3);
  require(filter.queries == std::vector<uint64_t>({1, 1, 2, 1, 9, 9, 2, 9, 9, 1}),
      "FIFO rotation, replay slots, or negative-response flag retention is wrong");
  require(stats.adversaryEvents[1].activeCandidates == 1
      && stats.adversaryEvents[2].activeCandidates == 2, "Candidate pool must deduplicate keys");
  require(stats.adversarialQueries == 3, "Unexpected extra probes");
}

// Retire a replay candidate that becomes a filter negative or a true database hit.
// An empty pool falls back to fresh queries without changing the last observed policy flag.
void negative_and_present_retirement() {
  for (bool present : {false, true}) {
    Filter filter; Database db; filter.positive = {1};
    if (present) db.becomesPresent = 1; else filter.disappearOnQuery = 2;
    auto stats = run({1, 2, 3, 4}, filter, db);
    require(filter.queries == std::vector<uint64_t>({1, 1, 2, 3}), "Empty pool must fall back to fresh keys");
    const auto& e = stats.adversaryEvents[1];
    require(e.repeatBefore && e.repeatAfter && !e.activeCandidates,
        "Replay retirement must preserve the last policy observation");
  }
}

// Script a switch from tolerance to correction and back, and check the exact query sequence.
// The adversary must recognize both changes through ordinary query outcomes.
void delayed_switches() {
  Filter filter; Database db; filter.positive = {1, 2, 3, 4};
  filter.policy = [](size_t query) { return query >= 3 && query <= 5 ? 1 : 0; };
  auto stats = run({1, 2, 3, 4, 5, 6, 7, 8}, filter, db);
  require(filter.queries == std::vector<uint64_t>({1, 1, 1, 2, 3, 4, 4, 4}),
      "Both strategy changes must be recognized from ordinary queries");
  require(stats.adapts == 3 && stats.adversarialQueries == 4, "Wrong delayed-switch counts");
}

// Check that adaptation failure aborts with the offending query position.
// A failed correction must not be mistaken for a deliberate decision to tolerate.
void failure_is_explicit() {
  Filter filter; Database db; filter.positive = {1}; filter.fallback = -1;
  bool failed = false;
  try { run({1, 2}, filter, db); } catch (const std::runtime_error& e) {
    failed = std::string(e.what()).find("correction failed at query 1") != std::string::npos;
  }
  require(failed, "Correction failure must not be interpreted as tolerated policy");
}

int main() {
  try {
    immediate_feedback_and_cursor(); always_adapts(); tolerance_and_fifo();
    negative_and_present_retirement(); delayed_switches(); failure_is_explicit();
    std::cout << "Adaptive-adversary checks passed\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
