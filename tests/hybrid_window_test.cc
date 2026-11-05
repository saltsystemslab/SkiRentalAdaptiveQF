#include "hybrid_skiqf.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>

// A single inserted key gives exact reverse-map behavior without database I/O.
struct SingleKeyReverseMap {
  uint64_t original = 0;
  int init(std::string, int, int, int, bool, uint64_t) { return 0; }
  int insertFingerprint(uint64_t, int, uint64_t key) { original = key; return 0; }
  int insertAndCommitFingerprint(uint64_t fp, int rank, uint64_t key) {
    return insertFingerprint(fp, rank, key);
  }
  int getFingerprint(uint64_t, int, uint64_t* key) { *key = original; return 0; }
  void commitFingerprints() {}
  int close() { return 0; }
};

using Filter = HybridSkiQF<SingleKeyReverseMap>;
constexpr uint64_t original = 0x123456789abcdef0ULL;
constexpr uint64_t falsePositive = original ^ (1ULL << 20);
constexpr uint64_t trueNegative = original ^ (1ULL << 8);

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void initialize(Filter& filter, QFilterConfig config) {
  require(filter.construct(config) == 0, "Hybrid construction failed");
  uint64_t key = original;
  require(filter.bulkLoad(&key, 1) == 0, "Hybrid single-key load failed");
}

int query_false_positive(Filter& filter) {
  QFilterQueryResult result{};
  filter.queryFilter(falsePositive, &result);
  require(result.key_present, "Test query should still be a false positive");
  return filter.adapt(falsePositive, &result);
}

int main() {
  try {
    QFilterConfig config{12, 8, 2, 0.95, {4, 0, false, true}, 42};
    require(config.hybridWindowSize == 1'000'000, "Historical default changed");
    Filter historical;
    initialize(historical, config);
    for (int i = 0; i < 100'000; ++i)
      require(query_false_positive(historical) == 0, "Default detector switched before a million queries");
    historical.close();

    config.hybridWindowSize = 100'000;
    Filter medium;
    initialize(medium, config);
    for (int i = 0; i < 99'999; ++i)
      require(query_false_positive(medium) == 0, "Configured detector switched early");
    require(query_false_positive(medium) == 1, "Detector did not switch at query 100,000");
    QFilterQueryResult corrected{};
    medium.queryFilter(falsePositive, &corrected);
    require(!corrected.key_present, "Reported correction did not fix the query");
    medium.close();

    config.hybridWindowSize = 16;
    Filter mixed;
    initialize(mixed, config);
    for (int i = 0; i < 12; ++i)
      require(query_false_positive(mixed) == 0, "Mixed window switched before its boundary");
    for (int i = 0; i < 4; ++i) {
      QFilterQueryResult negative{};
      mixed.queryFilter(trueNegative, &negative);
      require(!negative.key_present, "Expected a filter rejection");
    }
    require(query_false_positive(mixed) == 1, "True negatives did not advance the detector window");
    mixed.close();

    config.hybridWindowSize = 0;
    Filter invalid;
    require(invalid.construct(config) == -1, "Zero-length detector window was accepted");
    std::cout << "Hybrid detector-window checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
