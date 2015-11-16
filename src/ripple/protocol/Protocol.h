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

#ifndef RIPPLE_PROTOCOL_PROTOCOL_H_INCLUDED
#define RIPPLE_PROTOCOL_PROTOCOL_H_INCLUDED

#include <ripple/basics/base_uint.h>
#include <cstdint>

namespace ripple {

/** Protocol specific constants, types, and data.

    This information is part of the Ripple protocol. Specifically,
    it is required for peers to be able to communicate with each other.

    @note Changing these will create a hard fork.

    @ingroup protocol
    @defgroup protocol
*/
struct Protocol
{
    /** Smallest legal byte size of a transaction.
    */
    static int const txMinSizeBytes = 32;

    /** Largest legal byte size of a transaction.
    */
    static int const txMaxSizeBytes = 1024 * 1024; // 1048576
};

/** A ledger index. */
using LedgerIndex = std::uint32_t;

/** A transaction identifier.
    The value is computed as the hash of the
    canonicalized, serialized transaction object.
*/
using TxID = uint256;

using TxSeq = std::uint32_t;

} // ripple

#endif
