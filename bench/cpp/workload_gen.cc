#include <fstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <iostream>
#include <stdio.h>
#include <queue>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <random>

#include "cxxopts.hpp"
#include "cpp_random_distributions/zipfian_int_distribution.h"

extern "C" {
#include "include/rand_util.h"
#include "include/hashutil.h"
}

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

int saveKeySet(const char* name, const uint64_t* keys, uint64_t count) {
  const int fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd == -1) { perror(name); return -1; }
  auto writeAll = [fd](const void* data, size_t bytes) {
    const char* cursor = static_cast<const char*>(data);
    while (bytes > 0) {
      const ssize_t written = write(fd, cursor, bytes);
      if (written < 0 && errno == EINTR) continue;
      if (written <= 0) return false;
      cursor += written;
      bytes -= written;
    }
    return true;
  };
  const bool ok = writeAll(&count, sizeof(count))
      && writeAll(keys, count * sizeof(uint64_t));
  if (!ok) perror(name);
  const int closeResult = close(fd);
  return ok && closeResult == 0 ? 0 : -1;
}

int saveWorkloadToFile(uint64_t* insertSet, uint64_t numInserts,
    uint64_t* querySet, uint64_t numQueries) {
  if (saveKeySet("insertSet", insertSet, numInserts) != 0) return -1;
  return saveKeySet("querySet", querySet, numQueries);
}

int saveWorkloadStatsToFile(uint64_t *querySet, uint64_t numQueries) {
  uint64_t minQuery = -1;
  uint64_t maxQuery = 0;
  std::map<uint64_t, uint64_t> freqMap;
  printf("Parsing query stats\n");
  for (uint64_t i=0; i <numQueries; i++) {
    if (i%1000000==0) printProgressBar(i, numQueries);
    freqMap[querySet[i]]++;
    minQuery = std::min(minQuery, querySet[i]);
    maxQuery = std::max(maxQuery, querySet[i]);
  }
  std::vector<std::pair<uint64_t, uint64_t>> distMap;
  std::vector<uint64_t> freq;
  auto it = freqMap.begin();
  while (it != freqMap.end()) {
    distMap.push_back(std::pair<uint64_t, uint64_t>(it->second, it->first));
    freq.push_back(it->second);
    it++;
  }
  sort(distMap.begin(), distMap.end());
  reverse(distMap.begin(), distMap.end());
  {
    FILE *fd = fopen("queryStats", "w");
    fprintf(fd, "min: %lu max: %lu\n", minQuery, maxQuery);
    fprintf(fd, "uniqueQueries: %lu\n", distMap.size());
    fprintf(fd, "Top 10 most frequent queries\n");
    for (uint64_t i=0; i < 10 && i < distMap.size(); i++) {
      fprintf(fd, "num: %lu freq: %lu\n", distMap[i].second, distMap[i].first);
    }
    fprintf(fd, "Top 10 least frequent queries\n");
    for (uint64_t i=distMap.size()-1; i>=0  && i>distMap.size()-10; i--) {
      fprintf(fd, "num: %lu freq: %lu\n", distMap[i].second, distMap[i].first);
    }
    fclose(fd);
  }
  {
    sort(freq.begin(), freq.end());
    FILE *fd = fopen("rankFreq", "w");
    fprintf(fd, "rank freq\n");

    uint64_t numPoints = 300;
    uint64_t skip = freq.size() / 300;
    for (uint64_t i=0; i < freq.size(); i+=skip) {
      fprintf(fd, "%lu %lu\n", i+1, freq[i]);
      if (i >= freq.size() - 300) break;
    }
    for (uint64_t i=freq.size()-300; i < freq.size(); i++) {
      fprintf(fd, "%lu %lu\n", i+1, freq[i]);
    }
    fprintf(fd, "%lu %lu\n", freq.size(), freq[freq.size()-1]);
    fclose(fd);
  }

  return 0;
}

