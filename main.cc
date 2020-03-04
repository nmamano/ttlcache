#include <iostream>
#include <string>

#include <random> //for randomized testing
#include <ctime> //random seed for testing
#include <unordered_map> //to compare against in testing

#include "ttl_cache.hpp"

void manualTest() {
  //remember to turn on VERBOSE
  ttl_cache<std::string, std::string, std::hash<std::string>> cache(5, 0.5, std::hash<std::string>());
  cache.get("key1");
  cache.insert("key1", "value1");
  cache.insert("key2", "value2");
  cache.insert("key3", "value3");
  cache.get("key2");
  cache.insert("key4", "value4");
  cache.insert("key5", "value5");
  cache.get("key4");
  cache.insert("key6", "value6"); //kicks out 1
  cache.print(); //LRU: 3 -> 2 -> 5 -> 4 -> 6
  cache.insert("key7", "value7"); //kicks out 3
  cache.insert("key8", "value8"); //kicks out 2
  cache.insert("key9", "value9"); //kicks out 5
  cache.get("key1");
  cache.get("key9");
  cache.get("key8");
  cache.print(); //LRU: 4 -> 6 -> 7 -> 9 -> 8
}

void autoTest() {
  //remember to turn off VERBOSE

  unsigned int randomSeed = time(NULL);
  srand (randomSeed);

  int numOperations = 1000000;
  int numDifferentValues = 1000000;
  int numRuns = 10;

  for (int i = 0; i < numRuns; i++) {

    int numDifferentKeys = 10 + rand()%200;
    std::size_t cacheMaxSize = numDifferentKeys / (1+ rand()%5);
    double loadFactor = 0.1 * (1 + rand()%5);
    std::cout<<"TEST "<<numDifferentKeys<<" keys, "<<cacheMaxSize<<" max cache size, "
             <<loadFactor<<" load factor"<<std::endl;

    ttl_cache<int, int, std::hash<int>> cache(cacheMaxSize, loadFactor, std::hash<int>());
    std::unordered_map<int, int> trueMap;
    double matches = 0, misses = 0;

    bool bugInCache = false;
    for (int i = 0; i < numOperations; i++) {
      int key = rand()%numDifferentKeys;
      bool isInsert = (rand()%2 == 0);
      if (isInsert) {
        int value = rand()%numDifferentValues;
        trueMap[key] = value;
        cache.insert(key, value);
      } else {
        const int *value = cache.get(key);
        if (value != nullptr and *value != trueMap[key]) {
          std::cout<<"wrong value in the cache"<<std::endl;
          bugInCache = true;
        }
        if (value == nullptr) misses+=1;
        else matches+=1;
      }
    }
    if (bugInCache) std::cout<<"cache implementation is wrong"<<std::endl;
    else std::cout<<">> cache passed the randomized test (match ratio: "
                  <<matches/(matches+misses)<<" expected: "
                  <<cacheMaxSize/(double) numDifferentKeys<<")"<<std::endl;
  }
}

int main() {
  // test();
  autoTest();
}
