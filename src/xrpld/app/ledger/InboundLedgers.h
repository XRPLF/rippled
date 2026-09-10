#pragma once

#include <xrpld/app/ledger/InboundLedger.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Peer.h>

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

    /**
     * Aggregate acquire-progress snapshot across every in-flight acquire.
     *
     * Bounded, pre-aggregated telemetry: one value per field regardless of how
     * many acquires are in flight, so the derived metric cannot grow a series
     * per ledger. Per-ledger detail stays available on the `ledger.acquire`
     * span, which is where unbounded identity belongs.
     */
    struct AcquireProgress
    {
        /**
         * Largest outstanding account-state node count of any in-flight
         * acquire. The max, not the sum, so one stuck acquire stays visible
         * instead of being averaged away by healthy ones.
         */
        int maxMissingStateNodes{0};

        /**
         * Largest outstanding transaction-tree node count of any in-flight
         * acquire.
         */
        int maxMissingTxNodes{0};

        /**
         * Total unprocessed peer packets stashed across all in-flight acquires.
         * Summed because it measures one shared processing backlog.
         */
        std::size_t receivedDataDepth{0};

        /**
         * Number of acquires currently in flight, so the three values above can
         * be read in context (all zero with zero acquires is idle, not healthy).
         */
        std::size_t inFlight{0};
    };

    /**
     * Collect the aggregate acquire-progress snapshot.
     *
     * Intended for the telemetry observable gauge, polled about every 10 s.
     * Takes the collection lock only long enough to copy the handles, then reads
     * each acquire's relaxed atomics without holding it, so it never blocks the
     * node-receive path.
     *
     * @return Aggregated progress; an all-zero value with `inFlight == 0` when
     *         nothing is being acquired.
     */
    [[nodiscard]] virtual AcquireProgress
    acquireProgress() = 0;
};

std::unique_ptr<InboundLedgers>
makeInboundLedgers(
    Application& app,
    InboundLedgers::clock_type& clock,
    beast::insight::Collector::ptr const& collector);

}  // namespace xrpl
