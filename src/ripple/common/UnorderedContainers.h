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

#ifndef RIPPLE_UNORDERED_CONTAINERS_H
#define RIPPLE_UNORDERED_CONTAINERS_H

#include <beast/container/hash_append.h>

#include <unordered_map>
#include <unordered_set>

namespace ripple
{

template <class Key, class Value, class Hash = beast::uhash<>,
          class Pred = std::equal_to<Key>,
          class Allocator = std::allocator<std::pair<Key const, Value>>>
using unordered_map = std::unordered_map <Key, Value, Hash, Pred, Allocator>;

template <class Value, class Hash = beast::uhash<>,
          class Pred = std::equal_to<Value>,
          class Allocator = std::allocator<Value>>
using unordered_set = std::unordered_set <Value, Hash, Pred, Allocator>;

} // ripple

#endif
