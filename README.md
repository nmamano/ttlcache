# LRU + TTL Cache

An in-memory hash table that acts as a cache for a Key-Value storage and supports timeouts.

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

## Implementation

1. The 'removeExpired' algorithm is probabilistic and is based on [Redis](https://redis.io/commands/expire):

> 1. *Test 20 random keys from the set of keys with an associated expire.*
> 2. *Delete all the keys found expired.*
> 3. *If more than 25% of keys were expired, start again from step 1.*

2. Templates are used to allow the keys and values to be of any type. This avoids the need to "serialize" them into strings or any other specific types.

3. The hash table uses open addressing and linear probing, in contrast to Redis (which uses separate chaining). The usual trade-offs apply: separate chaining tolerates higher load factors and is less sensitive to bad hash functions, but has worse space locality (following a chain requires several random jumps in memory).

    Open addressing is likely better when the keys/values are large (e.g., long strings or arrays), because then the extra memory required to maintain a low load factor is relatively small. In the current implementation, each table cell takes 3 words (a pointer to a key-value pair, the key's hash, and the pair's ttl), so keeping the load factor to, e.g., 1/3, means that the table uses 9 words per cached item when full. If cached items themselves take much more than that, the overhead in space is minor. Conversely, if the items are just 1 word (e.g., an int), then the overhead in memory is substantial.


## Files

* `ttl_cache.hpp`: the cache implementation.
* `tests.cpp`: correctness tests.
* `realtime_ttl_cache.hpp`: wrapper around the cache where time stamps are automatically generated from a real-time clock, so the user does not need to pass its own time stamps.
* `dummy_cache.hpp`: a trivial implementation of a "cache" that just saves everything. It is used to compare against in tests.

## Build

Requires c++17 features. I compile the tests myself as:

`clang -O3 -std=c++17 tests.cpp`
