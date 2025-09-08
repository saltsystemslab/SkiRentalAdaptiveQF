#include <chrono>
#include <iostream>
#include <stdio.h>
#include <queue>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <openssl/rand.h>
#include <random>

#include "cxxopts.hpp"

extern "C" {
#include "include/rand_util.h"
#include "include/hashutil.h"
}

int saveWorkloadToFile(uint64_t *insertSet, uint64_t numInserts, uint64_t *querySet, uint64_t numQueries) {
  {
    int fd = open("insertSet", O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
      perror("Failed to write insertSet");
      return -1;
    }
    ssize_t bytesToWrite = (1 + numInserts) * sizeof(uint64_t) ;
    ssize_t offset = pwrite(fd, &numInserts, sizeof(uint64_t), 0);
    char *buf = (char *)insertSet;
    while (offset < bytesToWrite) {
      ssize_t bytesWritten = pwrite(fd, buf + offset, bytesToWrite-offset, offset);
      offset += bytesWritten; 
      printf("bytes written: %lu\n", bytesWritten);
    }
    close(fd);
  }

  {
    int fd = open("querySet", O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
      perror("Failed to write querySet");
      return -1;
    }
    ssize_t bytesToWrite = (1 + numQueries) * sizeof(uint64_t) ;
    ssize_t offset = pwrite(fd, &numQueries, sizeof(uint64_t), 0);
    char *buf = (char *)querySet;
    while (offset < bytesToWrite) {
      ssize_t bytesWritten = pwrite(fd, buf + offset, bytesToWrite-offset, offset);
      offset += bytesWritten; 
      printf("bytes written: %lu bytesToWrite %lu %lu\n", bytesWritten, offset, bytesToWrite);
    }
    close(fd);
  }
  return 0;
}

int saveWorkloadStatsToFile(uint64_t *querySet, uint64_t numQueries) {
  uint64_t minQuery = -1;
  uint64_t maxQuery = 0;
  std::map<uint64_t, uint64_t> freqMap;
  for (uint64_t i=0; i <numQueries; i++) {
    freqMap[querySet[i]]++;
    minQuery = std::min(minQuery, querySet[i]);
    maxQuery = std::max(maxQuery, querySet[i]);
  }
  std::vector<std::pair<uint64_t, uint64_t>> distMap;
  auto it = freqMap.begin();
  while (it != freqMap.end()) {
    distMap.push_back(std::pair<uint64_t, uint64_t>(it->second, it->first));
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
  return 0;
}

int main(int argc, char **argv) {
  cxxopts::Options options("Bench adaptive filter variants");

  options.add_options()(
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
  std::cout<<"HashAgainForZipfian: "<<hashAgainForZipfian<<std::endl;

  uint64_t *insertSet = (uint64_t *)malloc(numInserts * sizeof(uint64_t));
  uint64_t *querySet = (uint64_t *)malloc(numQueries * sizeof(uint64_t));
  uint64_t minirun_bitmask = (1ULL << (qbits + rbits)) - 1;

  std::cout<<"Allocated Memory "<<std::endl;


  if (queryWorkload == "false-positive") {
    RAND_bytes((unsigned char *)insertSet, numInserts * sizeof(uint64_t));
    RAND_bytes((unsigned char *)querySet, numQueries * sizeof(uint64_t));
    uint64_t numQueriesPerRound = numQueries / numRounds;
    for (int r = 0; r < numRounds; r++) {
      for (uint64_t i = 0; i < numQueriesPerRound; i++) {
        // Create a random key having a fingerprint from the insert set.
        uint64_t queryIdx = r * numQueriesPerRound + i;
        querySet[queryIdx] = querySet[queryIdx] << (qbits + rbits);
        querySet[queryIdx] = querySet[queryIdx] | ((insertSet[queryIdx % numInserts]) & minirun_bitmask);
      }
    }
  } else if (queryWorkload == "uniform" || queryWorkload == "adversarial") {
    printf("Going to generate insertSet\n");
    RAND_bytes((unsigned char *)insertSet, numInserts * sizeof(uint64_t));

    uint64_t temp = numQueries;
    uint64_t offset = 0;
    uint64_t block_size = 10000000;
    printf("\n");
    while (temp > 0) {
      uint64_t numElements = (temp > block_size) ? block_size : temp;
      RAND_bytes((unsigned char *)(querySet + offset), numElements * sizeof(uint64_t));
      offset = offset + numElements;
      temp = temp - numElements;
      printf("%lu elements generated\n", offset);
    }
  } else if (queryWorkload == "normal") {
    uint64_t numUniqueQueries = numQueries/5;
    RAND_bytes((unsigned char *)insertSet, numInserts * sizeof(uint64_t));
    std::random_device rd;
    std::mt19937 gen;
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
    RAND_bytes((unsigned char *)insertSet, numInserts * sizeof(uint64_t));
    std::random_device rd;
    std::mt19937 gen;
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
    RAND_bytes((unsigned char *)insertSet, numInserts * sizeof(uint64_t));
    std::cout<<"Generated insert set!"<<std::endl;
    std::random_device rd;
    std::mt19937 gen;
    std::uniform_int_distribution<uint64_t> dist;
    RAND_bytes((unsigned char *)querySet, numQueries * sizeof(uint64_t));
    uint64_t upd = numQueries /100;
    std::cout<<"Generating query set"<<std::endl;
    for (uint64_t i=0; i < numQueries; i++) {
      if (i % upd == 0) std::cout<< i/upd <<" \% done" << std::endl;
      querySet[i] = rand_zipfian(zipfConstant, 1ull << 63, dist(gen));
      if (hashAgainForZipfian) {
        querySet[i] = MurmurHash64A((void*)(&querySet[i]), sizeof(querySet[i]), randSeed);
      }     
    }
  } 

  saveWorkloadToFile(insertSet, numInserts, querySet, numQueries);
  // saveWorkloadStatsToFile(querySet, numQueries);

  return 0;
}
