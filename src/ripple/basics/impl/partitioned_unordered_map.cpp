//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2021 Ripple Labs Inc.

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

#include <ripple/basics/partitioned_unordered_map.h>

#include <ripple/basics/SHAMapHash.h>
#include <ripple/basics/base_uint.h>
#include <ripple/beast/hash/uhash.h>
#include <ripple/protocol/Protocol.h>
#include <limits>
#include <string>

namespace ripple {

static std::size_t
extract(uint256 const& key)
{
    return *reinterpret_cast<std::size_t const*>(key.data());
}

static std::size_t
extract(SHAMapHash const& key)
{
    return *reinterpret_cast<std::size_t const*>(key.as_uint256().data());
}

static std::size_t
extract(LedgerIndex key)
{
    return static_cast<std::size_t>(key);
}

static std::size_t
extract(std::string const& key)
{
    return ::beast::uhash<>{}(key);
}

template <typename Key>
std::size_t
partitioner(Key const& key, std::size_t const numPartitions)
{
    return extract(key) % numPartitions;
}

template std::size_t
partitioner<LedgerIndex>(
    LedgerIndex const& key,
    std::size_t const numPartitions);

template std::size_t
partitioner<uint256>(uint256 const& key, std::size_t const numPartitions);

template std::size_t
partitioner<SHAMapHash>(SHAMapHash const& key, std::size_t const numPartitions);

template std::size_t
partitioner<std::string>(
    std::string const& key,
    std::size_t const numPartitions);

}  // namespace ripple
