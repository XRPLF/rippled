#include <xrpld/app/ledger/LedgerPersistence.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/HashRouter.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/PendingSaves.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/rdb/RelationalDatabase.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>

namespace xrpl {

static bool
saveValidatedLedger(
    ServiceRegistry& registry,
    std::shared_ptr<Ledger const> const& ledger,
    bool current)
{
    auto j = registry.getJournal("Ledger");
    auto seq = ledger->header().seq;
    if (!registry.getPendingSaves().startWork(seq))
    {
        // The save was completed synchronously
        JLOG(j.debug()) << "Save aborted";
        return true;
    }

    auto& db = registry.getRelationalDatabase();

    auto const res = db.saveValidatedLedger(ledger, current);

    // Clients can now trust the database for
    // information about this ledger sequence.
    registry.getPendingSaves().finishWork(seq);
    return res;
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
        // We have tried to save this ledger recently
        auto stream = registry.getJournal("Ledger").debug();
        JLOG(stream) << "Double pend save for " << ledger->header().seq;

        if (!isSynchronous || !registry.getPendingSaves().pending(ledger->header().seq))
        {
            // Either we don't need it to be finished
            // or it is finished
            return true;
        }
    }

    XRPL_ASSERT(ledger->isImmutable(), "xrpl::pendSaveValidated : immutable ledger");

    if (!registry.getPendingSaves().shouldWork(ledger->header().seq, isSynchronous))
    {
        auto stream = registry.getJournal("Ledger").debug();
        JLOG(stream) << "Pend save with seq in pending saves " << ledger->header().seq;

        return true;
    }

    // See if we can use the JobQueue.
    if (!isSynchronous &&
        registry.getJobQueue().addJob(
            isCurrent ? JtPubledger : JtPuboldledger,
            "Pub" + std::to_string(ledger->seq()),
            [&registry, ledger, isCurrent]() { saveValidatedLedger(registry, ledger, isCurrent); }))
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

static void
finishLoadByIndexOrHash(std::shared_ptr<Ledger> const& ledger, beast::Journal j)
{
    if (!ledger)
        return;

    XRPL_ASSERT(
        ledger->header().seq < kXrpLedgerEarliestFees || ledger->read(keylet::fees()),
        "xrpl::finishLoadByIndexOrHash : valid ledger fees");
    ledger->setImmutable();

    JLOG(j.trace()) << "Loaded ledger: " << to_string(ledger->header().hash);

    ledger->setFull();
}

std::tuple<std::shared_ptr<Ledger>, std::uint32_t, uint256>
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
    uint256 const& ledgerHash,
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
