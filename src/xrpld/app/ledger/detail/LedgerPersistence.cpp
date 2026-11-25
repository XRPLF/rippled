#include <xrpld/app/ledger/LedgerPersistence.h>

#include <xrpld/app/ledger/LedgerMaster.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/HashRouter.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/PendingSaves.h>
#include <xrpl/ledger/entries/FeeSettingsEntry.h>
#include <xrpl/protocol/Indexes.h>  // IWYU pragma: keep
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SystemParameters.h>  // IWYU pragma: keep
#include <xrpl/rdb/RelationalDatabase.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>

namespace xrpl {

/**
 * Release everything a save that did not happen left behind.
 *
 * The ledger's row may be gone, so the sequence stops being advertised as
 * complete. Neither the in-progress mark nor the SAVED flag may outlive the
 * save, or a retry would be deduplicated away. They are released in the
 * reverse of the order pendSaveValidated() takes them, or a concurrent enqueue
 * can re-arm the flag.
 *
 * @param registry The services that hold the range, the pending saves and
 *        the flags.
 * @param ledger The ledger whose save failed.
 */
static void
releaseFailedSave(ServiceRegistry& registry, Ledger const& ledger)
{
    registry.getLedgerMaster().clearLedger(ledger.header().seq);
    registry.getPendingSaves().finishWork(ledger.header().seq);
    registry.getHashRouter().clearFlags(ledger.header().hash, HashRouterFlags::SAVED);
}

/**
 * Write a validated ledger to the relational database.
 *
 * The caller owns the pending-saves entry for the sequence. The entry is
 * finished on every exit, so a save that fails leaves no sequence marked in
 * progress: a later save of the same ledger can run, and a synchronous caller
 * waiting on it wakes up. A save that throws, or that the database reports as
 * not done, also releases the SAVED flag and the complete-range entry through
 * releaseFailedSave().
 *
 * @param registry The services that hold the pending saves and the database.
 * @param ledger The ledger to save.
 * @param current Whether the ledger is the current validated ledger.
 * @return Whether the database reported the save as done.
 */
static bool
writeValidatedLedger(
    ServiceRegistry& registry,
    std::shared_ptr<Ledger const> const& ledger,
    bool current)
{
    auto const seq = ledger->header().seq;
    auto& db = registry.getRelationalDatabase();

    bool res = false;
    try
    {
        res = db.saveValidatedLedger(ledger, current);
    }
    catch (...)
    {
        releaseFailedSave(registry, *ledger);
        throw;
    }

    if (!res)
    {
        // The database did not write the ledger, so nothing may claim that it did.
        releaseFailedSave(registry, *ledger);
        return false;
    }

    // Clients can now trust the database for information about this ledger sequence.
    registry.getPendingSaves().finishWork(seq);
    return true;
}

/**
 * Save a scheduled validated ledger, unless another thread took the work.
 *
 * @param registry The services that hold the pending saves and the database.
 * @param ledger The ledger to save.
 * @param current Whether the ledger is the current validated ledger.
 * @return Whether the database reported the save as done, or true when another
 *         thread took the work.
 */
static bool
saveValidatedLedger(
    ServiceRegistry& registry,
    std::shared_ptr<Ledger const> const& ledger,
    bool current)
{
    if (!registry.getPendingSaves().startWork(ledger->header().seq))
    {
        // The save was completed synchronously
        JLOG(registry.getJournal("Ledger").debug()) << "Save aborted";
        return true;
    }

    return writeValidatedLedger(registry, ledger, current);
}

bool
pendSaveValidated(
    ServiceRegistry& registry,
    std::shared_ptr<Ledger const> const& ledger,
    bool isSynchronous,
    bool isCurrent)
{
    if (!registry.getHashRouter().setFlags(ledger->header().hash, HashRouterFlags::SAVED))
    {
        // We have tried to save this ledger recently. That is enough for an asynchronous
        // caller: the earlier save, or the retry after its failure, covers it. A synchronous
        // caller must not trust the flag, since a save that failed clears it separately from
        // its pending entry, so a retry could read the flag before that cleanup and the entry
        // after it. It always claims and performs the write itself.
        auto const stream = registry.getJournal("Ledger").debug();
        JLOG(stream) << "Double pend save for " << ledger->header().seq;

        if (!isSynchronous)
            return true;
    }

    XRPL_ASSERT(ledger->isImmutable(), "xrpl::pendSaveValidated : immutable ledger");

    if (isSynchronous)
    {
        // Own the write outright, after any save of the sequence in progress. Taking it in the
        // two steps the asynchronous path uses would let another thread win the second step,
        // and this caller would then return before the ledger is written.
        registry.getPendingSaves().claimWork(ledger->header().seq);
        return writeValidatedLedger(registry, ledger, isCurrent);
    }

    if (!registry.getPendingSaves().shouldWork(ledger->header().seq))
    {
        auto const stream = registry.getJournal("Ledger").debug();
        JLOG(stream) << "Pend save with seq in pending saves " << ledger->header().seq;

        return true;
    }

    // See if we can use the JobQueue.
    if (registry.getJobQueue().addJob(
            isCurrent ? JtPubledger : JtPuboldledger,
            "Pub" + std::to_string(ledger->seq()),
            [&registry, ledger, isCurrent] { saveValidatedLedger(registry, ledger, isCurrent); }))
    {
        return true;
    }

    // The JobQueue won't do the Job.  Do the save synchronously.
    return saveValidatedLedger(registry, ledger, isCurrent);
}

std::shared_ptr<Ledger>
loadLedgerHelper(
    LedgerHeader const& info,
    Rules const& rules,
    Fees const& fees,
    ServiceRegistry& registry,
    bool acquire)
{
    bool loaded = false;
    auto ledger = std::make_shared<Ledger>(
        info,
        loaded,
        acquire,
        rules,
        fees,
        registry.getNodeFamily(),
        registry.getJournal("Ledger"));

    if (!loaded)
        ledger.reset();

    return ledger;
}

/**
 * Settle a ledger just loaded from local storage, or discard it.
 *
 * @param ledger The ledger to settle. Cleared on failure, so the caller cannot
 *        hand out one that is still mutable.
 * @param j Where to log a refusal.
 */
static void
finishLoadByIndexOrHash(std::shared_ptr<Ledger>& ledger, beast::Journal j)
{
    if (!ledger)
        return;

    XRPL_ASSERT(
        ledger->header().seq < kXrpLedgerEarliestFees || FeeSettingsEntryR(*ledger),
        "xrpl::finishLoadByIndexOrHash : valid ledger fees");
    // Loaded locally. See Ledger::setImmutable().
    if (!ledger->setImmutable())
    {
        // LCOV_EXCL_START
        JLOG(j.error()) << "Invalid map for ledger " << ledger->header().seq
                        << "; not marking it as loaded";
        UNREACHABLE("xrpl::finishLoadByIndexOrHash : map is invalid");
        // Discarded rather than left un-full, since nothing gates usability on the full flag.
        ledger.reset();
        return;
        // LCOV_EXCL_STOP
    }

    JLOG(j.trace()) << "Loaded ledger: " << to_string(ledger->header().hash);

    ledger->setFull();
}

std::tuple<std::shared_ptr<Ledger>, std::uint32_t, UInt256>
getLatestLedger(Rules const& rules, Fees const& fees, ServiceRegistry& registry)
{
    std::optional<LedgerHeader> const info = registry.getRelationalDatabase().getNewestLedgerInfo();
    if (!info)
        return {std::shared_ptr<Ledger>(), {}, {}};
    return {loadLedgerHelper(*info, rules, fees, registry, true), info->seq, info->hash};
}

std::shared_ptr<Ledger>
loadByIndex(
    std::uint32_t ledgerIndex,
    Rules const& rules,
    Fees const& fees,
    ServiceRegistry& registry,
    bool acquire)
{
    if (std::optional<LedgerHeader> info =
            registry.getRelationalDatabase().getLedgerInfoByIndex(ledgerIndex))
    {
        std::shared_ptr<Ledger> ledger = loadLedgerHelper(*info, rules, fees, registry, acquire);
        finishLoadByIndexOrHash(ledger, registry.getJournal("Ledger"));
        return ledger;
    }
    return {};
}

std::shared_ptr<Ledger>
loadByHash(
    UInt256 const& ledgerHash,
    Rules const& rules,
    Fees const& fees,
    ServiceRegistry& registry,
    bool acquire)
{
    if (std::optional<LedgerHeader> info =
            registry.getRelationalDatabase().getLedgerInfoByHash(ledgerHash))
    {
        std::shared_ptr<Ledger> ledger = loadLedgerHelper(*info, rules, fees, registry, acquire);
        finishLoadByIndexOrHash(ledger, registry.getJournal("Ledger"));
        XRPL_ASSERT(
            !ledger || ledger->header().hash == ledgerHash,
            "xrpl::loadByHash : ledger hash match if loaded");
        return ledger;
    }
    return {};
}

}  // namespace xrpl
