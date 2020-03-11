#ifndef REALTIME_TTL_CACHE_H
#define REALTIME_TTL_CACHE_H

#include "ttl_cache.hpp"
#include <chrono> 

/* wrapper around ttl_cache where time stamps are automatically generated from a real-time clock,
   so the user does not need to pass its own time stamps. 
   by default, TTL values are expressed in ms. This can be changed with the ticsPerSec template argument
   e.g., for microseconds, set it to 1000000.
    */
template<class Key, class Value, class HashFunction = std::hash<Key>, long long ticsPerSec = 1000>
class realtime_ttl_cache {

  ttl_cache<Key,Value,HashFunction,long long> cache;

public:

  typedef long long timestamp_t;

  realtime_ttl_cache(std::size_t maxEntries, double maxLoadFactor, const HashFunction& hashFunction):
    cache(maxEntries, maxLoadFactor, hashFunction) {}

  std::optional<Value> get(const Key& key) {
    return cache.get(key, currentTimeStamp());
  }

  void insert(const Key& key, const Value& value, timestamp_t ticsToLive) {
    cache.insert(key, value, currentTimeStamp(), ticsToLive);
  }

  void removeExpired(double targetRatio) {
    cache.removeExpired(currentTimeStamp(), targetRatio);
  }

  //converts real time to a "tick count" with "ticsPerSec" precision
  timestamp_t currentTimeStamp() {
    auto currentTime = std::chrono::steady_clock::now();
    typedef std::chrono::duration<timestamp_t, std::ratio<1, ticsPerSec>> time_t;
    auto currentTimePoint = std::chrono::time_point_cast<time_t> (currentTime);
    return currentTimePoint.time_since_epoch().count();
  }

  std::size_t size() const { return cache.size(); } //includes expired entries still in the table
  bool empty() const { return cache.empty(); }
  std::size_t capacity() const { return cache.capacity(); }
  double loadFactor() const { return cache.loadFactor(); }
  void print() const { cache.print() }

};

#endif /* REALTIME_TTL_CACHE_H */