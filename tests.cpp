#include <iostream>
#include <string>
#include <vector>

#include <chrono>
#include <random>

#include <cassert>

#include "ttl_cache.hpp"
#include "dummy_cache.hpp"

/* sequence of operations to test the LRU mechanism.
   All the timestamps are set so no keys expire, so TTL does not interfere
   run with VERBOSE on in ttl_cache
*/
void LRU_testcase() {
  std::size_t maxEntries = 5;
  double loadFactor = 0.5;
  ttl_cache<std::string, std::string, std::hash<std::string>> cache(maxEntries, loadFactor, std::hash<std::string>());

  cache.get("key1", 1); //not found
  cache.insert("key1", "value1", 2, 100);
  cache.insert("key2", "value2", 3, 100);
  cache.insert("key3", "value3", 4, 100);
  cache.get("key2", 5);
  cache.insert("key4", "value4", 6, 100);
  cache.insert("key5", "value5", 7, 100);
  cache.get("key4", 8);
  cache.insert("key6", "value6", 9, 100); //kicks out 1

  cache.print();
  std::vector<std::string> expectedOrder = {"key3","key2","key5","key4","key6"};
  assert(cache.LRU_order() == expectedOrder);

  cache.insert("key7", "value7", 10, 100); //kicks out 3
  cache.insert("key8", "value8", 11, 100); //kicks out 2
  cache.insert("key9", "value9", 12, 100); //kicks out 5
  cache.get("key1", 13); //not found
  cache.get("key9", 14);
  cache.get("key8", 15);

  cache.print();
  expectedOrder = {"key4","key6","key7","key9","key8"};
  assert(cache.LRU_order() == expectedOrder);
}

/* sequence of operations to test the TTL mechanism and Expire algorithm
   run with VERBOSE on in ttl_cache (note: large output)
*/
void TTL_testcase() {
  std::size_t maxEntries = 100;
  double loadFactor = 0.5;
  ttl_cache<std::string, std::string, std::hash<std::string>> cache(maxEntries, loadFactor, std::hash<std::string>());

  for (int i = 1; i <= 100; i++) {
    cache.insert("key"+std::to_string(i), "value"+std::to_string(i), i, 102-i); //all expire at t:102
  }

  cache.print();

  cache.removeExpired(101, 0.5); //nothing expired yet, exits without removing anything

  cache.removeExpired(102, 0.5); //everything expired, should stop with <20 entries due to the load factor being too low

  cache.print();

  for (int i = 1; i <= 50; i++) {
    cache.insert("key"+std::to_string(i), "value"+std::to_string(i), 200+i, 102-i); //all expire at t:302
  }
  for (int i = 51; i <= 100; i++) {
    cache.insert("key"+std::to_string(i), "value"+std::to_string(i), 200+i, 103-i); //all expire at t:303
  }

  cache.print();

  cache.removeExpired(302, 0.1); //50 expired and 50 alive entries, so expired ratio is 0.5.
                                 //expired entries will be remove until this ratio is close to 0.1

  cache.print();

}

/* generates a high-volume sequence of operations
   with randomized parameters to test the cache under a variety of situations
   the results are compared against "dummy_cache", a trivial implementation of a cache
   run with VERBOSE off in ttl_cache!
*/
void automatedCorrectnessTest() {

  std::mt19937_64 RNG{static_cast<uint64_t> (std::chrono::steady_clock::now().time_since_epoch().count())};

  //parameters
  int numOperations = 1000000;
  int numDifferentValues = 1000000;
  int numUpdatesPerRun = 3;
  int numRuns = 10;

  for (int i = 0; i < numRuns; i++) {

    //parameters with randomized values
    int numFrequentKeys = 3 + RNG()%25;
    int numTotalKeys = numFrequentKeys + 1 + RNG()%1000;
    int freqToAllKeyRatio = 1 + RNG()%2;
    int minTimeStep = 1 + RNG()%2;
    int maxTimeStep = minTimeStep + 1 + RNG()%5;
    int minTTL = 1 + RNG()%5;
    int maxTTL = minTTL + RNG()%10000;
    std::size_t cacheMaxSize = numTotalKeys / (1+ RNG()%5);
    double loadFactor = 0.1 * (1 + RNG()%5);
    int readWriteRatio = 1 + RNG()%2; 

    std::cout<<">>>> Test with "<<numTotalKeys<<" keys, "<<cacheMaxSize<<" max cache size, "
             <<loadFactor<<" load factor"<<std::endl;

    //tested data structure and ground truth
    ttl_cache<int, int> cache(cacheMaxSize, loadFactor, std::hash<int>());
    dummy_cache<int, int> trueMap;

    //analytics, could add many more
    int hits = 0, misses = 0, noncached = 0;
    int numReads = 0, numWrites = 0;

    int currentTime = 0;
    for (int j = 0; j < numOperations; j++) {
      currentTime += minTimeStep + RNG()%(maxTimeStep - minTimeStep);

      int key;
      if (RNG()%(1+freqToAllKeyRatio) != 0) key = RNG()%numFrequentKeys;
      else key = RNG()%numTotalKeys;

      bool isInsert;
      if (RNG()%(1+readWriteRatio) != 0) isInsert = false;
      else isInsert = true;

      if (isInsert) {
        int value = RNG()%numDifferentValues;
        int ttl = minTTL + RNG()%(maxTTL-minTTL);

        trueMap.insert(key, value, currentTime, ttl);
        cache.insert(key, value, currentTime, ttl);
        numWrites++;

      } else {
        auto value = cache.get(key, currentTime);
        auto trueValue = trueMap.get(key, currentTime);
        if (value) {
          if (!trueValue or *value != *trueValue) {
            std::cout<<"wrong value in the cache"<<std::endl;
            std::cout<<"cache implementation is wrong"<<std::endl;            
            return;
          }
        }

        numReads++;
        if (!trueValue) noncached++;
        else if (!value) misses++;
        else hits++;

      }

      if (j%(numOperations/numUpdatesPerRun) == 0 or j == numOperations-1) {
        std::cout<<"current time: "<<currentTime<<std::endl;
        std::cout<<cache.size()<<" / "<<trueMap.size()<<" keys currently in ttl_cache / dummy_cache"<<std::endl;
        std::cout<<j+1<<" ops: "<<numWrites<<" writes, "<<numReads<<" reads ("<<hits<<" hits, "
                 <<misses<<" misses, "<<noncached<<" non-cached)"<<std::endl;
      }

    }
    std::cout<<">>>> cache passed the randomized test (match ratio: "
             <<hits/(double) (hits+misses)<<")"<<std::endl;
  }
}


int main() {
  // LRU_manualTest();
  TTL_testcase();
  // automatedCorrectnessTest();
}
