#pragma once

#include <xrpl/basics/hardened_hash.h>
#include <xrpl/basics/partitioned_unordered_map.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/beast/hash/xxhasher.h>

#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

/**
 * Use hash_* containers for keys that do not need a cryptographically secure
 * hashing algorithm.
 *
 * Use hardened_hash_* containers for keys that do need a secure hashing
 * algorithm.
 *
 * The cryptographic security of containers where a hash function is used as a
 * template parameter depends entirely on that hash function and not at all on
 * what container it is.
 */

namespace xrpl {

// hash containers

template <
    class Key,
    class Value,
    class Hash = beast::Uhash<>,
    class Pred = std::equal_to<Key>,
    class Allocator = std::allocator<std::pair<Key const, Value>>>
using HashMap = std::unordered_map<Key, Value, Hash, Pred, Allocator>;

template <
    class Key,
    class Value,
    class Hash = beast::Uhash<>,
    class Pred = std::equal_to<Key>,
    class Allocator = std::allocator<std::pair<Key const, Value>>>
using HashMultimap = std::unordered_multimap<Key, Value, Hash, Pred, Allocator>;

template <
    class Value,
    class Hash = beast::Uhash<>,
    class Pred = std::equal_to<Value>,
    class Allocator = std::allocator<Value>>
using HashSet = std::unordered_set<Value, Hash, Pred, Allocator>;

template <
    class Value,
    class Hash = beast::Uhash<>,
    class Pred = std::equal_to<Value>,
    class Allocator = std::allocator<Value>>
using HashMultiset = std::unordered_multiset<Value, Hash, Pred, Allocator>;

// hardened_hash containers

using StrongHash = beast::Xxhasher;

template <
    class Key,
    class Value,
    class Hash = HardenedHash<StrongHash>,
    class Pred = std::equal_to<Key>,
    class Allocator = std::allocator<std::pair<Key const, Value>>>
using HardenedHashMap = std::unordered_map<Key, Value, Hash, Pred, Allocator>;

template <
    class Key,
    class Value,
    class Hash = HardenedHash<StrongHash>,
    class Pred = std::equal_to<Key>,
    class Allocator = std::allocator<std::pair<Key const, Value>>>
using HardenedPartitionedHashMap = PartitionedUnorderedMap<Key, Value, Hash, Pred, Allocator>;

template <
    class Key,
    class Value,
    class Hash = HardenedHash<StrongHash>,
    class Pred = std::equal_to<Key>,
    class Allocator = std::allocator<std::pair<Key const, Value>>>
using HardenedHashMultimap = std::unordered_multimap<Key, Value, Hash, Pred, Allocator>;

template <
    class Value,
    class Hash = HardenedHash<StrongHash>,
    class Pred = std::equal_to<Value>,
    class Allocator = std::allocator<Value>>
using HardenedHashSet = std::unordered_set<Value, Hash, Pred, Allocator>;

template <
    class Value,
    class Hash = HardenedHash<StrongHash>,
    class Pred = std::equal_to<Value>,
    class Allocator = std::allocator<Value>>
using HardenedHashMultiset = std::unordered_multiset<Value, Hash, Pred, Allocator>;

}  // namespace xrpl
