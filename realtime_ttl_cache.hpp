#ifndef REALTIME_TTL_CACHE_H
#define REALTIME_TTL_CACHE_H

#include "ttl_cache.hpp"
#include <chrono> 

/* wrapper around ttl_cache where time stamps are automatically generated from a real-time clock,
   so the user does not need to pass its own time stamps.  */
template<class Key, class Value, class HashFunction = std::hash<Key>>
class realtime_ttl_cache : public ttl_cache<Key,Value,HashFunction,long long> {

public:

  typedef long long timestamp_t;

  realtime_ttl_cache(std::size_t maxEntries, double maxLoadFactor, const HashFunction& hashFunction):
    ttl_cache<Key,Value,HashFunction,timestamp_t>(maxEntries, maxLoadFactor, hashFunction) {}


  //*overload* (not override) the ttl_cache public functions without the timestamp parameter:

  std::optional<Value> get(const Key& key) {
    return ttl_cache<Key,Value,HashFunction,timestamp_t>::get(key, currentTimeStamp());
  }
  void insert(const Key& key, const Value& value, timestamp_t millisecondsToLive) {
    ttl_cache<Key,Value,HashFunction,timestamp_t>::insert(key, value, currentTimeStamp(), millisecondsToLive);
  }
  void removeExpired(double targetRatio) {
    ttl_cache<Key,Value,HashFunction,timestamp_t>::removeExpired(currentTimeStamp(), targetRatio);
  }


  //converts the time to a "tick count" with 1ms precision:
  //each real-time second contains 1000 unique time stamps
  timestamp_t currentTimeStamp() {
    auto currentTime = std::chrono::steady_clock::now();
    typedef std::chrono::duration<timestamp_t, std::ratio<1,1000>> ms_time_t;
    auto now_ms = std::chrono::time_point_cast<ms_time_t>(currentTime);
    return now_ms.time_since_epoch().count();
  }


};

#endif /* REALTIME_TTL_CACHE_H */