#ifndef TTL_CACHE_H
#define TTL_CACHE_H

#include <iostream> //logging
#include <cassert> //checking invariants
#include <stdexcept> //invalid parameters in public function calls
#include <optional> //for when a key is not in cache
#include <cmath> //ceil
#include <random> //to get random samples in the expire algorithm
#include <chrono> //random seed for expire algo, default timestamp type
#include <vector> //to store the samples in the expire algorithm


/* In-memory hash table that acts as a cache for a Key-Value storage and supports timeouts.

It supports three operations:
- insert a key-value pair with an associated ttl (time-to-live)
- get the value associated with a key (if it has not expired according to its ttl)
- remove expired entries to reduce the load factor

It implements the following mechanisms:

* **LRU**:
The maximum number of key-value pairs is specified at construction time.
If new insertions exceed this limit, the least recently read/written pair is deleted.

* **TTL**:
Expired entries which are still in the table are removed in two ways:
  1. Passively, as they are discoverd through get/insert operations. 
  2. Actively, by calling 'removeExpired'. This can prevent the LRU mechanism from removing still-alive enties.
*/

template<class Key, class Value, class HashFunction = std::hash<Key>, class timestamp_t = long long int>
class ttl_cache {

private:

  //option to log the cache's changes to cerr. declared as a compiler-time constant so that the compiler
  //can remove all the dead logging code when it is set to false
  static constexpr bool VERBOSE = false;

  /* each key-value pair is stored in this struct
     it makes a doubly-linked list ('prev' and 'next' pointers) to be able to implement the LRU mechanism
  */
  struct KeyValue {
    Key key;
    Value value;
    KeyValue *next, *prev;
    KeyValue(Key key, Value value):
      key{std::move(key)},
      value{std::move(value)},
      next{nullptr}, prev{nullptr} {}
  };

  /* to determine which entries are expired.
     currentTime is updated through the time stamps passed to public calls (get, insert, and removeExpired)
     note that we do not use "real time" (the chrono include is just for the random seed)
     we only assume that the time stamps in public calls will be non-negative and increasing

     timestamp_t should be a signed integer or floating point type
  */
  timestamp_t currentTime;

  /* hash table entries should be as small as possible to improve space usage
     smaller entries will also require fewer reads from memory to iterate through the
     table (since we use open addressing, sometimes we need to traverse it sequentially)
     thus, we do not want to put KeyValue structs directly in the table
     instead, TableEntry is a small struct (3 words)
     pointing to an actual KeyValue.
     It also keeps the key's hash and expire time so that we
     can check for a match without having to access the KeyValue

     invariant: the hash corresponds to the key's hash. if the KeyValue pointer
     is null, the hash/expireTime values are meaningless
  */
  struct TableEntry {
    KeyValue *kv;
    std::size_t hash;
    timestamp_t expireTime;
    TableEntry(): kv{nullptr}, hash{0}, expireTime{0} {}
  };

  const HashFunction& hashFunction;
  double maxLoadFactor;
  const std::size_t _capacity; //size of the hash table

  /* the hash table using open addressing
     invariant (the "open addressing invariant"): there are no empty entries between a
     key's "ideal" position in the table and a key's actual position in the table
  */
  TableEntry* table;

  /* LRU mechanism invariants:
   - if the cache is empty, both LRU_oldest and LRU_newest are NULL
   - if the cache contains 1 element, both LRU_oldest and LRU_newest point to it
   - with >1 elements, LRU_oldest and LRU_newest are the endpoints of the doubly-linked list
     with all the cached KeyValue pairs */
  KeyValue *LRU_oldest, *LRU_newest;
  std::size_t _size;
  static constexpr timestamp_t LRU_EVICTED_FLAG = -2;

  //for expire algorithm
  std::mt19937_64 RNG;


public:

  ttl_cache(std::size_t maxEntries, double maxLoadFactor, const HashFunction& hashFunction):
    currentTime{0},
    hashFunction{hashFunction},
    maxLoadFactor{maxLoadFactor},
    _capacity{maxLoadFactor >= 0.01 ? (std::size_t) ceil(maxEntries/maxLoadFactor) : 0},
    table{nullptr},
    LRU_oldest{nullptr}, LRU_newest{nullptr},
    _size{0},
    RNG{static_cast<uint64_t> (std::chrono::steady_clock::now().time_since_epoch().count())}
  {
      if (maxLoadFactor > 0.5) throw std::invalid_argument("Load factor too high");
      if (maxLoadFactor < 0.01) throw std::invalid_argument("Load factor too low");
      if (maxEntries < 2) throw std::invalid_argument("Too few entries");

      table = new TableEntry[_capacity];

      if (VERBOSE) std::cerr<<"Created hash table with max size "<<maxEntries
                            <<" and capacity "<<_capacity<<std::endl<<std::endl;
  }

