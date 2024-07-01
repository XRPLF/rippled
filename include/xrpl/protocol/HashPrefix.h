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

#ifndef RIPPLE_PROTOCOL_HASHPREFIX_H_INCLUDED
#define RIPPLE_PROTOCOL_HASHPREFIX_H_INCLUDED

#include <ripple/beast/hash/hash_append.h>
#include <cstdint>

namespace ripple {

namespace detail {

constexpr std::uint32_t
make_hash_prefix(char a, char b, char c)
{
    return (static_cast<std::uint32_t>(a) << 24) +
        (static_cast<std::uint32_t>(b) << 16) +
        (static_cast<std::uint32_t>(c) << 8);
}

}  // namespace detail

/** Prefix for hashing functions.

    These prefixes are inserted before the source material used to generate
    various hashes. This is done to put each hash in its own "space." This way,
    two different types of objects with the same binary data will produce
    different hashes.

    Each prefix is a 4-byte value with the last byte set to zero and the first
    three bytes formed from the ASCII equivalent of some arbitrary string. For
    example "TXN".

    @note Hash prefixes are part of the protocol; you cannot, arbitrarily,
          change the type or the value of any of these without causing breakage.
*/
enum class HashPrefix : std::uint32_t {
    /** transaction plus signature to give transaction ID */
    transactionID = detail::make_hash_prefix('T', 'X', 'N'),

    /** transaction plus metadata */
    txNode = detail::make_hash_prefix('S', 'N', 'D'),

    /** account state */
    leafNode = detail::make_hash_prefix('M', 'L', 'N'),

    /** inner node in V1 tree */
    innerNode = detail::make_hash_prefix('M', 'I', 'N'),

    /** ledger master data for signing */
    ledgerMaster = detail::make_hash_prefix('L', 'W', 'R'),

    /** inner transaction to sign */
    txSign = detail::make_hash_prefix('S', 'T', 'X'),

    /** inner transaction to multi-sign */
    txMultiSign = detail::make_hash_prefix('S', 'M', 'T'),

    /** validation for signing */
    validation = detail::make_hash_prefix('V', 'A', 'L'),

    /** proposal for signing */
    proposal = detail::make_hash_prefix('P', 'R', 'P'),

    /** Manifest */
    manifest = detail::make_hash_prefix('M', 'A', 'N'),

    /** Payment Channel Claim */
    paymentChannelClaim = detail::make_hash_prefix('C', 'L', 'M'),

    /** shard info for signing */
    shardInfo = detail::make_hash_prefix('S', 'H', 'D'),
};

template <class Hasher>
void
hash_append(Hasher& h, HashPrefix const& hp) noexcept
{
    using beast::hash_append;
    hash_append(h, static_cast<std::uint32_t>(hp));
}

}  // namespace ripple

#endif
