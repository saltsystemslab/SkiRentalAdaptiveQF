#ifndef BENCHMARK_FLAGS_HPP
#define BENCHMARK_FLAGS_HPP

#include <climits>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "cxxopts.hpp"
#include "benchmark_params.hpp"

inline void validateFlags(const BenchmarkParams& params, int advFreq) {
  if (params.qfConfig.breakEvenCount < 1) {
    throw std::invalid_argument("breakEven must be at least 1");
  }
  if (advFreq < 1 || advFreq > 100) {
    throw std::invalid_argument("advFreq must be between 1 and 100");
  }
  if (params.numRounds < 1 || params.numRounds > INT_MAX || params.numPhases < 1) {
    throw std::invalid_argument("numRounds and numPhases must be at least 1");
  }
  if (params.isPhasedTest &&
      (static_cast<uint64_t>(params.numPhases) > params.numRounds || params.numRounds % params.numPhases != 0)) {
    throw std::invalid_argument("Phased tests require numPhases <= numRounds and evenly dividing it");
  }
  if (params.isAdaptiveAdversary && params.adaptiveAdversaryWindowSize == 0) {
    throw std::invalid_argument("adaptiveAdversaryWindowSize must be positive");
  }
  if (params.isAdaptiveAdversary && params.adaptiveAdversaryWindowSize >= 1000000) {
    throw std::invalid_argument(
        "adaptiveAdversaryWindowSize must be smaller than HybridSkiQF's 1,000,000-query detection window");
  }
  if (params.isAdaptiveAdversary && params.isPhasedTest) {
    throw std::invalid_argument("adaptive-adversarial and phasedTest are separate modes");
  }
  const std::unordered_set<std::string> knownFilters = {
      "nonAdaptive", "adaptive", "skiQF", "randomizedSkiQF", "coinFlip", "hybridSkiQF"};
  if (!knownFilters.count(params.output_file)) {
    throw std::invalid_argument("Unknown filter: " + params.output_file);
  }
}

inline BenchmarkParams buildBenchmarkParams(const cxxopts::ParseResult& flags) {
  BenchmarkParams params{};
  params.qfConfig.qbits = flags["q"].as<int>();
  params.qfConfig.rbits = flags["r"].as<int>();
  params.qfConfig.breakEvenCount = flags["breakEven"].as<int>();
  params.qfConfig.max_load_factor = 0.95;
  params.numRounds = flags["numRounds"].as<int>();
  params.output_file = flags["filter"].as<std::string>();
  const auto workload = flags["queryWorkload"].as<std::string>();
  params.is_adversarial = workload == "adversarial";
  params.isAdaptiveAdversary = workload == "adaptive-adversarial";
  params.adaptiveAdversaryWindowSize = flags["adaptiveAdversaryWindowSize"].as<uint64_t>();
  params.isPhasedTest = flags["phasedTest"].as<bool>();
  params.numPhases = flags["numPhases"].as<int>();
  params.startWithAdversarialPhase = flags["startWithAdversarialPhase"].as<bool>();
  params.shouldCollectDbStats = flags["dbStats"].as<bool>();
  params.qfConfig.reverseMapConfig.shouldCollectDbStats = params.shouldCollectDbStats;
  params.storageCacheSizeMB = flags["storageCacheSizeMB"].as<uint64_t>();
  params.qfConfig.reverseMapConfig.cacheSizeMB = flags["reverseMapCacheSizeMB"].as<uint64_t>();
  params.storageSleepUs = flags["storageSleepUs"].as<uint64_t>();
  params.qfConfig.reverseMapConfig.sleepUs = flags["reverseSleepUs"].as<uint64_t>();
  params.qfConfig.randomSeed = static_cast<uint64_t>(flags["seed"].as<int>());
  params.shouldSort = !flags["microBench"].as<bool>() && flags["sortAndInsertKeys"].as<bool>();
  params.qfConfig.reverseMapConfig.sortAndInsertFingerprints = flags["sortAndInsertFingerprints"].as<bool>();
  const int advFreq = flags["advFreq"].as<int>();
  validateFlags(params, advFreq);
  params.adversarial_freq = 100 / advFreq;
  return params;
}

inline std::vector<uint64_t> read_key_set(const std::string& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    throw std::runtime_error("Failed to open key set: " + path);
  }
  const auto fileSize = file.tellg();
  uint64_t count = 0;
  file.seekg(0);
  file.read(reinterpret_cast<char*>(&count), sizeof(count));
  if (!file || fileSize < static_cast<std::streamoff>(sizeof(count)) ||
      (fileSize - sizeof(count)) % sizeof(uint64_t) != 0 ||
      static_cast<uint64_t>((fileSize - sizeof(count)) / sizeof(uint64_t)) != count) {
    throw std::runtime_error("Invalid key set: " + path);
  }
  std::vector<uint64_t> keys(count);
  file.read(reinterpret_cast<char*>(keys.data()), count * sizeof(uint64_t));
  if (!file) {
    throw std::runtime_error("Failed to read key set: " + path);
  }
  return keys;
}

#endif