  ~ttl_cache() {
    KeyValue* cur = LRU_oldest;
    while (cur) {
      KeyValue* next = cur->next;
      delete cur;
      cur = next;
    }
    delete table;
  }

  //getters for basic info
  std::size_t size() const { return _size; } //includes expired entries still in the table
  bool empty() const { return _size == 0; }
  std::size_t capacity() const { return _capacity; }
  double loadFactor() const { return _size/(double) _capacity; }
  timestamp_t currentTimeStamp() const { return currentTime; }


  std::optional<Value> get(const Key& key, timestamp_t timeStamp) {

    if (timeStamp < currentTime) throw std::invalid_argument("attempt to time travel");

    std::size_t hash = hashFunction(key);
    std::size_t idealIndex = hashToIndex(hash);
    if (VERBOSE) std::cerr<<"GET call: "<<key<<" (hash "<<hash
                          <<", ideal pos "<<idealIndex<<") [at time: "<<timeStamp<<"]"<<std::endl;

    currentTime = timeStamp;
    fixCluster(idealIndex);

    std::size_t actualIndex = findKey(key, hash);
    if (actualIndex != invalidIndex()) {
      assert(not isExpired(actualIndex));

      KeyValue* kv = table[actualIndex].kv;
      LRU_moveToNewest(kv);
      if (VERBOSE) std::cerr<<"GET result: found value "<< kv->value<<" for key "<<key
                            <<" (at pos "<<actualIndex<<")"<<std::endl<<std::endl;
      return kv->value;
    }

    if (VERBOSE) std::cerr<<"GET result: not found"<<std::endl<<std::endl;
    return {};
  }

  void insert(const Key& key, const Value& value, timestamp_t timeStamp, timestamp_t ttl) {

    if (timeStamp < currentTime) throw std::invalid_argument("attempt to time travel");
    if (ttl <= 0) throw std::invalid_argument("insertion dead on arrival");

    std::size_t hash = hashFunction(key);
    std::size_t idealIndex = hashToIndex(hash);
    if (VERBOSE) std::cerr<<"INSERT call: "<<key<<" = "<<value
                          <<" (hash "<<hash<<", ideal pos "<<idealIndex<<") [lifespan: "
                          <<timeStamp<<"-"<<timeStamp+ttl<<"]"<<std::endl;

    currentTime = timeStamp;
    fixCluster(idealIndex);

    if ((_size+1) > maxLoadFactor * _capacity) {
      LRU_evictOldest();
    }

    std::size_t actualIndex = findKey(key, hash);
    if (actualIndex != invalidIndex()) {
      KeyValue* kv = table[actualIndex].kv;
      LRU_moveToNewest(kv);
      if (VERBOSE) std::cerr<<"INSERT result: updated value for key "<<key
                            <<" (at pos "<<actualIndex<<"): "
                            <<kv->value<<" -> "<<value<<std::endl<<std::endl;
      table[actualIndex].expireTime = timeStamp + ttl;
      kv->value = value;
      return;
    }

    std::size_t newIndex = nextEmpty(idealIndex);
    auto kv = new KeyValue(key, value);
    table[newIndex].kv = kv;
    table[newIndex].hash = hash;
    table[newIndex].expireTime = timeStamp + ttl;

    LRU_insertNewest(kv);
    _size++;

    assert(findKey(key) != invalidIndex());
    if (VERBOSE) std::cerr<<"INSERT result: inserted new entry "<<key
                          <<" = "<<value<<" (at pos "<<newIndex<<")"<<std::endl<<std::endl;

  }

