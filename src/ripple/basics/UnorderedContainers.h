//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_BASICS_UNORDEREDCONTAINERS_H_INCLUDED
#define RIPPLE_BASICS_UNORDEREDCONTAINERS_H_INCLUDED

#include <ripple/basics/hardened_hash.h>
#include <ripple/beast/hash/hash_append.h>
#include <ripple/beast/hash/uhash.h>
#include <ripple/beast/hash/xxhasher.h>
#include <unordered_map>
#include <unordered_set>

/**
* Use hash_* containers for keys that do not need a cryptographically secure
* hashing algorithm.
*
* Use hardened_hash_* containers for keys that do need a secure hashing algorithm.
*
* The cryptographic security of containers where a hash function is used as a
* template parameter depends entirely on that hash function and not at all on
* what container it is.
*/

namespace ripple
{

// hash containers

template <class Key, class Value, class Hash = beast::uhash<>,
          class Pred = std::equal_to<Key>,
          class Allocator = std::allocator<std::pair<Key const, Value>>>
using hash_map = std::unordered_map <Key, Value, Hash, Pred, Allocator>;

template <class Key, class Value, class Hash = beast::uhash<>,
          class Pred = std::equal_to<Key>,
          class Allocator = std::allocator<std::pair<Key const, Value>>>
using hash_multimap = std::unordered_multimap <Key, Value, Hash, Pred, Allocator>;

template <class Value, class Hash = beast::uhash<>,
          class Pred = std::equal_to<Value>,
          class Allocator = std::allocator<Value>>
using hash_set = std::unordered_set <Value, Hash, Pred, Allocator>;

template <class Value, class Hash = beast::uhash<>,
          class Pred = std::equal_to<Value>,
          class Allocator = std::allocator<Value>>
using hash_multiset = std::unordered_multiset <Value, Hash, Pred, Allocator>;

// hardened_hash containers

using strong_hash = beast::xxhasher;

template <class Key, class Value, class Hash = hardened_hash<strong_hash>,
          class Pred = std::equal_to<Key>,
          class Allocator = std::allocator<std::pair<Key const, Value>>>
using hardened_hash_map = std::unordered_map <Key, Value, Hash, Pred, Allocator>;

template <class Key, class Value, class Hash = hardened_hash<strong_hash>,
          class Pred = std::equal_to<Key>,
          class Allocator = std::allocator<std::pair<Key const, Value>>>
using hardened_hash_multimap = std::unordered_multimap <Key, Value, Hash, Pred, Allocator>;

template <class Value, class Hash = hardened_hash<strong_hash>,
          class Pred = std::equal_to<Value>,
          class Allocator = std::allocator<Value>>
using hardened_hash_set = std::unordered_set <Value, Hash, Pred, Allocator>;

template <class Value, class Hash = hardened_hash<strong_hash>,
          class Pred = std::equal_to<Value>,
          class Allocator = std::allocator<Value>>
using hardened_hash_multiset = std::unordered_multiset <Value, Hash, Pred, Allocator>;

} // ripple

#endif
