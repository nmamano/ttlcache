#include <vector> //substrate for the table
#include <iostream> //for logging
#include <cmath> //ceil
#include <stdexcept> //invalid_argument
#include <cassert> //checking invariants


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

  /* the actual cached data is stored in this struct
     it makes a doubly-linked list ('prev' and 'next' pointers) to be able to implement the LRU mechanism
  */
  struct KeyValue {
    Key key;
    Value value;
    std::size_t hash; //redundant storing to avoid recomputing
    KeyValue *next, *prev;
    KeyValue(Key key, Value value, std::size_t hash):
      key{std::move(key)},
      value{std::move(value)},
      hash{hash}, //this hash must be the key's hash
      next{nullptr}, prev{nullptr} {}
  };

  /* table entries should be as small as possible to improve space usage
     smaller entries will also require fewer reads from memory to iterate through the
     table (since we use open addressing, sometimes we need to traverse it sequentially)
     thus, we do not want to put KeyValue structs directly in the table
     instead, TableEntry is a small struct (8/16 bytes in a 32/64-bit system)
     pointing to an actual KeyValue. It also keeps the key's hash so that we
     can check for a match without having to access the KeyValue

     invariant: the hash corresponds to the key's hash. if the KeyValue pointer
     is null, the hash value is meaningless
  */
  struct TableEntry {
    KeyValue *kv;
    std::size_t hash;
    TableEntry(): kv{nullptr}, hash{0} {}
    TableEntry& operator=(const TableEntry& other) {
      kv = other.kv; hash = other.hash;
      return *this;
    }
  };

  const HashFunction& hashFunction;
  double maxLoadFactor;
  std::vector<TableEntry> table;

  /* invariant:
   - if the cache is empty, both LRU_oldest and LRU_newest are NULL
   - if the cache contains 1 element, both LRU_oldest and LRU_newest point to it
   - with >1 elements, LRU_oldest and LRU_newest are the endpoints of the doubly-linked list
     with all the cached KeyValue pairs */
  KeyValue *LRU_oldest, *LRU_newest;
  std::size_t _size;

public:

  ttl_cache(std::size_t maxEntries, double maxLoadFactor, const HashFunction& hashFunction):
    hashFunction{hashFunction},
    maxLoadFactor{maxLoadFactor},
    table{0},
    LRU_oldest{nullptr}, LRU_newest{nullptr},
    _size{0} {

      if (maxLoadFactor > 0.5) throw std::invalid_argument("Load factor too high");
      if (maxLoadFactor < 0.01) throw std::invalid_argument("Load factor too low");
      if (maxEntries < 2) throw std::invalid_argument("Too few entries");
      table = std::vector<TableEntry> ((std::size_t) ceil(maxEntries/maxLoadFactor));

      if (VERBOSE) std::cerr<<"Created hash table with max size "<<maxEntries
                            <<" and capacity "<<table.size()<<std::endl<<std::endl;
    }

  std::size_t size() const { return _size; }
  bool empty() const { return _size == 0; }
  std::size_t capacity() const { return table.size(); }
  double loadFactor() const { return _size/capacity(); }

  /* returns the value as a constant pointer to avoid copying it if the
     caller only needs to read it. The caller can copy it if needed
  */
  const Value* get(const Key& key) { //no "const" qualifier because of the LRU mechanism

    std::size_t hash = hashFunction(key);
    std::size_t index = hashToIndex(hash);
    if (VERBOSE) std::cerr<<"GET call: "<<key<<" (hash "<<hash<<", ideal pos "<<index<<")"<<std::endl;

    while (not isEmptyEntry(index)) {
      if (isKeyAtIndex(key, hash, index)) {
        KeyValue* kv = table[index].kv;
        LRU_moveToNewest(kv);
        if (VERBOSE) std::cerr<<"GET result: found value "<< kv->value<<" for key "<<key
                              <<" (at pos "<<index<<")"<<std::endl<<std::endl;
        return &kv->value;
      }
      index = mod_inc(index);
    }

    if (VERBOSE) std::cerr<<"GET result: not found"<<std::endl<<std::endl;
    return nullptr;
  }

  void insert(const Key& key, const Value& value) {

    std::size_t hash = hashFunction(key);
    std::size_t index = hashToIndex(hash);
    if (VERBOSE) std::cerr<<"INSERT call: "<<key<<" = "<<value
                          <<" (hash "<<hash<<", ideal pos "<<index<<")"<<std::endl;

    if ((_size+1) > maxLoadFactor*capacity()) {
      LRU_evictOldest();
    }

    while (not isEmptyEntry(index)) {
      if (isKeyAtIndex(key, hash, index)) {
        KeyValue* kv = table[index].kv;
        LRU_moveToNewest(kv);

        if (VERBOSE) std::cerr<<"INSERT result: updated value for key "<<key<<" (at pos "<<index<<"): "
                              <<kv->value<<" -> "<<value<<std::endl<<std::endl;

        kv->value = value; //actual update
        return;
      }
      index = mod_inc(index);
    }

    //not found
    auto kv = new KeyValue(key, value, hash);
    table[index].kv = kv;
    table[index].hash = hash;

    LRU_insertNewest(kv);

    if (VERBOSE) std::cerr<<"INSERT result: inserted new entry "<<key
                          <<" = "<<value<<" (at pos "<<index<<")"<<std::endl<<std::endl;
  }

  void print() const {
    printTable();
    std::cout<<std::endl<<"LRU order: "<<std::endl;
    printLRUOrder();
    std::cout<<std::endl;
  }




