#ifndef XRPL_PROTOCOL_LEDGERHEADER_H_INCLUDED
#define XRPL_PROTOCOL_LEDGERHEADER_H_INCLUDED

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/XRPAmount.h>

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
