#pragma once

#include <xrpld/app/main/Application.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/protocol/Protocol.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

namespace xrpl {

class TransactionMaster;

/**
 * class to create database, launch online delete thread, and
 * related SQLite database
 */
class SHAMapStore
{
public:
    /**
     * Parse node store settings without opening the store or its state database.
     */
    struct Setup
    {
        std::uint32_t deleteInterval = 0;
        bool advisoryDelete = false;
        std::uint32_t deleteBatch = 100;
        std::chrono::milliseconds backOff{100};
        std::chrono::seconds ageThreshold{60};
        /**
         * If the node is out of sync, or any recent ledgers are not
         * available during an online_delete healthWait() call, sleep
         * the thread for this time, and continue checking until recovery.
         * See also: "recovery_wait_seconds" in xrpld-example.cfg
         */
        std::chrono::seconds recoveryWaitTime{2};
        /**
         * If the rotation stays "unhealthy" for a very long time, the process is aborted, and tried
         * again later. This value represents the number of ledgers that must be validated without
         * making rotation progress before the process is aborted.
         */
        std::uint32_t maxWaitingLedgers = deleteBatch;

        explicit Setup(Config& config);
    };

    virtual ~SHAMapStore() = default;

    /**
     * Called by LedgerMaster every time a ledger validates.
     */
    virtual void
    onLedgerClosed(std::shared_ptr<Ledger const> const& ledger) = 0;

    virtual void
    start() = 0;

    [[nodiscard]] virtual bool
    rendezvous(std::optional<std::chrono::milliseconds> const& timeout = {}) const = 0;

    virtual void
    stop() = 0;

    [[nodiscard]] virtual std::uint32_t
    clampFetchDepth(std::uint32_t fetchDepth) const = 0;

    virtual std::unique_ptr<node_store::Database>
    makeNodeStore(int readThreads) = 0;

    /**
     * Highest ledger that may be deleted.
     */
    virtual LedgerIndex
    setCanDelete(LedgerIndex canDelete) = 0;

    /**
     * Whether advisory delete is enabled.
     */
    [[nodiscard]] virtual bool
    advisoryDelete() const = 0;

    /**
     * Maximum ledger that has been deleted, or will be deleted if
     *  currently in the act of online deletion.
     */
    virtual LedgerIndex
    getLastRotated() = 0;

    /**
     * Highest ledger that may be deleted.
     */
    virtual LedgerIndex
    getCanDelete() = 0;

    /**
     * Returns the number of file descriptors that are needed.
     */
    [[nodiscard]] virtual int
    fdRequired() const = 0;

    /**
     * The minimum ledger to try and maintain in our database.
     *
     * This defines the lower bound for attempting to acquire historical
     * ledgers over the peer to peer network.
     *
     * If online_delete is enabled, then each time online_delete executes
     * and just prior to clearing SQL databases of historical ledgers,
     * move the value forward to one past the greatest ledger being deleted.
     * This minimizes fetching of ledgers that are in the process of being
     * deleted. Without online_delete or before online_delete is
     * executed, this value is always the minimum value persisted in the
     * ledger database, if any.
     *
     * @return The minimum ledger sequence to keep online based on the
     *     description above. If not set, then an unseated optional.
     */
    [[nodiscard]] virtual std::optional<LedgerIndex>
    minimumOnline() const = 0;
};

//------------------------------------------------------------------------------

std::unique_ptr<SHAMapStore>
makeSHAMapStore(Application& app, node_store::Scheduler& scheduler, beast::Journal journal);
}  // namespace xrpl