  /* Expire algorithm from Redis
     source: https://redis.io/commands/expire

     Removes expired entries until the ratio of expired entries is around 'targetRatio'
     The algorithm's performance degrades when targetRatio is small. recommended: 0.25

     "1. Test 20 random keys from the set of keys with an associated expire.
      2. Delete all the keys found expired.
      3. If more than 25% of keys were expired, start again from step 1."
  */
  void removeExpired(timestamp_t timeStamp, double targetRatio) {

    static constexpr double MIN_LOAD_FACTOR = 0.1;
    static constexpr unsigned int MIN_SAMPLE_SIZE = 20;
    static constexpr double MIN_TARGET_RATIO = 0.01;

    if (timeStamp < currentTime) throw std::invalid_argument("attempt to time travel");
    if (targetRatio < MIN_TARGET_RATIO) throw std::invalid_argument("too demanding target");

    if (VERBOSE) std::cerr<<"EXPIRE call: size: "<<_size<<", load factor: "<<loadFactor()
                          <<", target expired ratio: "<<targetRatio<<", at time: ["<<timeStamp<<"]"<<std::endl;

    currentTime = timeStamp;
    
    double expiredRatio;

    while (true) {

      if (loadFactor() < MIN_LOAD_FACTOR) {
        if (VERBOSE) std::cerr<<"expire stopped because the load factor ("<<loadFactor()<<") is"
                              <<" so low that taking random samples is too expensive"<<std::endl;
        break;
      }
      if (_size < MIN_SAMPLE_SIZE) {
        if (VERBOSE) std::cerr<<"expire stopped because the cache does not have enough"
                              <<" entries to take a random sample"<<std::endl;
        break;
      }

      std::size_t beforeSize = _size;

      std::vector<std::size_t> sample;
      sample.reserve(MIN_SAMPLE_SIZE+5);
      while (sample.size() < MIN_SAMPLE_SIZE) {
        std::size_t index = randomIndex();
        while (isEmpty(index)) index = randomIndex();

        bool alreadySampled = std::find(sample.begin(), sample.end(), index) != sample.end();
        if (alreadySampled) continue;

        std::vector<std::size_t> cluster = clusterIndices(index);
        std::move(cluster.begin(), cluster.end(), std::back_inserter(sample));
        fixCluster(index); //this removes the expired entries in the cluster, updating _size in the process
      }

      unsigned int expiredCount = (beforeSize - _size);
      expiredRatio = expiredCount / (double) sample.size(); 


      if (VERBOSE) std::cerr<<"sampled "<<sample.size()<<" random keys, removed "<<expiredCount
                            <<" expired keys ("<<100.0*expiredRatio<<"%)"<<std::endl;
      if (expiredRatio <= targetRatio) break;
    }

    if (VERBOSE) std::cerr<<"EXPIRE result: size: "<<_size<<", load factor: "<<loadFactor()
                          <<", last sample expired ratio: "<<100.0*expiredRatio<<"%"<<std::endl<<std::endl;
  }


  void print() const {
    std::cout<<"State [at time "<<currentTime<<"]"<<std::endl<<std::endl;
    printTable();
    std::cout<<std::endl<<"LRU order: "<<std::endl;
    printLRUOrder();
    std::cout<<std::endl;
  }

  std::vector<Key> LRU_order() {
    std::vector<Key> res;
    res.reserve(_size);
    KeyValue* cur = LRU_oldest;
    while (cur) {
      res.push_back(cur->key);
      cur = cur->next;
    }    
    return res;
  }




private:

  inline std::size_t nextIndex(const std::size_t index) const {
    return (index + 1)%_capacity;
  }

  inline std::size_t prevIndex(const std::size_t index) const {
    return (index + _capacity - 1)%_capacity;
  }

  inline std::size_t invalidIndex() const {
    return _capacity; //arbitrary value outside the valid range
  }

  inline std::size_t randomIndex() {
    return (std::size_t) RNG()%_capacity;
  }

  inline std::size_t hashToIndex(const std::size_t hash) const {
    return hash % _capacity;
  }

  inline std::size_t entryDist(const std::size_t index1, const std::size_t index2) const {
    if (index1 <= index2) return index2 - index1;
    return index2 + _capacity - index1;
  }

  inline std::size_t entryIndex(const TableEntry* entry) const {
    return (entry - &table)/sizeof(TableEntry);
  }

  inline bool isEmpty(const std::size_t index) const {
    return table[index].kv == nullptr;
  }

  inline void setEmpty(const std::size_t index) const {
    table[index].kv = nullptr; //lazy operation: does not update the other fields
  }

