#ifndef TTL_CACHE_H
#define TTL_CACHE_H

#include <iostream> //for logging
#include <cmath> //ceil
#include <stdexcept> //invalid_argument
#include <cassert> //checking invariants
#include <optional> //for when a key is not in cache

/* Hash table that acts as a cache for a Key-Value storage.

   It supports the following operations:
    - insert a key-value pair
    - get the value associated with a key, if any

   LRU mechanism:
    The cache stores a maximum number of pairs, specified at construction time
    If new insertions exceed this limit, the least recently read/written pair is deleted

   TTL mechanism (not implemented yet):
    Each key has an associated "time-to-live", after which it expires
    The cache offers a function to look for expired entries and remove them
    This can prevent the LRU mechanism from removing still-alive enties
*/

template<class Key, class Value, class HashFunction = std::hash<Key>>
class ttl_cache {

private:

  static constexpr bool VERBOSE = false; //logs the cache's changes in cerr


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
    int expireTime;
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
  static constexpr int LRU_EVICTED_FLAG = -2;

  /* to determine which entries are expired.
     currentTime is updated through calls to get, insert, and removeExpired
  */
  int currentTime;

public:

  ttl_cache(std::size_t maxEntries, double maxLoadFactor, const HashFunction& hashFunction):
    hashFunction{hashFunction},
    maxLoadFactor{maxLoadFactor},
    _capacity{maxLoadFactor >= 0.01 ? (std::size_t) ceil(maxEntries/maxLoadFactor) : 0},
    table{nullptr},
    LRU_oldest{nullptr}, LRU_newest{nullptr},
    _size{0},
    currentTime{0}
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
  std::size_t size() const { return _size; }
  bool empty() const { return _size == 0; }
  std::size_t capacity() const { return _capacity; }
  double loadFactor() const { return _size/_capacity; }


  std::optional<Value> get(const Key& key, int timeStamp) {

    if (timeStamp < currentTime) throw std::invalid_argument("attempt to time travel");

    std::size_t hash = hashFunction(key);
    std::size_t idealIndex = hashToIndex(hash);
    if (VERBOSE) std::cerr<<"GET call: "<<key<<" (hash "<<hash
                          <<", ideal pos "<<idealIndex<<")"<<std::endl;

    currentTime = timeStamp;
    fixCluster(idealIndex);

    std::size_t actualIndex = findKey(key, hash);
    if (actualIndex != invalidIndex()) {
      KeyValue* kv = table[actualIndex].kv;
      LRU_moveToNewest(kv);
      if (VERBOSE) std::cerr<<"GET result: found value "<< kv->value<<" for key "<<key
                            <<" (at pos "<<actualIndex<<")"<<std::endl<<std::endl;
      return kv->value;
    }

    if (VERBOSE) std::cerr<<"GET result: not found"<<std::endl<<std::endl;
    return {};
  }

  void insert(const Key& key, const Value& value, int timeStamp, int ttl) {

    if (timeStamp < currentTime) throw std::invalid_argument("attempt to time travel");
    if (ttl <= 0) throw std::invalid_argument("insertion dead on arrival");

    std::size_t hash = hashFunction(key);
    std::size_t idealIndex = hashToIndex(hash);
    if (VERBOSE) std::cerr<<"INSERT call: "<<key<<" = "<<value
                          <<" (hash "<<hash<<", ideal pos "<<idealIndex<<")"<<std::endl;

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

  /* Removes expired entries until the ratio of expired entries is around the expiredRatio
     The algorithm's performance degrades when expiredRatio is small. recommended: 0.25
  */
  void removeExpired(double expiredRatio) {
    //todo
  }

  void print() const {
    printTable();
    std::cout<<std::endl<<"LRU order: "<<std::endl;
    printLRUOrder();
    std::cout<<std::endl;
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

  inline std::size_t hashToIndex(const std::size_t hash) const {
    return hash % _capacity;
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
                     <<" (expired at "<<table[index].expireTime
                     <<", now is "<<currentTime<<")"<<std::endl;
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

    //pass 3: check that everything is correct
    // index = startIndex;
    // while (index != clusterEnd) {
    //   if (not isEmpty(index)) assert(findKey(table[index].kv->key) != invalidIndex());
    //   index = nextIndex(index);
    // }
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
    if (index == invalidIndex()) {//todo: delete this if (it's only for debugging)
      std::size_t hash = hashFunction(LRU_oldest->key);
      std::cerr<<"oldest hash: "<<hash<<" ideal idx: "<<hashToIndex(hash)<<std::endl;
      std::cerr<<"oldest key: "<<LRU_oldest->key<<std::endl;
      print();
    }
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
        std::cout<<kv->key<<" = "<<kv->value;
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