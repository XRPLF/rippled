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

/** Prefix for hashing functions.

    These prefixes are inserted before the source material used to generate
    various hashes. This is done to put each hash in its own "space." This way,
    two different types of objects with the same binary data will produce
    different hashes.

    Each prefix is a 4-byte value with the last byte set to zero and the first
    three bytes formed from the ASCII equivalent of some arbitrary string. For
    example "TXN".

    @note Hash prefixes are part of the Ripple protocol.

    @ingroup protocol
*/
class HashPrefix
{
private:
    std::uint32_t m_prefix;

    HashPrefix (char a, char b, char c)
        : m_prefix (0)
    {
        m_prefix = a;
        m_prefix = (m_prefix << 8) + b;
        m_prefix = (m_prefix << 8) + c;
        m_prefix = m_prefix << 8;
    }

public:
    HashPrefix(HashPrefix const&) = delete;
    HashPrefix& operator=(HashPrefix const&) = delete;

    /** Returns the hash prefix associated with this object */
    operator std::uint32_t () const
    {
        return m_prefix;
    }

    // VFALCO TODO Expand the description to complete, concise sentences.
    //

    /** transaction plus signature to give transaction ID */
    static HashPrefix const transactionID;

    /** transaction plus metadata */
    static HashPrefix const txNode;

    /** account state */
    static HashPrefix const leafNode;

    /** inner node in V1 tree */
    static HashPrefix const innerNode;

    /** inner node in V2 tree */
    static HashPrefix const innerNodeV2;

    /** ledger master data for signing */
    static HashPrefix const ledgerMaster;

    /** inner transaction to sign */
    static HashPrefix const txSign;

    /** inner transaction to multi-sign */
    static HashPrefix const txMultiSign;

    /** validation for signing */
    static HashPrefix const validation;

    /** proposal for signing */
    static HashPrefix const proposal;

    /** Manifest */
    static HashPrefix const manifest;

    /** Payment Channel Claim */
    static HashPrefix const paymentChannelClaim;
};

template <class Hasher>
void
hash_append (Hasher& h, HashPrefix const& hp) noexcept
{
    using beast::hash_append;
    hash_append(h,
        static_cast<std::uint32_t>(hp));
}

} // ripple

#endif
