#include <xrpl/basics/Slice.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/Serializer.h>

namespace ripple {

void
addRaw(LedgerHeader const& info, Serializer& s, bool includeHash)
{
    s.add32(info.seq);
    s.add64(info.drops.drops());
    s.addBitString(info.parentHash);
    s.addBitString(info.txHash);
    s.addBitString(info.accountHash);
    s.add32(info.parentCloseTime.time_since_epoch().count());
    s.add32(info.closeTime.time_since_epoch().count());
    s.add8(info.closeTimeResolution.count());
    s.add8(info.closeFlags);

    if (includeHash)
        s.addBitString(info.hash);
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
