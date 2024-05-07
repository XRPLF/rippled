//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_LEDGERHEADER_H_INCLUDED
#define RIPPLE_PROTOCOL_LEDGERHEADER_H_INCLUDED

#include <ripple/basics/Slice.h>
#include <ripple/basics/XRPAmount.h>
#include <ripple/basics/base_uint.h>
#include <ripple/basics/chrono.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/Serializer.h>

namespace ripple {

/** Information about the notional ledger backing the view. */
struct LedgerHeader
{
    explicit LedgerHeader() = default;

    //
    // For all ledgers
    //

    LedgerIndex seq = 0;
    NetClock::time_point parentCloseTime = {};

    //
    // For closed ledgers
    //

    // Closed means "tx set already determined"
    uint256 hash = beast::zero;
    uint256 txHash = beast::zero;
    uint256 accountHash = beast::zero;
    uint256 parentHash = beast::zero;

    XRPAmount drops = beast::zero;

    // If validated is false, it means "not yet validated."
    // Once validated is true, it will never be set false at a later time.
    // VFALCO TODO Make this not mutable
    bool mutable validated = false;
    bool accepted = false;

    // flags indicating how this ledger close took place
    int closeFlags = 0;

    // the resolution for this ledger close time (2-120 seconds)
    NetClock::duration closeTimeResolution = {};

    // For closed ledgers, the time the ledger
    // closed. For open ledgers, the time the ledger
    // will close if there's no transactions.
    //
    NetClock::time_point closeTime = {};
};

// We call them "headers" in conversation
// but "info" in code. Unintuitive.
// This alias lets us give the "correct" name to the class
// without yet disturbing existing uses.
using LedgerInfo = LedgerHeader;

// ledger close flags
static std::uint32_t const sLCF_NoConsensusTime = 0x01;

inline bool
getCloseAgree(LedgerHeader const& info)
{
    return (info.closeFlags & sLCF_NoConsensusTime) == 0;
}

void
addRaw(LedgerHeader const&, Serializer&, bool includeHash = false);

/** Deserialize a ledger header from a byte array. */
LedgerHeader
deserializeHeader(Slice data, bool hasHash = false);

/** Deserialize a ledger header (prefixed with 4 bytes) from a byte array. */
LedgerHeader
deserializePrefixedHeader(Slice data, bool hasHash = false);

}  // namespace ripple

#endif
