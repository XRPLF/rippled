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

#ifndef RIPPLE_NODESTORE_TUNING_H_INCLUDED
#define RIPPLE_NODESTORE_TUNING_H_INCLUDED

namespace ripple {
namespace NodeStore {

enum
{
    // Target cache size of the TaggedCache used to hold nodes
    cacheTargetSize     = 16384

    // Fraction of the cache one query source can take
    ,asyncDivider = 8
};

// Expiration time for cached nodes
std::chrono::seconds constexpr cacheTargetAge = std::chrono::minutes{5};
auto constexpr shardCacheSz = 16384;
std::chrono::seconds constexpr shardCacheAge = std::chrono::minutes{1};

}
}

#endif
