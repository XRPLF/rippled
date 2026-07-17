#pragma once

#include <xrpld/app/ledger/InboundLedger.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Peer.h>

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/clock/abstract_clock.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/protocol/RippleLedgerHash.h>

#include <xrpl.pb.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace xrpl {

// Per-node cap for AS state leaves stashed via `gotStaleData`.
//
// `gotStaleData` only handles `liAS_NODE` payloads, which carry
// SHAMap state-map leaves (ledger objects).
//
// Sizing: worst-case serialized size across all 31 ledger entry
// types is ~53 KB (`XChainOwnedCreateAccountClaimID`, 256
// attestations x ~209 B, capped by `kMaxAttestations` in
// `include/xrpl/protocol/XChainAttestations.h`), followed by
// `XChainOwnedClaimID` ~40 KB, `NFTokenPage` ~9.5 KB, and
// `LedgerHashes` ~8.2 KB. 256 KiB leaves ~4.8x headroom over the
// current worst case.
//
// Future-proofing: this cap is NOT derived from a single protocol
// constant — it is a soft bound over independently-tuned caps
// (`kMaxAttestations`, `kDirMaxTokensPerPage`, `kMaxTokenUriLength`,
// etc.). Two types (`Amendments`, `NegativeUNL`) have no hard schema
// cap and grow with network state. Revisit if a new object type or
// a lifted array cap approaches ~256 KiB. The downstream
// `SHAMapAccountStateLeafNode` construction rejects anything above
// the 16 MiB SHAMapItem invariant regardless.
inline constexpr std::size_t kMaxFetchPackNodeBytes = 256 * 1024;

// Aggregate cap on the sum of `nodedata().size()` across all entries
// in a single `TMLedgerData` message. Rejects amplification-shaped
// payloads (many nodes, each individually under `kMaxFetchPackNodeBytes`,
// that together dwarf the per-message budget) at ingress in PeerImp,
// before dispatch into `InboundLedger::gotData` or `gotStaleData`.
inline constexpr std::size_t kMaxLedgerDataBytes = megabytes(1);

/**
 * Manages the lifetime of inbound ledgers.
 *
 * @see InboundLedger
 */
class InboundLedgers
{
public:
    using clock_type = beast::AbstractClock<std::chrono::steady_clock>;

    virtual ~InboundLedgers() = default;

    // Callers should use this if they possibly need an authoritative
    // response immediately.
    virtual std::shared_ptr<Ledger const>
    acquire(uint256 const& hash, std::uint32_t seq, InboundLedger::Reason) = 0;

    // Callers should use this if they are known to be executing on the Job
    // Queue. TODO review whether all callers of acquire() can use this
    // instead. Inbound ledger acquisition is asynchronous anyway.
    virtual void
    acquireAsync(uint256 const& hash, std::uint32_t seq, InboundLedger::Reason reason) = 0;

    virtual std::shared_ptr<InboundLedger>
    find(LedgerHash const& hash) = 0;

    // VFALCO TODO Remove the dependency on the Peer object.
    //
    virtual bool
    gotLedgerData(
        LedgerHash const& ledgerHash,
        std::shared_ptr<Peer>,
        std::shared_ptr<protocol::TMLedgerData>) = 0;

    virtual void
    gotStaleData(std::shared_ptr<protocol::TMLedgerData> packet) = 0;

    virtual void
    logFailure(uint256 const& h, std::uint32_t seq) = 0;

    virtual bool
    isFailure(uint256 const& h) = 0;

    virtual void
    clearFailures() = 0;

    virtual json::Value
    getInfo() = 0;

    /**
     * Returns the rate of historical ledger fetches per minute.
     */
    virtual std::size_t
    fetchRate() = 0;

    /**
     * Called when a complete ledger is obtained.
     */
    virtual void
    onLedgerFetched() = 0;

    virtual void
    gotFetchPack() = 0;
    virtual void
    sweep() = 0;

    virtual void
    stop() = 0;

    virtual std::size_t
    cacheSize() = 0;
};

std::unique_ptr<InboundLedgers>
makeInboundLedgers(
    Application& app,
    InboundLedgers::clock_type& clock,
    beast::insight::Collector::ptr const& collector);

}  // namespace xrpl
