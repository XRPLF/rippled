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

#include <ripple/protocol/Deserializer.h>
#include <ripple/protocol/LedgerHeader.h>

namespace ripple {

void
serializeLedgerHeader(
    LedgerHeader const& header,
    SerializerBase& s,
    bool includeHash)
{
    s.add32(header.seq);
    s.add64(header.drops.drops());
    s.addBitString(header.parentHash);
    s.addBitString(header.txHash);
    s.addBitString(header.accountHash);
    s.add32(header.parentCloseTime.time_since_epoch().count());
    s.add32(header.closeTime.time_since_epoch().count());
    s.add8(header.closeTimeResolution.count());
    s.add8(header.closeFlags);
    if (includeHash)
        s.addBitString(header.hash);
}

LedgerHeader
deserializeHeader(Slice data, bool hasHash)
{
    SerialIter sit(data.data(), data.size());

    LedgerHeader header;

    header.seq = sit.get32();
    header.drops = sit.get64();
    header.parentHash = sit.get256();
    header.txHash = sit.get256();
    header.accountHash = sit.get256();
    header.parentCloseTime =
        NetClock::time_point{NetClock::duration{sit.get32()}};
    header.closeTime = NetClock::time_point{NetClock::duration{sit.get32()}};
    header.closeTimeResolution = NetClock::duration{sit.get8()};
    header.closeFlags = sit.get8();

    if (hasHash)
        header.hash = sit.get256();

    return header;
}

LedgerHeader
deserializePrefixedHeader(Slice data, bool hasHash)
{
    return deserializeHeader(data + 4, hasHash);
}

}  // namespace ripple
