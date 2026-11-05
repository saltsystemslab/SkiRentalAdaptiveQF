#ifndef BENCHMARK_OUTPUT_HPP
#define BENCHMARK_OUTPUT_HPP

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

struct AdversaryEvent {
  uint64_t queryIndex;
  uint64_t key;
  bool replay;
  const char* outcome;
  bool repeatBefore;
  bool repeatAfter;
  size_t activeCandidates;
};

// Called after the measured workload, so trace serialization adds no disk I/O
// to the adversary's ordinary query stream.
inline void write_adversary_events(
    const std::string& path, const std::vector<AdversaryEvent>& events) {
  FILE* file = fopen(path.c_str(), "w");
  if (!file) throw std::runtime_error("Cannot open adversary trace: " + path);
  fprintf(file, "query_index,key,source,outcome,repeat_before,repeat_after,active_candidates\n");
  for (const auto& event : events) {
    fprintf(file, "%lu,%lu,%s,%s,%d,%d,%zu\n", event.queryIndex, event.key,
        event.replay ? "replay" : "fresh", event.outcome,
        event.repeatBefore, event.repeatAfter, event.activeCandidates);
  }
  const bool failed = ferror(file);
  const int closed = fclose(file);
  if (failed || closed) throw std::runtime_error("Cannot write adversary trace: " + path);
}

inline void printProgressBar(uint64_t current, uint64_t total, int barWidth = 50) {
  const double progress = total ? static_cast<double>(current) / total : 0;
  const int pos = barWidth * progress;
  std::cout << "[";
  for (int i = 0; i < barWidth; ++i) {
    if (i < pos) std::cout << "=";
    else if (i == pos) std::cout << ">";
    else std::cout << " ";
  }
  std::cout << "] " << int(progress * 100.0) << "%\r";
  std::cout.flush();
}

inline void write_latency_row(FILE* file, const char* metric, std::vector<uint64_t>& samples) {
  if (samples.empty()) {
    fprintf(file, "%s,0,0,0,0,0,0,0\n", metric);
    return;
  }
  std::sort(samples.begin(), samples.end());
  const size_t p50 = samples.size() / 2;
  const size_t p99 = samples.size() * 0.99;
  const size_t p9999 = samples.size() * 0.9999;
  double average = 0;
  for (uint64_t sample : samples) {
    average += static_cast<double>(sample) / samples.size();
  }
  fprintf(file, "%s,%zu,%lu,%lu,%lu,%lu,%lu,%f\n", metric, samples.size(),
      samples.front(), samples[p50], samples[p99], samples[p9999], samples.back(), average);
}

inline void write_latencies_to_file(
    const std::string& output_file_name, std::vector<uint64_t> fp_miss_latencies,
    std::vector<uint64_t> adapt_latencies) {
  FILE* latency_file = fopen(output_file_name.c_str(), "w");
  fprintf(latency_file, "metric,numSamples,min,50p,99p,99.99p,max,avg\n");
  write_latency_row(latency_file, "DbMiss", fp_miss_latencies);
  write_latency_row(latency_file, "Adapt", adapt_latencies);
  fclose(latency_file);
}

inline void write_microbench_to_file(std::string output_file_name, uint64_t numInserts, uint64_t tpTimeUs, uint64_t numQueries, uint64_t queryTimeUs, uint64_t filterInsertUs, uint64_t systemInsertUs, double load_factor, double fpr, uint64_t sizeBytes) {
  FILE *microbench_file = fopen(output_file_name.c_str(), "w");
  fprintf(microbench_file,"Load Factor:%lf\nFPR:%lf\n", load_factor, fpr);
  fprintf(microbench_file,"NumInserts: %lu\nFilterInsertTime(us): %lu\nFilterInsert Thput:%lf\n", numInserts, filterInsertUs, (1.0*numInserts)/filterInsertUs);
  fprintf(microbench_file,"SystemInsertTime(us): %lu\nSystemInsert Thput:%lf\n", systemInsertUs, (1.0*numInserts)/systemInsertUs);
  fprintf(microbench_file,"True Queries: %lu\nTrue Queries(us): %lu\nTrue Queries Thput: %lf\n", numInserts, tpTimeUs, (1.0*numInserts)/(tpTimeUs));
  fprintf(microbench_file,"Queries: %lu\nQueries(us): %lu\nQueries Thput: %lf\n", numQueries, queryTimeUs, (1.0*numQueries)/(queryTimeUs));
  fprintf(microbench_file,"Size(B): %lu Size(MB): %lf\n", sizeBytes, sizeBytes/(1024.0 * 1024.0));
  fclose(microbench_file);
}

inline void write_fp_stats_to_file(std::string output_file_name, std::unordered_map<uint64_t, uint64_t> fp_freq) {
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

#endif