private:

  inline std::size_t mod_inc(const std::size_t index) const {
    return (index+1)%table.size();
  }

  inline std::size_t hashToIndex(const std::size_t hash) const {
    return hash%table.size();
  }

  inline std::size_t entryIndex(const TableEntry* entry) const {
    return (entry - &table)/sizeof(TableEntry);
  }

  inline bool isEmptyEntry(const std::size_t index) const {
    return table[index].kv == nullptr;
  }

  /* eager evaluation is used to avoid the potentially expensive key comparison
   unless the key is actually found or there is an (extremely rare) hash collision
   the key's hash is passed along with the key to avoid recomputing it */
  inline bool isKeyAtIndex(const Key& key, const std::size_t keyHash, const std::size_t index) const {
    const TableEntry& entry = table[index];
    return entry.hash == keyHash and entry.kv != nullptr and entry.kv->key == key;
  }

  //precondition: the key is in the table
  //the key's hash is passed along with the key to avoid recomputing it
  inline std::size_t keyToIndex(const Key& key, const std::size_t keyHash) const {
    std::size_t index = hashToIndex(keyHash);
    while (not isKeyAtIndex(key, keyHash, index)) {
      assert(not isEmptyEntry(index)); //open addressing invariant: no "holes" between a key's
                                       //ideal index and its actual index
      index = mod_inc(index);
    }
    return index;
  }





  void LRU_moveToNewest(KeyValue* kv) {
    if (kv == LRU_newest) {
      if (VERBOSE) std::cerr<<"LRU: moved key "<<kv->key
                            <<" to the end of the LRU order: already there"<<std::endl;
      return;
    }

    //remove kv form its current place in the doubly-linked list
    if (kv == LRU_oldest) {
      LRU_oldest = LRU_oldest->next;
      LRU_oldest->prev = nullptr;
    } else {
      kv->prev->next = kv->next;
      kv->next->prev = kv->prev;
    }

    //add kv at the end
    LRU_newest->next = kv;
    kv->prev = LRU_newest;
    kv->next = nullptr;
    LRU_newest = kv;

    if (VERBOSE) std::cerr<<"LRU: moved key "<<kv->key<<" to the end of the LRU order"<<std::endl;
  }

  void LRU_evictOldest() {
    assert(_size > 0);

    std::size_t vacatedIdx = keyToIndex(LRU_oldest->key, LRU_oldest->hash);

    if (VERBOSE) std::cerr<<"LRU: evicted key "<<LRU_oldest->key
                          <<" from pos "<<vacatedIdx<<std::endl;

    table[vacatedIdx].kv = nullptr;
    if (_size > 1) {
      LRU_oldest = LRU_oldest->next;
      delete LRU_oldest->prev;
      LRU_oldest->prev = nullptr;
    } else if (_size == 1) {
      delete LRU_oldest;
      LRU_oldest = LRU_newest = nullptr;
    }
    _size--;

    repositionAfterDelete(vacatedIdx);
  }

  /* After removing an entry in open addressing, subsequent entries may need
     to be moved to restore the open addressing invariant
  */
  void repositionAfterDelete(std::size_t vacatedIdx) {
    std::size_t index = mod_inc(vacatedIdx);
    while (not isEmptyEntry(index)) {
      std::size_t idealIdx = hashToIndex(table[index].hash);
      if (idealIdx != index) {
        std::size_t newIdx = idealIdx;
        while (not isEmptyEntry(newIdx)) newIdx = mod_inc(newIdx);

        if (newIdx != index) {
          table[newIdx] = table[index];
          table[index].kv = nullptr;
          if (VERBOSE) std::cerr<<"repositioned key "<<table[newIdx].kv->key
                                <<" from pos "<<index<<" to "<<newIdx<<std::endl;
        }
      }

      index = mod_inc(index);
    }
  }

  void LRU_insertNewest(KeyValue* kv) {
    if (_size > 0) {
      LRU_newest->next = kv;
      kv->prev = LRU_newest;
      LRU_newest = kv;
    } else {
      LRU_oldest = LRU_newest = kv;
    }
    _size++;

    if (VERBOSE) std::cerr<<"LRU: added key "<<kv->key<<" at the end of the LRU order"<<std::endl;
  }




  void printTable() const {
    for (std::size_t i = 0; i < capacity(); i++) {
      std::cout<<i<<": ";
      if (not isEmptyEntry(i)) {
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