int main(int argc, char **argv) {
  cxxopts::Options options("Bench adaptive filter variants");

  options.add_options()("singletonFraction", "Fraction of distinct keys occurring once", cxxopts::value<double>()->default_value("0.5"))("frequencyStats", "Save frequency statistics", cxxopts::value<bool>()->default_value("false"))(
      "seed",
      "RandSeed",
      cxxopts::value<uint64_t>()->default_value("0"))(

      "q,quotient",
      "Quotient bits to use in filter",
      cxxopts::value<int>()->default_value("22"))(

      "c,zipfianConstant",
      "Zipfian Constant",
      cxxopts::value<float>()->default_value("1.2"))(

      "r,remainder",
      "Remainder bits to use in filter",
      cxxopts::value<int>()->default_value("8"))(

      "hashAgainForZipfian",
      "hashAgainForZipfian",
      cxxopts::value<bool>()->default_value("false"))(

      "numRounds",
      "Number of rounds",
      cxxopts::value<int>()->default_value("100"))(


      "queryWorkload",
      "uniform, false-positive, zipfian",
      cxxopts::value<std::string>()->default_value("uniform"))(

      "numQueries",
      "Number of total queries ",
      cxxopts::value<uint64_t>()->default_value("20000"));



  auto result = options.parse(argc, argv);
  int qbits = result["q"].as<int>();
  int rbits = result["r"].as<int>();
  float zipfConstant = result["c"].as<float>();
  int numRounds = result["numRounds"].as<int>();
  uint64_t randSeed = result["seed"].as<uint64_t>();
  uint64_t numQueries = result["numQueries"].as<uint64_t>();
  std::string queryWorkload = result["queryWorkload"].as<std::string>();
  size_t numInserts = (1ull << qbits) * 0.9f; // strtoull(argv[3], NULL, 10);
  bool hashAgainForZipfian = result["hashAgainForZipfian"].as<bool>();
  std::mt19937_64 keyGenerator(randSeed);
  auto fillKeys = [&keyGenerator](uint64_t* keys, uint64_t count) {
    for (uint64_t i = 0; i < count; ++i) keys[i] = keyGenerator();
  };
  std::cout<<"HashAgainForZipfian: "<<hashAgainForZipfian<<std::endl;

  uint64_t *insertSet = (uint64_t *)malloc(numInserts * sizeof(uint64_t));
  uint64_t *querySet = (uint64_t *)malloc(numQueries * sizeof(uint64_t));
  uint64_t minirun_bitmask = (1ULL << (qbits + rbits)) - 1;

  std::cout<<"Allocated Memory "<<std::endl;


  if (queryWorkload == "singleton-mixture") {
    const double f = result["singletonFraction"].as<double>();
    if (!std::isfinite(f) || f < 0 || f > 1) return 2;
    auto mix = [randSeed](uint64_t x) {
      x += randSeed + 0x9e3779b97f4a7c15ULL;
      x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
      x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
      return x ^ (x >> 31);
    };
    for (uint64_t i=0;i<numInserts;++i) insertSet[i]=mix(i);
    const uint64_t doubles = std::min(numQueries/2, static_cast<uint64_t>(std::llround(numQueries*(1-f)/(2-f))));
    const uint64_t singles = numQueries-2*doubles;
    uint64_t pos=0;
    for(uint64_t i=0;i<singles+doubles;++i) {
      const uint64_t key=mix(numInserts+i);
      querySet[pos++]=key;
      if(i>=singles) querySet[pos++]=key;
    }
    std::mt19937_64 shuffleGenerator(randSeed ^ 0xd1b54a32d192ed03ULL);
    std::shuffle(querySet,querySet+numQueries,shuffleGenerator);
  } else if (queryWorkload == "false-positive") {
    fillKeys(insertSet, numInserts);
    fillKeys(querySet, numQueries);
    uint64_t numQueriesPerRound = numQueries / numRounds;
    for (int r = 0; r < numRounds; r++) {
      for (uint64_t i = 0; i < numQueriesPerRound; i++) {
        // Create a random key having a fingerprint from the insert set.
        uint64_t queryIdx = r * numQueriesPerRound + i;
        querySet[queryIdx] = querySet[queryIdx] << (qbits + rbits);
        querySet[queryIdx] = querySet[queryIdx] | ((insertSet[queryIdx % numInserts]) & minirun_bitmask);
      }
    }
  } else if (queryWorkload == "normal") {
    uint64_t numUniqueQueries = numQueries/5;
    fillKeys(insertSet, numInserts);
    std::mt19937_64 gen(randSeed);
    std::normal_distribution<double> dist(0.0, 1.0);
    std::vector<double> weights;
    double minWeight = std::numeric_limits<double>::max();
    for (uint64_t i=0; i < numUniqueQueries; i++) {
      weights.push_back(dist(gen));
      minWeight = std::min(weights[i], minWeight);
    }
    for (uint64_t i=0; i<numUniqueQueries; i++) {
      weights[i] -= minWeight - 0.001;
    }
    std::discrete_distribution<> normalDist(weights.begin(), weights.end());
    for (uint64_t i=0; i < numQueries; i++) {
      querySet[i] = normalDist(gen);
      querySet[i] = MurmurHash64A((void*)(&querySet[i]), sizeof(querySet[i]), randSeed);
    }
  } else if (queryWorkload == "lognormal") {
    uint64_t numUniqueQueries = numQueries/5;
    fillKeys(insertSet, numInserts);
    std::mt19937_64 gen(randSeed);
    std::lognormal_distribution<double> dist(0.0, 1.0);
    std::vector<double> weights;
    double minWeight = std::numeric_limits<double>::max();
    for (uint64_t i=0; i < numUniqueQueries; i++) {
      weights.push_back(dist(gen));
      minWeight = std::min(weights[i], minWeight);
    }
    for (uint64_t i=0; i<numUniqueQueries; i++) {
      weights.push_back(dist(gen));
    }
    std::discrete_distribution<> normalDist(weights.begin(), weights.end());
    for (uint64_t i=0; i < numQueries; i++) {
      querySet[i] = normalDist(gen);
      querySet[i] = MurmurHash64A((void*)(&querySet[i]), sizeof(querySet[i]), randSeed);
    }
  } else if (queryWorkload == "zipfian") {
    std::cout<<"Generating insert set..."<<std::endl;
    fillKeys(insertSet, numInserts);
    std::cout<<"Generated insert set!"<<std::endl;
    std::cout<<"Generating query set"<<std::endl;
    std::mt19937_64 generator(randSeed);
    zipfian_int_distribution<uint64_t> distribution((uint64_t)0, numQueries, zipfConstant);

    for (uint64_t i=0; i < numQueries; i++) {
      if (i % 1000000 == 0) printProgressBar(i, numQueries);
      querySet[i] = distribution(generator);
      if (hashAgainForZipfian) {
        querySet[i] = MurmurHash64A((void*)(&querySet[i]), sizeof(querySet[i]), randSeed);
      }     
    }
  } else {
    printf("Going to generate insertSet\n");
    fillKeys(insertSet, numInserts);

    uint64_t temp = numQueries;
    uint64_t offset = 0;
    uint64_t block_size = 10000000;
    printf("\n");
    while (temp > 0) {
      uint64_t numElements = (temp > block_size) ? block_size : temp;
      fillKeys(querySet + offset, numElements);
      offset = offset + numElements;
      temp = temp - numElements;
      printf("%lu elements generated\n", offset);
    }
  } 

  if (saveWorkloadToFile(insertSet, numInserts, querySet, numQueries) != 0) return 1;
  if (result["frequencyStats"].as<bool>()) {
    std::sort(querySet, querySet+numQueries);
    std::map<uint64_t,uint64_t> histogram;
    uint64_t unique=0, singles=0, doubles=0;
    for(uint64_t i=0;i<numQueries;) {
      uint64_t j=i+1;
      while(j<numQueries && querySet[j]==querySet[i]) ++j;
      ++histogram[j-i]; ++unique;
      singles += j-i==1; doubles += j-i==2; i=j;
    }
    std::ofstream h("workload_frequencies.csv"); h << "frequency,distinct_keys\n";
    for(auto item:histogram) h << item.first << "," << item.second << "\n";
    std::ofstream out("workload_stats.json");
    out << std::setprecision(17) << "{\"total_queries\":" << numQueries
        << ",\"distinct_keys\":" << unique << ",\"singleton_keys\":" << singles
        << ",\"doubleton_keys\":" << doubles << ",\"singleton_key_fraction\":" << (unique?double(singles)/unique:0)
        << ",\"singleton_operation_fraction\":" << (numQueries?double(singles)/numQueries:0) << "}\n";
  }

  return 0;
}
