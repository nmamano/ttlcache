#include <vector>
#include <list>
#include <iostream>

template<class Key, class T, class Hash = std::hash<Key>>
class ttl_cache {

public:
  ttl_cache(std::size_t capacity, double maxLoadFactor, const Hash& hashFunction):
    hashFunction(hashFunction), maxLoadFactor(maxLoadFactor), content(), table(capacity) {

    maxLoadFactor = min(maxLoadFactor, 0.5);
  }

  bool empty() const { return size() == 0; }
  std::size_t size() const { return content.size(); }
  std::size_t capacity() const { return table.size(); }
  double loadFactor() const { return size()/capacity(); }

  const T* get(const Key& key) const {
    std::size_t hash = hashFunction(key);
    std::size_t index = hash%capacity();
    while (!emptyEntry(index)) {
      if (isKeyAtIndex(key, hash, index)) {
        return &(table[index].kv->value);
      }
      circular_inc(index);
    }
    return nullptr;
  }

  void insert(const Key& key, const T& value) {
    if ((size()+1)/capacity() > maxLoadFactor) return; //LRU not implemented yet

    std::size_t hash = hashFunction(key);
    std::size_t index = hash%capacity();
    while (!emptyEntry(index)) {
      if (isKeyAtIndex(key, hash, index)) {
        (table[index].kv)->value = value;
        return;
      }
      circular_inc(index);
    }

    auto KV = new KeyValue(key, value);
    content.insert(content.end(), KV);
    table[index].hash = hash;
    table[index].kv = KV;
  }

  void print() const {
    for (std::size_t i = 0; i < capacity(); i++) {
      std::cout<<i<<": ";
      if (not emptyEntry(i)) {
        std::cout<<table[i].kv->key<<" = "<<table[i].kv->value;
      }
      std::cout<<std::endl;
    }
    std::cout<<std::endl;
  }


private:
  const Hash& hashFunction;
  double maxLoadFactor;

  struct KeyValue {
    Key key;
    T value;
    KeyValue(const Key& key, const T& value): key(key), value(value) {}
  };
  std::list<KeyValue*> content;

  //table entries should be small (to improve spatial locality)
  //so we do not store the entire key in the table---we point to it
  //however, to avoid having to go to the heap to check for a key match
  //we also maintain the key's hash in the table
  struct TableEntry {
    KeyValue* kv;
    std::size_t hash;
    TableEntry(): kv(nullptr), hash() {}
  };
  std::vector<TableEntry> table;

  inline void circular_inc(std::size_t& index) const {
    index++;
    if (index == capacity()) index = 0;
  }

  inline emptyEntry(const std::size_t index) const {
    return table[index].kv == nullptr;
  }

  //eager evaluation is used to avoid the potentially expensive key comparison
  //except if the key is actually found or there is an (extremely rare) hash collision
  //the key's hash is passed along with the key to avoid recomputing it
  inline bool isKeyAtIndex(const Key& key, const std::size_t keyHash, const std::size_t index) const {
    const TableEntry& entry = table[index];
    return entry.hash == keyHash and entry.kv != nullptr and entry.kv->key == key;
  }
};

//test
int main() {
  ttl_cache<int,double,std::hash<int>> cache1(10, std::hash<int>());
  cache1.insert(7, 7.142);
  cache1.insert(9, 9.999);
  cache1.insert(1, 1.618);
  cache1.insert(2, 2.718);
  cache1.insert(3, 3.141);
  cache1.insert(0, 0.070);
  cache1.insert(9, 9.876);
  cache1.insert(74, 74.04);
  cache1.insert(100, 100.1);
  cache1.print();
}
