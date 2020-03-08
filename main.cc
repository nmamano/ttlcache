#include <iostream>
#include <string>

#include <random> //for randomized testing
#include <ctime> //random seed for testing
#include <unordered_map> //to compare against in testing

#include "ttl_cache.hpp"
#include "dummy_cache.hpp"

void LRU_manualTest() {
  //remember to turn on VERBOSE
  //This only tests LRU. All the timestamps are set so no keys expire 
  ttl_cache<std::string, std::string, std::hash<std::string>> cache(5, 0.5, std::hash<std::string>());
  cache.get("key1", 1);
  cache.insert("key1", "value1", 1, 2);
  cache.insert("key2", "value2", 1, 2);
  cache.insert("key3", "value3", 1, 2);
  cache.get("key2", 1);
  cache.insert("key4", "value4", 1, 2);
  cache.insert("key5", "value5", 1, 2);
  cache.get("key4", 1);
  cache.insert("key6", "value6", 1, 2); //kicks out 1
  cache.print(); //LRU: 3 -> 2 -> 5 -> 4 -> 6
  cache.insert("key7", "value7", 1, 2); //kicks out 3
  cache.insert("key8", "value8", 1, 2); //kicks out 2
  cache.insert("key9", "value9", 1, 2); //kicks out 5
  cache.get("key1", 1); //not found
  cache.get("key9", 1);
  cache.get("key8", 1);
  cache.print(); //LRU: 4 -> 6 -> 7 -> 9 -> 8
}

void autoTest() {
  //remember to turn off VERBOSE

  unsigned int randomSeed = time(NULL);
  srand (randomSeed);

  //parameters
  int numOperations = 1000000;
  int numDifferentValues = 1000000;
  int numUpdatesPerRun = 3;
  int numRuns = 10;

  for (int i = 0; i < numRuns; i++) {

    //parameters with randomized values
    int numFrequentKeys = 3 + rand()%25;
    int numTotalKeys = numFrequentKeys + 1 + rand()%1000;
    int freqToAllKeyRatio = 1 + rand()%2;
    int minTimeStep = 1 + rand()%2;
    int maxTimeStep = minTimeStep + 1 + rand()%5;
    int minTTL = 1 + rand()%5;
    int maxTTL = minTTL + rand()%10000;
    std::size_t cacheMaxSize = numTotalKeys / (1+ rand()%5);
    double loadFactor = 0.1 * (1 + rand()%5);
    int readWriteRatio = 1 + rand()%2; 

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
      currentTime += minTimeStep + rand()%(maxTimeStep - minTimeStep);

      int key;
      if (rand()%(1+freqToAllKeyRatio) != 0) key = rand()%numFrequentKeys;
      else key = rand()%numTotalKeys;

      bool isInsert;
      if (rand()%(1+readWriteRatio) != 0) isInsert = false;
      else isInsert = true;

      if (isInsert) {
        int value = rand()%numDifferentValues;
        int ttl = minTTL + rand()%(maxTTL-minTTL);

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
  autoTest();
}
