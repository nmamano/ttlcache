#ifndef DUMMY_CACHE_H
#define DUMMY_CACHE_H

#include <unordered_map>
#include <utility>

//for testing purposes. a "cache" that saves everything forever
template<class Key, class Value>
class dummy_cache {

public:
  typedef long long timestamp_t; 
private:

  timestamp_t currentTime;
  std::unordered_map<Key,std::pair<Value,timestamp_t>> kvMap; //key maps to (value, expirationTime)

public:

  dummy_cache(): currentTime{0} {}

  std::size_t size() { return kvMap.size(); }

  void insert(const Key& key, const Value& value, timestamp_t timeStamp, timestamp_t ttl) {

    if (timeStamp < currentTime) throw std::invalid_argument("attempt to time travel");
    if (ttl <= 0) throw std::invalid_argument("insertion dead on arrival");
    currentTime = timeStamp;

    kvMap[key] = {value, timeStamp + ttl};
  }

  std::optional<Value> get(const Key& key, timestamp_t timeStamp) {

    if (timeStamp < currentTime) throw std::invalid_argument("attempt to time travel");
    currentTime = timeStamp;

    if (kvMap.count(key)) {
      timestamp_t expirationTime = kvMap[key].second;
      if (expirationTime < currentTime) {
        kvMap.erase(key);
        return {};
      }
      return kvMap[key].first;
    }
    return {};
  }

};


#endif /* DUMMY_CACHE_H */