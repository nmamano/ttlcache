#ifndef DUMMY_CACHE_H
#define DUMMY_CACHE_H

#include <unordered_map>
#include <utility>

//for testing purposes. "cache" that saves everything forever
template<class Key, class Value>
class dummy_cache {

private:

  int currentTime;
  std::unordered_map<Key,std::pair<Value,int>> kvMap; //key maps to (value, expirationTime)

public:

  dummy_cache(): currentTime{0} {}

  std::size_t size() { return kvMap.size(); }
  
  void insert(const Key& key, const Value& value, int timeStamp, int ttl) {

    if (timeStamp < currentTime) throw std::invalid_argument("attempt to time travel");
    if (ttl <= 0) throw std::invalid_argument("insertion dead on arrival");
    currentTime = timeStamp;

    kvMap[key] = {value, timeStamp + ttl};
  }

  std::optional<Value> get(const Key& key, int timeStamp) {

    if (timeStamp < currentTime) throw std::invalid_argument("attempt to time travel");
    currentTime = timeStamp;

    if (kvMap.count(key)) {
      int expirationTime = kvMap[key].second;
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