  inline void moveEntryFromTo(const std::size_t fromIndex, const std::size_t toIndex) const {
      table[toIndex].kv = table[fromIndex].kv;
      table[toIndex].hash = table[fromIndex].hash;
      table[toIndex].expireTime = table[fromIndex].expireTime;
      setEmpty(fromIndex);
  }

  std::size_t nextEmpty(std::size_t index) const {
    while (not isEmpty(index)) index = nextIndex(index);
    return index;
  }

  std::size_t findClusterStart(std::size_t index) const {
    assert(not isEmpty(index));
    while (not isEmpty(prevIndex(index))) index = prevIndex(index);
    return index;
  }

  inline std::size_t clusterSize(const std::size_t index) const {
    assert (not isEmpty(index));
    return nextEmpty(index) - findClusterStart(index);
  }

  std::vector<std::size_t> clusterIndices(std::size_t index) const {
    assert(not isEmpty(index));
    std::vector<std::size_t> res;
    while (not isEmpty(index)) { //add indices after
      res.push_back(index);
      index = nextIndex(index);
    }
    index = prevIndex(res[0]);
    while (not isEmpty(index)) { //add indices before
      res.push_back(index);
      index = prevIndex(index);
    }
    return res;
  }

  inline bool isExpired(const std::size_t index) const {
    assert(not isEmpty(index));
    return currentTime >= table[index].expireTime;
  }

  /* eager evaluation is used to avoid the potentially expensive key comparison
    unless the key is actually found or there is an (extremely rare) hash collision.
    the key's hash is passed along with the key to avoid recomputing it
    precondition: table is not empty at index
  */
  inline bool isKeyAtIndex(const Key& key, const std::size_t keyHash, const std::size_t index) const {
    assert(not isEmpty(index)); 
    return table[index].hash == keyHash and table[index].kv->key == key;
  }

  //the key's hash is passed along with the key to avoid recomputing it
  std::size_t findKey(const Key& key, const std::size_t keyHash) const {
    for (auto idx = hashToIndex(keyHash); not isEmpty(idx); idx = nextIndex(idx)) {
      if (isKeyAtIndex(key, keyHash, idx)) return idx;
    }
    return invalidIndex();
  }
  inline std::size_t findKey(const Key& key) const {
    return findKey(key, hashFunction(key));
  }

  /* a cluster is a maximal contiguous sequence of non-empty entries.
     if table[indexInCluster] is empty, does nothing
     otherwise, removes all the expired entries in its cluster,
     and restores the open-addressing invariant for all the entries in the cluster
     by moving them to their best possible location, from left to right
  */
  void fixCluster(const std::size_t indexInCluster) {
    if (isEmpty(indexInCluster)) return;

    //pass 1: remove all expired entries, without relocating anything
    std::size_t startIndex = findClusterStart(indexInCluster);
    std::size_t firstRemovedIndex = invalidIndex();
    std::size_t index = startIndex;
    while (not isEmpty(index)) {
      if (isExpired(index)) {
        if (VERBOSE) {
          if (table[index].expireTime != LRU_EVICTED_FLAG) {
            std::cerr<<"TTL: removed expired key "<<table[index].kv->key
                     <<" [expired at "<<table[index].expireTime
                     <<", now is "<<currentTime<<"]"<<std::endl;
          }
        }
        removeWithoutRelocations(index);
        if (firstRemovedIndex == invalidIndex()) {
          firstRemovedIndex = index;
        }
      }
      index = nextIndex(index);
    }

    //pass 2: relocate the entries to restore the open-adressing invariant
    if (firstRemovedIndex == invalidIndex()) {
      return; //didn't remove anything, so no relocations happen
    }
    std::size_t clusterEnd = index;

    index = nextIndex(firstRemovedIndex); //first index that could be relocated
    while (index != clusterEnd) {
      if (not isEmpty(index)) {
        assert(not isExpired(index));
        std::size_t idealIndex = hashToIndex(table[index].hash);
        if (idealIndex != index) {
          std::size_t newIdx = idealIndex;
          //possible optimization: instead of initializing newIdx to idealIndex
          //initialize it to max(idealIndex, first empty index in the cluster)
          while (newIdx != index and not isEmpty(newIdx)) newIdx = nextIndex(newIdx);

          if (newIdx != index) {
            moveEntryFromTo(index, newIdx);
            if (VERBOSE) std::cerr<<"repositioned key "<<table[newIdx].kv->key
                                  <<" from pos "<<index<<" to "<<newIdx<<std::endl;
          }
        }
      }
      index = nextIndex(index);
    }
  }

  /* removes from the hash table and the doubly-linked list,
     but does not make relocations to maintain the open-adressing invariant
  */
  void removeWithoutRelocations(const std::size_t index) {
    assert(not isEmpty(index));
    KeyValue* kv = table[index].kv;
    setEmpty(index);
    LRU_removeFromList(kv);
    _size--;
    delete kv;
  }



  /*** LRU functions ***/


  //updates the next/prev pointers, and LRU_newest/LRU_oldest, as needed
  //so that the LRU list is exactly the same but without 'kv'
  //- does not update _size nor delete kv
  //- the next/prev pointers of kv are not set to null
  void LRU_removeFromList(const KeyValue* kv) {
    if (_size == 1) {
      assert(kv == LRU_newest);
      assert(kv == LRU_oldest);
      LRU_newest = LRU_oldest = nullptr;
    } else if (kv == LRU_newest) {
      assert(kv != LRU_oldest);
      LRU_newest = LRU_newest->prev;
      LRU_newest->next = nullptr;
    } else if (kv == LRU_oldest) {
      assert (kv != LRU_newest);
      LRU_oldest = LRU_oldest->next;
      LRU_oldest->prev = nullptr;
    } else {
      kv->next->prev = kv->prev;
      kv->prev->next = kv->next;
    }
  }

  void LRU_moveToNewest(KeyValue* kv) {
    if (kv == LRU_newest) {
      if (VERBOSE) std::cerr<<"LRU: moved key "<<kv->key
                            <<" to the end of the LRU order: already there"<<std::endl;
      return;
    }
    LRU_removeFromList(kv);
    LRU_insertNewest(kv, true);
    assert(findKey(kv->key) != invalidIndex());
  }

  //updates the next/prev pointers, and LRU_newest/LRU_oldest, as needed
  //so that the LRU list is exactly the same but with 'kv' at the end
  //- does not update _size
  //- the optional bool parameter is only relevant for logging in verbose mode
  void LRU_insertNewest(KeyValue* kv, const bool logAsMove = false) {
    if (_size == 0) {
      LRU_oldest = LRU_newest = kv;
      kv->next = kv->prev = nullptr;
    }
    else  {
      LRU_newest->next = kv;
      kv->prev = LRU_newest;
      kv->next= nullptr;
      LRU_newest = kv;
    }
    if (VERBOSE) {
      if (logAsMove) std::cerr<<"LRU: moved key "<<kv->key<<" to the end of the LRU order"<<std::endl;
      else           std::cerr<<"LRU: added key "<<kv->key<<" at the end of the LRU order"<<std::endl;
    }
  }

  void LRU_evictOldest() {
    assert(_size > 0);
    std::size_t index = findKey(LRU_oldest->key);
    assert(index != invalidIndex());

    //manipulate the expire time to make it look expired
    table[index].expireTime = LRU_EVICTED_FLAG;

    if (VERBOSE) std::cerr<<"LRU: evicted key "<<LRU_oldest->key
                          <<" from pos "<<index<<std::endl;

    fixCluster(index);
  }



  /*** printing functions ***/

  void printTable() const {
    for (std::size_t i = 0; i < _capacity; i++) {
      std::cout<<i<<": ";
      if (not isEmpty(i)) {
        KeyValue *kv = table[i].kv;
        std::size_t displacement = entryDist(hashToIndex(table[i].hash), i);
        std::cout<<kv->key<<" = "<<kv->value<<" ";
        if (displacement == 0) std::cout<<"(*)";
        else std::cout<<"(+"<<displacement<<")";

        std::cout<<" ["<<table[i].expireTime;
        if (isExpired(i)) std::cout<<"!";
        std::cout<<"]";
      }
      std::cout<<std::endl;
    }
  }

  void printLRUOrder() const {
    std::cout<<"[";
    KeyValue *kv = LRU_oldest;
    while (kv) {
      std::cout<<kv->key<<" = "<<kv->value;
      if (kv->next) std::cout<<", ";
      kv = kv->next;
    }
    std::cout<<"]"<<std::endl;
  }

};




#endif /* TTL_CACHE */