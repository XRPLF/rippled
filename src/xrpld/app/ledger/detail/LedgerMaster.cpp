#include <xrpld/app/ledger/LedgerMaster.h>

#include <xrpld/app/consensus/RCLValidations.h>
#include <xrpld/app/ledger/InboundLedger.h>
#include <xrpld/app/ledger/LedgerPersistence.h>
#include <xrpld/app/ledger/LedgerReplay.h>
#include <xrpld/app/ledger/LedgerReplayer.h>
#include <xrpld/app/ledger/OpenLedger.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/SHAMapStore.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/core/Config.h>
#include <xrpld/core/TimeKeeper.h>
#include <xrpld/overlay/Overlay.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/rpc/detail/PathRequestManager.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/MathUtilities.h>
#include <xrpl/basics/RangeSet.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/UptimeClock.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/basics/scope.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/Job.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/AmendmentTable.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/ledger/OrderBookDB.h>
#include <xrpl/ledger/PendingSaves.h>
#include <xrpl/ledger/View.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/protocol/BuildInfo.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/RippleLedgerHash.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/rdb/RelationalDatabase.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/server/LoadFeeTrack.h>
#include <xrpl/server/NetworkOPs.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapMissingNode.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <boost/icl/concept/interval_set.hpp>

#include <xrpl.pb.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <sstream>
#include <utility>
#include <vector>

namespace xrpl {

// Don't catch up more than 100 ledgers (cannot exceed 256)
static constexpr int kMaxLedgerGap{100};

// Don't acquire history if ledger is too old
static constexpr std::chrono::minutes kMaxLedgerAgeAcquire{1};

// Don't acquire history if write load is too high
static constexpr int kMaxWriteLoadAcquire{8192};

// Helper function for LedgerMaster::doAdvance()
// Return true if candidateLedger should be fetched from the network.
static bool
shouldAcquire(
    std::uint32_t const currentLedger,
    std::uint32_t const ledgerHistory,
    std::optional<LedgerIndex> const minimumOnline,
    std::uint32_t const candidateLedger,
    beast::Journal j)
{
    bool const ret = [&]() {
        // Fetch ledger if it may be the current ledger
        if (candidateLedger >= currentLedger)
            return true;

        // Or if it is within our configured history range:
        if (currentLedger - candidateLedger <= ledgerHistory)
            return true;

        // Or if greater than or equal to a specific minimum ledger.
        // Do nothing if the minimum ledger to keep online is unknown.
        return minimumOnline.has_value() && candidateLedger >= *minimumOnline;
    }();

    JLOG(j.trace()) << "Missing ledger " << candidateLedger << (ret ? " should" : " should NOT")
                    << " be acquired";
    return ret;
}

LedgerMaster::LedgerMaster(
    Application& app,
    Stopwatch& stopwatch,
    beast::insight::Collector::ptr const& collector,
    beast::Journal journal)
    : app_(app)
    , journal_(journal)
    , ledgerHistory_(collector, app)
    , standalone_(app_.config().standalone())
    , fetchDepth_(app_.getSHAMapStore().clampFetchDepth(app_.config().fetchDepth))
    , ledgerHistorySize_(app_.config().ledgerHistory)
    , ledgerFetchSize_(app_.config().getValueFor(SizedItem::LedgerFetch))
    , fetchPacks_(
          "FetchPack",
          65536,
          std::chrono::seconds{45},
          stopwatch,
          app_.getJournal("TaggedCache"))
    , stats_(std::bind(&LedgerMaster::collectMetrics, this), collector)
{
}

LedgerIndex
LedgerMaster::getCurrentLedgerIndex()
{
    return app_.getOpenLedger().current()->header().seq;
}

LedgerIndex
LedgerMaster::getValidLedgerIndex()
{
    return validLedgerSeq_;
}

bool
LedgerMaster::isCompatible(ReadView const& view, beast::Journal::Stream s, char const* reason)
{
    auto validLedger = getValidatedLedger();

    if (validLedger && !areCompatible(*validLedger, view, s, reason))
    {
        return false;
    }

    {
        std::scoped_lock const sl(mutex_);

        if ((lastValidLedger_.second != 0) &&
            !areCompatible(lastValidLedger_.first, lastValidLedger_.second, view, s, reason))
        {
            return false;
        }
    }

    return true;
}

std::chrono::seconds
LedgerMaster::getPublishedLedgerAge()
{
    using namespace std::chrono_literals;
    std::chrono::seconds const pubClose{pubLedgerClose_.load()};
    if (pubClose == 0s)
    {
        JLOG(journal_.debug()) << "No published ledger";
        return weeks{2};
    }

    std::chrono::seconds ret = app_.getTimeKeeper().closeTime().time_since_epoch();
    ret -= pubClose;
    ret = (ret > 0s) ? ret : 0s;
    static std::chrono::seconds kLastRet = -1s;

    if (ret != kLastRet)
    {
        JLOG(journal_.trace()) << "Published ledger age is " << ret.count();
        kLastRet = ret;
    }
    return ret;
}

std::chrono::seconds
LedgerMaster::getValidatedLedgerAge()
{
    using namespace std::chrono_literals;

    std::chrono::seconds const valClose{validLedgerSign_.load()};
    if (valClose == 0s)
    {
        JLOG(journal_.debug()) << "No validated ledger";
        return weeks{2};
    }

    std::chrono::seconds ret = app_.getTimeKeeper().closeTime().time_since_epoch();
    ret -= valClose;
    ret = (ret > 0s) ? ret : 0s;
    static std::chrono::seconds kLastRet = -1s;

    if (ret != kLastRet)
    {
        JLOG(journal_.trace()) << "Validated ledger age is " << ret.count();
        kLastRet = ret;
    }
    return ret;
}

bool
LedgerMaster::isCaughtUp(std::string& reason)
{
    using namespace std::chrono_literals;

    if (getPublishedLedgerAge() > 3min)
    {
        reason = "No recently-published ledger";
        return false;
    }
    std::uint32_t const validClose = validLedgerSign_.load();
    std::uint32_t const pubClose = pubLedgerClose_.load();
    if ((validClose == 0u) || (pubClose == 0u))
    {
        reason = "No published ledger";
        return false;
    }
    if (validClose > (pubClose + 90))
    {
        reason = "Published ledger lags validated ledger";
        return false;
    }
    return true;
}

void
LedgerMaster::setValidLedger(std::shared_ptr<Ledger const> const& l)
{
    std::vector<NetClock::time_point> times;
    std::optional<uint256> consensusHash;

    if (!standalone_)
    {
        auto validations = app_.getValidators().negativeUNLFilter(
            app_.getValidations().getTrustedForLedger(l->header().hash, l->header().seq));
        times.reserve(validations.size());
        for (auto const& val : validations)
            times.push_back(val->getSignTime());

        if (!validations.empty())
            consensusHash = validations.front()->getConsensusHash();
    }

    NetClock::time_point signTime;

    if (!times.empty() && times.size() >= app_.getValidators().quorum())
    {
        // Calculate the sample median
        std::ranges::sort(times);
        auto const t0 = times[(times.size() - 1) / 2];
        auto const t1 = times[times.size() / 2];
        signTime = t0 + (t1 - t0) / 2;
    }
    else
    {
        signTime = l->header().closeTime;
    }

    validLedger_.set(l);
    validLedgerSign_ = signTime.time_since_epoch().count();
    XRPL_ASSERT(
        validLedgerSeq_ || !app_.getMaxDisallowedLedger() ||
            l->header().seq + maxLedgerDifference_ > app_.getMaxDisallowedLedger(),
        "xrpl::LedgerMaster::setValidLedger : valid ledger sequence");
    (void)maxLedgerDifference_;
    validLedgerSeq_ = l->header().seq;

    app_.getOPs().updateLocalTx(*l);
    app_.getSHAMapStore().onLedgerClosed(getValidatedLedger());
    ledgerHistory_.validatedLedger(l, consensusHash);
    app_.getAmendmentTable().doValidatedLedger(l);
    if (!app_.getOPs().isBlocked())
    {
        if (app_.getAmendmentTable().hasUnsupportedEnabled())
        {
            JLOG(journal_.error()) << "One or more unsupported amendments "
                                      "activated: server blocked.";
            app_.getOPs().setAmendmentBlocked();
        }
        else if (!app_.getOPs().isAmendmentWarned() || l->isFlagLedger())
        {
            // Amendments can lose majority, so re-check periodically (every
            // flag ledger), and clear the flag if appropriate. If an unknown
            // amendment gains majority log a warning as soon as it's
            // discovered, then again every flag ledger until the operator
            // upgrades, the amendment loses majority, or the amendment goes
            // live and the node gets blocked. Unlike being amendment blocked,
            // this message may be logged more than once per session, because
            // the node will otherwise function normally, and this gives
            // operators an opportunity to see and resolve the warning.
            if (auto const first = app_.getAmendmentTable().firstUnsupportedExpected())
            {
                JLOG(journal_.error()) << "One or more unsupported amendments "
                                          "reached majority. Upgrade before "
                                       << to_string(*first)
                                       << " to prevent your server from "
                                          "becoming amendment blocked.";
                app_.getOPs().setAmendmentWarned();
            }
            else
            {
                app_.getOPs().clearAmendmentWarned();
            }
        }
    }
}

void
LedgerMaster::setPubLedger(std::shared_ptr<Ledger const> const& l)
{
    pubLedger_ = l;
    pubLedgerClose_ = l->header().closeTime.time_since_epoch().count();
    pubLedgerSeq_ = l->header().seq;
}

void
LedgerMaster::addHeldTransaction(std::shared_ptr<Transaction> const& transaction)
{
    std::scoped_lock const ml(mutex_);
    heldTransactions_.insert(transaction->getSTransaction());
}

// Validate a ledger's close time and sequence number if we're considering
// jumping to that ledger. This helps defend against some rare hostile or
// diverged majority scenarios.
bool
LedgerMaster::canBeCurrent(std::shared_ptr<Ledger const> const& ledger)
{
    XRPL_ASSERT(ledger, "xrpl::LedgerMaster::canBeCurrent : non-null input");

    // Never jump to a candidate ledger that precedes our
    // last validated ledger

    auto validLedger = getValidatedLedger();
    if (validLedger && (ledger->header().seq < validLedger->header().seq))
    {
        JLOG(journal_.trace()) << "Candidate for current ledger has low seq "
                               << ledger->header().seq << " < " << validLedger->header().seq;
        return false;
    }

    // Ensure this ledger's parent close time is within five minutes of
    // our current time. If we already have a known fully-valid ledger
    // we perform this check. Otherwise, we only do it if we've built a
    // few ledgers as our clock can be off when we first start up

    auto closeTime = app_.getTimeKeeper().closeTime();
    auto ledgerClose = ledger->header().parentCloseTime;

    using namespace std::chrono_literals;
    if ((validLedger || (ledger->header().seq > 10)) &&
        ((std::max(closeTime, ledgerClose) - std::min(closeTime, ledgerClose)) > 5min))
    {
        JLOG(journal_.warn()) << "Candidate for current ledger has close time "
                              << to_string(ledgerClose) << " at network time "
                              << to_string(closeTime) << " seq " << ledger->header().seq;
        return false;
    }

    if (validLedger)
    {
        // Sequence number must not be too high. We allow ten ledgers
        // for time inaccuracies plus a maximum run rate of one ledger
        // every two seconds. The goal is to prevent a malicious ledger
        // from increasing our sequence unreasonably high

        LedgerIndex maxSeq = validLedger->header().seq + 10;

        if (closeTime > validLedger->header().parentCloseTime)
        {
            maxSeq += std::chrono::duration_cast<std::chrono::seconds>(
                          closeTime - validLedger->header().parentCloseTime)
                          .count() /
                2;
        }

        if (ledger->header().seq > maxSeq)
        {
            JLOG(journal_.warn()) << "Candidate for current ledger has high seq "
                                  << ledger->header().seq << " > " << maxSeq;
            return false;
        }

        JLOG(journal_.trace()) << "Acceptable seq range: " << validLedger->header().seq
                               << " <= " << ledger->header().seq << " <= " << maxSeq;
    }

    return true;
}

void
LedgerMaster::switchLCL(std::shared_ptr<Ledger const> const& lastClosed)
{
    XRPL_ASSERT(lastClosed, "xrpl::LedgerMaster::switchLCL : non-null input");
    if (!lastClosed->isImmutable())
        logicError("mutable ledger in switchLCL");

    if (lastClosed->open())
        logicError("The new last closed ledger is open!");

    {
        std::scoped_lock const ml(mutex_);
        closedLedger_.set(lastClosed);
    }

    if (standalone_)
    {
        setFullLedger(lastClosed, true, false);
        tryAdvance();
    }
    else
    {
        checkAccept(lastClosed);
    }
}

bool
LedgerMaster::fixIndex(LedgerIndex ledgerIndex, LedgerHash const& ledgerHash)
{
    return ledgerHistory_.fixIndex(ledgerIndex, ledgerHash);
}

bool
LedgerMaster::storeLedger(std::shared_ptr<Ledger const> ledger)
{
    bool const validated = ledger->header().validated;
    // Returns true if we already had the ledger
    return ledgerHistory_.insert(ledger, validated);
}

/** Apply held transactions to the open ledger
    This is normally called as we close the ledger.
    The open ledger remains open to handle new transactions
    until a new open ledger is built.
*/
void
LedgerMaster::applyHeldTransactions()
{
    CanonicalTXSet const set = [this]() {
        std::scoped_lock const sl(mutex_);
        // VFALCO NOTE The hash for an open ledger is undefined so we use
        // something that is a reasonable substitute.
        CanonicalTXSet set(app_.getOpenLedger().current()->header().parentHash);
        std::swap(heldTransactions_, set);
        return set;
    }();

    if (!set.empty())
        app_.getOPs().processTransactionSet(set);
}

std::shared_ptr<STTx const>
LedgerMaster::popAcctTransaction(std::shared_ptr<STTx const> const& tx)
{
    std::scoped_lock const sl(mutex_);

    return heldTransactions_.popAcctTransaction(tx);
}

void
LedgerMaster::setBuildingLedger(LedgerIndex i)
{
    buildingLedgerSeq_.store(i);
}

bool
LedgerMaster::haveLedger(std::uint32_t seq)
{
    std::scoped_lock const sl(completeLock_);
    return boost::icl::contains(completeLedgers_, seq);
}

void
LedgerMaster::clearLedger(std::uint32_t seq)
{
    std::scoped_lock const sl(completeLock_);
    completeLedgers_.erase(seq);
}

bool
LedgerMaster::isValidated(ReadView const& ledger)
{
    if (ledger.open())
        return false;

    if (ledger.header().validated)
        return true;

    auto const seq = ledger.header().seq;
    try
    {
        // Use the skip list in the last validated ledger to see if ledger
        // comes before the last validated ledger (and thus has been
        // validated).
        auto const hash = walkHashBySeq(seq, InboundLedger::Reason::GENERIC);

        if (!hash || ledger.header().hash != *hash)
        {
            // This ledger's hash is not the hash of the validated ledger
            if (hash)
            {
                XRPL_ASSERT(hash->isNonZero(), "xrpl::LedgerMaster::isValidated : nonzero hash");
                uint256 const valHash = app_.getRelationalDatabase().getHashByIndex(seq);
                if (valHash == ledger.header().hash)
                {
                    // SQL database doesn't match ledger chain
                    clearLedger(seq);
                }
            }
            return false;
        }
    }
    catch (SHAMapMissingNode const& mn)
    {
        JLOG(journal_.warn()) << "Ledger #" << seq << ": " << mn.what();
        return false;
    }

    // Mark ledger as validated to save time if we see it again.
    ledger.header().validated = true;
    return true;
}

// returns Ledgers we have all the nodes for
bool
LedgerMaster::getFullValidatedRange(std::uint32_t& minVal, std::uint32_t& maxVal)
{
    // Validated ledger is likely not stored in the DB yet so we use the
    // published ledger which is.
    maxVal = pubLedgerSeq_.load();

    if (maxVal == 0u)
        return false;

    std::optional<std::uint32_t> maybeMin;
    {
        std::scoped_lock const sl(completeLock_);
        maybeMin = prevMissing(completeLedgers_, maxVal);
    }

    if (maybeMin == std::nullopt)
    {
        minVal = maxVal;
    }
    else
    {
        minVal = 1 + *maybeMin;
    }

    return true;
}

// Returns Ledgers we have all the nodes for and are indexed
bool
LedgerMaster::getValidatedRange(std::uint32_t& minVal, std::uint32_t& maxVal)
{
    if (!getFullValidatedRange(minVal, maxVal))
        return false;

    // Remove from the validated range any ledger sequences that may not be
    // fully updated in the database yet

    auto const pendingSaves = app_.getPendingSaves().getSnapshot();

    if (!pendingSaves.empty() && ((minVal != 0) || (maxVal != 0)))
    {
        // Ensure we shrink the tips as much as possible. If we have 7-9 and
        // 8,9 are invalid, we don't want to see the 8 and shrink to just 9
        // because then we'll have nothing when we could have 7.
        while (pendingSaves.contains(maxVal))
            --maxVal;
        while (pendingSaves.contains(minVal))
            ++minVal;

        // Best effort for remaining exclusions
        for (auto v : pendingSaves)
        {
            if ((v.first >= minVal) && (v.first <= maxVal))
            {
                if (v.first > ((minVal + maxVal) / 2))
                {
                    maxVal = v.first - 1;
                }
                else
                {
                    minVal = v.first + 1;
                }
            }
        }

        if (minVal > maxVal)
            minVal = maxVal = 0;
    }

    return true;
}

// Get the earliest ledger we will let peers fetch
std::uint32_t
LedgerMaster::getEarliestFetch()
{
    // The earliest ledger we will let people fetch is ledger zero,
    // unless that creates a larger range than allowed
    std::uint32_t e = getClosedLedger()->header().seq;

    if (e > fetchDepth_)
    {
        e -= fetchDepth_;
    }
    else
    {
        e = 0;
    }
    return e;
}

void
LedgerMaster::tryFill(std::shared_ptr<Ledger const> ledger)
{
    std::uint32_t seq = ledger->header().seq;
    uint256 prevHash = ledger->header().parentHash;

    std::map<std::uint32_t, LedgerHashPair> ledgerHashes;

    std::uint32_t minHas = seq;
    std::uint32_t maxHas = seq;

    NodeStore::Database& nodeStore{app_.getNodeStore()};
    while (!app_.getJobQueue().isStopping() && seq > 0)
    {
        {
            std::scoped_lock const ml(mutex_);
            minHas = seq;
            --seq;

            if (haveLedger(seq))
                break;
        }

        auto it(ledgerHashes.find(seq));

        if (it == ledgerHashes.end())
        {
            if (app_.isStopping())
                return;

            {
                std::scoped_lock const ml(completeLock_);
                completeLedgers_.insert(range(minHas, maxHas));
            }
            maxHas = minHas;
            ledgerHashes =
                app_.getRelationalDatabase().getHashesByIndex((seq < 500) ? 0 : (seq - 499), seq);
            it = ledgerHashes.find(seq);

            if (it == ledgerHashes.end())
                break;

            if (!nodeStore.fetchNodeObject(
                    ledgerHashes.begin()->second.ledgerHash, ledgerHashes.begin()->first))
            {
                // The ledger is not backed by the node store
                JLOG(journal_.warn())
                    << "SQL DB ledger sequence " << seq << " mismatches node store";
                break;
            }
        }

        if (it->second.ledgerHash != prevHash)
            break;

        prevHash = it->second.parentHash;
    }

    {
        std::scoped_lock const ml(completeLock_);
        completeLedgers_.insert(range(minHas, maxHas));
    }
    {
        std::scoped_lock const ml(mutex_);
        fillInProgress_ = 0;
        tryAdvance();
    }
}

/** Request a fetch pack to get to the specified ledger
 */
void
LedgerMaster::getFetchPack(LedgerIndex missing, InboundLedger::Reason reason)
{
    LedgerIndex const ledgerIndex = missing + 1;

    auto const haveHash{getLedgerHashForHistory(ledgerIndex, reason)};
    if (!haveHash || haveHash->isZero())
    {
        JLOG(journal_.error()) << "No hash for fetch pack. Missing Index " << missing;
        return;
    }

    // Select target Peer based on highest score.  The score is randomized
    // but biased in favor of Peers with low latency.
    std::shared_ptr<Peer> target;
    {
        int maxScore = 0;
        auto peerList = app_.getOverlay().getActivePeers();
        for (auto const& peer : peerList)
        {
            if (peer->hasRange(missing, missing + 1))
            {
                int const score = peer->getScore(true);
                if (!target || (score > maxScore))
                {
                    target = peer;
                    maxScore = score;
                }
            }
        }
    }

    if (target)
    {
        protocol::TMGetObjectByHash tmBH;
        tmBH.set_query(true);
        tmBH.set_type(protocol::TMGetObjectByHash::otFETCH_PACK);
        tmBH.set_ledgerhash(haveHash->begin(), 32);
        auto packet = std::make_shared<Message>(tmBH, protocol::mtGET_OBJECTS);

        target->send(packet);
        JLOG(journal_.trace()) << "Requested fetch pack for " << missing;
    }
    else
        JLOG(journal_.debug()) << "No peer for fetch pack";
}

void
LedgerMaster::fixMismatch(ReadView const& ledger)
{
    int invalidate = 0;
    std::optional<uint256> hash;

    for (std::uint32_t lSeq = ledger.header().seq - 1; lSeq > 0; --lSeq)
    {
        if (haveLedger(lSeq))
        {
            try
            {
                hash = hashOfSeq(ledger, lSeq, journal_);
            }
            catch (std::exception const& ex)
            {
                JLOG(journal_.warn())
                    << "fixMismatch encounters partial ledger. Exception: " << ex.what();
                clearLedger(lSeq);
                return;
            }

            if (hash)
            {
                // try to close the seam
                auto otherLedger = getLedgerBySeq(lSeq);

                if (otherLedger && (otherLedger->header().hash == *hash))
                {
                    // we closed the seam
                    if (invalidate != 0)
                    {
                        JLOG(journal_.warn()) << "Match at " << lSeq << ", " << invalidate
                                              << " prior ledgers invalidated";
                    }

                    return;
                }
            }

            clearLedger(lSeq);
            ++invalidate;
        }
    }

    // all prior ledgers invalidated
    if (invalidate != 0)
    {
        JLOG(journal_.warn()) << "All " << invalidate << " prior ledgers invalidated";
    }
}

void
LedgerMaster::setFullLedger(
    std::shared_ptr<Ledger const> const& ledger,
    bool isSynchronous,
    bool isCurrent)
{
    // A new ledger has been accepted as part of the trusted chain
    JLOG(journal_.debug()) << "Ledger " << ledger->header().seq
                           << " accepted :" << ledger->header().hash;
    XRPL_ASSERT(
        ledger->stateMap().getHash().isNonZero(),
        "xrpl::LedgerMaster::setFullLedger : nonzero ledger state hash");

    ledger->setValidated();
    ledger->setFull();

    if (isCurrent)
        ledgerHistory_.insert(ledger, true);

    {
        // Check the SQL database's entry for the sequence before this
        // ledger, if it's not this ledger's parent, invalidate it
        uint256 const prevHash =
            app_.getRelationalDatabase().getHashByIndex(ledger->header().seq - 1);
        if (prevHash.isNonZero() && prevHash != ledger->header().parentHash)
            clearLedger(ledger->header().seq - 1);
    }

    pendSaveValidated(app_, ledger, isSynchronous, isCurrent);

    {
        std::scoped_lock const ml(completeLock_);
        completeLedgers_.insert(ledger->header().seq);
    }

    {
        std::scoped_lock const ml(mutex_);

        if (ledger->header().seq > validLedgerSeq_)
            setValidLedger(ledger);
        if (!pubLedger_)
        {
            setPubLedger(ledger);
            app_.getOrderBookDB().setup(ledger);
        }

        if (ledger->header().seq != 0 && haveLedger(ledger->header().seq - 1))
        {
            // we think we have the previous ledger, double check
            auto prevLedger = getLedgerBySeq(ledger->header().seq - 1);

            if (!prevLedger || (prevLedger->header().hash != ledger->header().parentHash))
            {
                JLOG(journal_.warn()) << "Acquired ledger invalidates previous ledger: "
                                      << (prevLedger ? "hashMismatch" : "missingLedger");
                fixMismatch(*ledger);
            }
        }
    }
}

void
LedgerMaster::failedSave(std::uint32_t seq, uint256 const& hash)
{
    clearLedger(seq);
    app_.getInboundLedgers().acquire(hash, seq, InboundLedger::Reason::GENERIC);
}

// Check if the specified ledger can become the new last fully-validated
// ledger.
void
LedgerMaster::checkAccept(uint256 const& hash, std::uint32_t seq)
{
    std::size_t valCount = 0;

    if (seq != 0)
    {
        // Ledger is too old
        if (seq < validLedgerSeq_)
            return;

        auto validations = app_.getValidators().negativeUNLFilter(
            app_.getValidations().getTrustedForLedger(hash, seq));
        valCount = validations.size();
        if (valCount >= app_.getValidators().quorum())
        {
            std::scoped_lock const ml(mutex_);
            if (seq > lastValidLedger_.second)
                lastValidLedger_ = std::make_pair(hash, seq);
        }

        if (seq == validLedgerSeq_)
            return;

        // Ledger could match the ledger we're already building
        if (seq == buildingLedgerSeq_)
            return;
    }

    auto ledger = ledgerHistory_.getLedgerByHash(hash);

    if (!ledger)
    {
        if ((seq != 0) && (getValidLedgerIndex() == 0))
        {
            // Set peers converged early if we can
            if (valCount >= app_.getValidators().quorum())
                app_.getOverlay().checkTracking(seq);
        }

        // FIXME: We may not want to fetch a ledger with just one
        // trusted validation
        ledger = app_.getInboundLedgers().acquire(hash, seq, InboundLedger::Reason::GENERIC);
    }

    if (ledger)
        checkAccept(ledger);
}

/**
 * Determines how many validations are needed to fully validate a ledger
 *
 * @return Number of validations needed
 */
std::size_t
LedgerMaster::getNeededValidations()
{
    return standalone_ ? 0 : app_.getValidators().quorum();
}

void
LedgerMaster::checkAccept(std::shared_ptr<Ledger const> const& ledger)
{
    // Can we accept this ledger as our new last fully-validated ledger

    if (!canBeCurrent(ledger))
        return;

    // Can we advance the last fully-validated ledger? If so, can we
    // publish?
    std::scoped_lock const ml(mutex_);

    if (ledger->header().seq <= validLedgerSeq_)
        return;

    auto const minVal = getNeededValidations();
    auto validations = app_.getValidators().negativeUNLFilter(
        app_.getValidations().getTrustedForLedger(ledger->header().hash, ledger->header().seq));
    auto const tvc = validations.size();
    if (tvc < minVal)  // nothing we can do
    {
        JLOG(journal_.trace()) << "Only " << tvc << " validations for " << ledger->header().hash;
        return;
    }

    JLOG(journal_.info()) << "Advancing accepted ledger to " << ledger->header().seq
                          << " with >= " << minVal << " validations";

    ledger->setValidated();
    ledger->setFull();
    setValidLedger(ledger);
    if (!pubLedger_)
    {
        pendSaveValidated(app_, ledger, true, true);
        setPubLedger(ledger);
        app_.getOrderBookDB().setup(ledger);
    }

    std::uint32_t const base = app_.getFeeTrack().getLoadBase();
    auto fees = app_.getValidations().fees(ledger->header().hash, base);
    {
        auto fees2 = app_.getValidations().fees(ledger->header().parentHash, base);
        fees.reserve(fees.size() + fees2.size());
        std::ranges::copy(fees2, std::back_inserter(fees));
    }
    std::uint32_t fee = 0;
    if (!fees.empty())
    {
        std::ranges::sort(fees);
        if (auto stream = journal_.debug())
        {
            std::stringstream s;
            s << "Received fees from validations: (" << fees.size() << ") ";
            for (auto const fee1 : fees)
            {
                s << " " << fee1;
            }
            stream << s.str();
        }
        fee = fees[fees.size() / 2];  // median
    }
    else
    {
        fee = base;
    }

    app_.getFeeTrack().setRemoteFee(fee);

    tryAdvance();

    if (ledger->seq() % 256 == 0)
    {
        // Check if the majority of validators run a higher version xrpld
        // software. If so print a warning.
        //
        // Validators include their xrpld software version in the validation
        // messages of every (flag - 1) ledger. We wait for one ledger time
        // before checking the version information to accumulate more validation
        // messages.

        auto currentTime = app_.getTimeKeeper().now();
        bool needPrint = false;

        // The variable upgradeWarningPrevTime_ will be set when and only when
        // the warning is printed.
        if (upgradeWarningPrevTime_ == TimeKeeper::time_point())
        {
            // Have not printed the warning before, check if need to print.
            auto const vals = app_.getValidations().getTrustedForLedger(
                ledger->header().parentHash, ledger->header().seq - 1);
            std::size_t higherVersionCount = 0;
            std::size_t xrpldCount = 0;
            for (auto const& v : vals)
            {
                if (v->isFieldPresent(sfServerVersion))
                {
                    auto version = v->getFieldU64(sfServerVersion);
                    higherVersionCount += BuildInfo::isNewerVersion(version) ? 1 : 0;
                    xrpldCount += BuildInfo::isXrpldVersion(version) ? 1 : 0;
                }
            }
            // We report only if (1) we have accumulated validation messages
            // from 90% validators from the UNL, (2) 60% of validators
            // running the xrpld implementation have higher version numbers,
            // and (3) the calculation won't cause divide-by-zero.
            if (higherVersionCount > 0 && xrpldCount > 0)
            {
                static constexpr std::size_t kReportingPercent = 90;
                static constexpr std::size_t kCutoffPercent = 60;
                auto const unlSize{app_.getValidators().getQuorumKeys().second.size()};
                needPrint = unlSize > 0 &&
                    calculatePercent(vals.size(), unlSize) >= kReportingPercent &&
                    calculatePercent(higherVersionCount, xrpldCount) >= kCutoffPercent;
            }
        }
        // To throttle the warning messages, instead of printing a warning
        // every flag ledger, we print every week.
        else if (currentTime - upgradeWarningPrevTime_ >= weeks{1})
        {
            // Printed the warning before, and assuming most validators
            // do not downgrade, we keep printing the warning
            // until the local server is restarted.
            needPrint = true;
        }

        if (needPrint)
        {
            upgradeWarningPrevTime_ = currentTime;
            auto const upgradeMsg =
                "Check for upgrade: "
                "A majority of trusted validators are "
                "running a newer version.";
            std::cerr << upgradeMsg << std::endl;
            JLOG(journal_.error()) << upgradeMsg;
        }
    }
}

/** Report that the consensus process built a particular ledger */
void
LedgerMaster::consensusBuilt(
    std::shared_ptr<Ledger const> const& ledger,
    uint256 const& consensusHash,
    json::Value consensus)
{
    // Because we just built a ledger, we are no longer building one
    setBuildingLedger(0);

    // No need to process validations in standalone mode
    if (standalone_)
        return;

    ledgerHistory_.builtLedger(ledger, consensusHash, std::move(consensus));

    if (ledger->header().seq <= validLedgerSeq_)
    {
        auto stream = app_.getJournal("LedgerConsensus").info();
        JLOG(stream) << "Consensus built old ledger: " << ledger->header().seq
                     << " <= " << validLedgerSeq_;
        return;
    }

    // See if this ledger can be the new fully-validated ledger
    checkAccept(ledger);

    if (ledger->header().seq <= validLedgerSeq_)
    {
        auto stream = app_.getJournal("LedgerConsensus").debug();
        JLOG(stream) << "Consensus ledger fully validated";
        return;
    }

    // This ledger cannot be the new fully-validated ledger, but
    // maybe we saved up validations for some other ledger that can be

    auto validations =
        app_.getValidators().negativeUNLFilter(app_.getValidations().currentTrusted());

    // Track validation counts with sequence numbers
    class ValSeq
    {
    public:
        ValSeq() = default;

        void
        mergeValidation(LedgerIndex seq)
        {
            valCount++;

            // If we didn't already know the sequence, now we do
            if (ledgerSeq == 0)
                ledgerSeq = seq;
        }

        std::size_t valCount{0};
        LedgerIndex ledgerSeq{0};
    };

    // Count the number of current, trusted validations
    hash_map<uint256, ValSeq> count;
    for (auto const& v : validations)
    {
        ValSeq& vs = count[v->getLedgerHash()];
        vs.mergeValidation(v->getFieldU32(sfLedgerSequence));
    }

    auto const neededValidations = getNeededValidations();
    auto maxSeq = validLedgerSeq_.load();
    auto maxLedger = ledger->header().hash;

    // Of the ledgers with sufficient validations,
    // find the one with the highest sequence
    for (auto& v : count)
    {
        if (v.second.valCount > neededValidations)
        {
            // If we still don't know the sequence, get it
            if (v.second.ledgerSeq == 0)
            {
                if (auto l = getLedgerByHash(v.first))
                    v.second.ledgerSeq = l->header().seq;
            }

            if (v.second.ledgerSeq > maxSeq)
            {
                maxSeq = v.second.ledgerSeq;
                maxLedger = v.first;
            }
        }
    }

    if (maxSeq > validLedgerSeq_)
    {
        auto stream = app_.getJournal("LedgerConsensus").debug();
        JLOG(stream) << "Consensus triggered check of ledger";
        checkAccept(maxLedger, maxSeq);
    }
}

std::optional<LedgerHash>
LedgerMaster::getLedgerHashForHistory(LedgerIndex index, InboundLedger::Reason reason)
{
    // Try to get the hash of a ledger we need to fetch for history
    std::optional<LedgerHash> ret;
    auto const& l{histLedger_};

    if (l && l->header().seq >= index)
    {
        ret = hashOfSeq(*l, index, journal_);
        if (!ret)
            ret = walkHashBySeq(index, l, reason);
    }

    if (!ret)
        ret = walkHashBySeq(index, reason);

    return ret;
}

std::vector<std::shared_ptr<Ledger const>>
LedgerMaster::findNewLedgersToPublish(std::unique_lock<std::recursive_mutex>& sl)
{
    std::vector<std::shared_ptr<Ledger const>> ret;

    JLOG(journal_.trace()) << "findNewLedgersToPublish<";

    // No valid ledger, nothing to do
    if (validLedger_.empty())
    {
        JLOG(journal_.trace()) << "No valid journal, nothing to publish.";
        return {};
    }

    if (!pubLedger_)
    {
        JLOG(journal_.info()) << "First published ledger will be " << validLedgerSeq_;
        return {validLedger_.get()};
    }

    if (validLedgerSeq_ > (pubLedgerSeq_ + kMaxLedgerGap))
    {
        JLOG(journal_.warn()) << "Gap in validated ledger stream " << pubLedgerSeq_ << " - "
                              << validLedgerSeq_ - 1;

        auto valLedger = validLedger_.get();
        ret.push_back(valLedger);
        setPubLedger(valLedger);
        app_.getOrderBookDB().setup(valLedger);

        return {valLedger};
    }

    if (validLedgerSeq_ <= pubLedgerSeq_)
    {
        JLOG(journal_.trace()) << "No valid journal, nothing to publish.";
        return {};
    }

    int acqCount = 0;

    auto pubSeq = pubLedgerSeq_ + 1;  // Next sequence to publish
    auto valLedger = validLedger_.get();
    std::uint32_t const valSeq = valLedger->header().seq;

    ScopeUnlock const sul{sl};
    try
    {
        for (std::uint32_t seq = pubSeq; seq <= valSeq; ++seq)
        {
            JLOG(journal_.trace()) << "Trying to fetch/publish valid ledger " << seq;

            std::shared_ptr<Ledger const> ledger;
            // This can throw
            auto hash = hashOfSeq(*valLedger, seq, journal_);
            // VFALCO TODO Restructure this code so that zero is not
            // used.
            if (!hash)
                hash = beast::kZero;  // kludge
            if (seq == valSeq)
            {
                // We need to publish the ledger we just fully validated
                ledger = valLedger;
            }
            else if (hash->isZero())
            {
                // LCOV_EXCL_START
                JLOG(journal_.fatal()) << "Ledger: " << valSeq << " does not have hash for " << seq;
                UNREACHABLE(
                    "xrpl::LedgerMaster::findNewLedgersToPublish : ledger "
                    "not found");
                // LCOV_EXCL_STOP
            }
            else
            {
                ledger = ledgerHistory_.getLedgerByHash(*hash);
            }

            if (!app_.config().ledgerReplay)
            {
                // Can we try to acquire the ledger we need?
                if (!ledger && (++acqCount < ledgerFetchSize_))
                {
                    ledger = app_.getInboundLedgers().acquire(
                        *hash, seq, InboundLedger::Reason::GENERIC);
                }
            }

            // Did we acquire the next ledger we need to publish?
            if (ledger && (ledger->header().seq == pubSeq))
            {
                ledger->setValidated();
                ret.push_back(ledger);
                ++pubSeq;
            }
        }

        JLOG(journal_.trace()) << "ready to publish " << ret.size() << " ledgers.";
    }
    catch (std::exception const& ex)
    {
        JLOG(journal_.error()) << "Exception while trying to find ledgers to publish: "
                               << ex.what();
    }

    if (app_.config().ledgerReplay)
    {
        /* Narrow down the gap of ledgers, and try to replay them.
         * When replaying a ledger gap, if the local node has
         * the start ledger, it saves an expensive InboundLedger
         * acquire. If the local node has the finish ledger, it
         * saves a skip list acquire.
         */
        auto const& startLedger = ret.empty() ? pubLedger_ : ret.back();
        auto finishLedger = valLedger;
        while (startLedger->seq() + 1 < finishLedger->seq())
        {
            if (auto const parent =
                    ledgerHistory_.getLedgerByHash(finishLedger->header().parentHash);
                parent)
            {
                finishLedger = parent;
            }
            else
            {
                auto numberLedgers = finishLedger->seq() - startLedger->seq() + 1;
                JLOG(journal_.debug())
                    << "Publish LedgerReplays " << numberLedgers
                    << " ledgers, from seq=" << startLedger->header().seq << ", "
                    << startLedger->header().hash << " to seq=" << finishLedger->header().seq
                    << ", " << finishLedger->header().hash;
                app_.getLedgerReplayer().replay(
                    InboundLedger::Reason::GENERIC, finishLedger->header().hash, numberLedgers);
                break;
            }
        }
    }

    return ret;
}

void
LedgerMaster::tryAdvance()
{
    std::scoped_lock const ml(mutex_);

    // Can't advance without at least one fully-valid ledger
    advanceWork_ = true;
    if (!advanceThread_ && !validLedger_.empty())
    {
        advanceThread_ = true;
        app_.getJobQueue().addJob(JtAdvance, "AdvanceLedger", [this]() {
            std::unique_lock sl(mutex_);

            XRPL_ASSERT(
                !validLedger_.empty() && advanceThread_,
                "xrpl::LedgerMaster::tryAdvance : has valid ledger");

            JLOG(journal_.trace()) << "advanceThread<";

            try
            {
                doAdvance(sl);
            }
            catch (std::exception const& ex)
            {
                JLOG(journal_.fatal()) << "doAdvance throws: " << ex.what();
            }

            advanceThread_ = false;
            JLOG(journal_.trace()) << "advanceThread>";
        });
    }
}

void
LedgerMaster::updatePaths()
{
    {
        std::scoped_lock const ml(mutex_);
        if (app_.getOPs().isNeedNetworkLedger())
        {
            --pathFindThread_;
            pathLedger_.reset();
            JLOG(journal_.debug()) << "Need network ledger for updating paths";
            return;
        }
    }

    while (!app_.getJobQueue().isStopping())
    {
        JLOG(journal_.debug()) << "updatePaths running";
        std::shared_ptr<ReadView const> lastLedger;
        {
            std::scoped_lock const ml(mutex_);

            if (!validLedger_.empty() &&
                (!pathLedger_ || (pathLedger_->header().seq != validLedgerSeq_)))
            {  // We have a new valid ledger since the last full pathfinding
                pathLedger_ = validLedger_.get();
                lastLedger = pathLedger_;
            }
            else if (pathFindNewRequest_)
            {  // We have a new request but no new ledger
                lastLedger = app_.getOpenLedger().current();
            }
            else
            {  // Nothing to do
                --pathFindThread_;
                pathLedger_.reset();
                JLOG(journal_.debug()) << "Nothing to do for updating paths";
                return;
            }
        }

        if (!standalone_)
        {  // don't pathfind with a ledger that's more than 60 seconds old
            using namespace std::chrono;
            auto age = time_point_cast<seconds>(app_.getTimeKeeper().closeTime()) -
                lastLedger->header().closeTime;
            if (age > 1min)
            {
                JLOG(journal_.debug()) << "Published ledger too old for updating paths";
                std::scoped_lock const ml(mutex_);
                --pathFindThread_;
                pathLedger_.reset();
                return;
            }
        }

        try
        {
            auto& pathRequests = app_.getPathRequestManager();
            {
                std::scoped_lock const ml(mutex_);
                if (!pathRequests.requestsPending())
                {
                    --pathFindThread_;
                    pathLedger_.reset();
                    JLOG(journal_.debug()) << "No path requests found. Nothing to do for updating "
                                              "paths. "
                                           << pathFindThread_ << " jobs remaining";
                    return;
                }
            }
            JLOG(journal_.debug()) << "Updating paths";
            pathRequests.updateAll(lastLedger);

            std::scoped_lock const ml(mutex_);
            if (!pathRequests.requestsPending())
            {
                JLOG(journal_.debug()) << "No path requests left. No need for further updating "
                                          "paths";
                --pathFindThread_;
                pathLedger_.reset();
                return;
            }
        }
        catch (SHAMapMissingNode const& mn)
        {
            JLOG(journal_.info()) << "During pathfinding: " << mn.what();
            if (lastLedger->open())
            {
                // our parent is the problem
                app_.getInboundLedgers().acquire(
                    lastLedger->header().parentHash,
                    lastLedger->header().seq - 1,
                    InboundLedger::Reason::GENERIC);
            }
            else
            {
                // this ledger is the problem
                app_.getInboundLedgers().acquire(
                    lastLedger->header().hash,
                    lastLedger->header().seq,
                    InboundLedger::Reason::GENERIC);
            }
        }
    }
}

bool
LedgerMaster::newPathRequest()
{
    std::unique_lock ml(mutex_);
    pathFindNewRequest_ = newPFWork("PthFindNewReq", ml);
    return pathFindNewRequest_;
}

bool
LedgerMaster::isNewPathRequest()
{
    std::scoped_lock const ml(mutex_);
    bool const ret = pathFindNewRequest_;
    pathFindNewRequest_ = false;
    return ret;
}

// If the order book is radically updated, we need to reprocess all
// pathfinding requests.
bool
LedgerMaster::newOrderBookDB()
{
    std::unique_lock ml(mutex_);
    pathLedger_.reset();

    return newPFWork("PthFindOBDB", ml);
}

/** A thread needs to be dispatched to handle pathfinding work of some kind.
 */
bool
LedgerMaster::newPFWork(char const* name, std::unique_lock<std::recursive_mutex>&)
{
    if (!app_.isStopping() && pathFindThread_ < 2 && app_.getPathRequestManager().requestsPending())
    {
        JLOG(journal_.debug()) << "newPFWork: Creating job. path find threads: " << pathFindThread_;
        if (app_.getJobQueue().addJob(JtUpdatePf, name, [this]() { updatePaths(); }))
        {
            ++pathFindThread_;
        }
    }
    // If we're stopping don't give callers the expectation that their
    // request will be fulfilled, even if it may be serviced.
    return pathFindThread_ > 0 && !app_.isStopping();
}

std::recursive_mutex&
LedgerMaster::peekMutex()
{
    return mutex_;
}

// The current ledger is the ledger we believe new transactions should go in
std::shared_ptr<ReadView const>
LedgerMaster::getCurrentLedger()
{
    return app_.getOpenLedger().current();
}

std::shared_ptr<Ledger const>
LedgerMaster::getValidatedLedger()
{
    return validLedger_.get();
}

Rules
LedgerMaster::getValidatedRules()
{
    // Once we have a guarantee that there's always a last validated
    // ledger then we can dispense with the if.

    // Return the Rules from the last validated ledger.
    if (auto const ledger = getValidatedLedger())
        return ledger->rules();

    return Rules(app_.config().features);
}

// This is the last ledger we published to clients and can lag the validated
// ledger.
std::shared_ptr<ReadView const>
LedgerMaster::getPublishedLedger()
{
    std::scoped_lock const lock(mutex_);
    return pubLedger_;
}

std::string
LedgerMaster::getCompleteLedgers()
{
    std::scoped_lock const sl(completeLock_);
    return to_string(completeLedgers_);
}

std::optional<NetClock::time_point>
LedgerMaster::getCloseTimeBySeq(LedgerIndex ledgerIndex)
{
    uint256 const hash = getHashBySeq(ledgerIndex);
    return hash.isNonZero() ? getCloseTimeByHash(hash, ledgerIndex) : std::nullopt;
}

std::optional<NetClock::time_point>
LedgerMaster::getCloseTimeByHash(LedgerHash const& ledgerHash, std::uint32_t index)
{
    auto nodeObject = app_.getNodeStore().fetchNodeObject(ledgerHash, index);
    if (nodeObject && (nodeObject->getData().size() >= 120))
    {
        SerialIter it(nodeObject->getData().data(), nodeObject->getData().size());
        if (safeCast<HashPrefix>(it.get32()) == HashPrefix::LedgerMaster)
        {
            it.skip(
                4 + 8 + 32 +   // seq drops parentHash
                32 + 32 + 4);  // txHash acctHash parentClose
            return NetClock::time_point{NetClock::duration{it.get32()}};
        }
    }

    return std::nullopt;
}

uint256
LedgerMaster::getHashBySeq(std::uint32_t index)
{
    uint256 hash = ledgerHistory_.getLedgerHash(index);

    if (hash.isNonZero())
        return hash;

    return app_.getRelationalDatabase().getHashByIndex(index);
}

std::optional<LedgerHash>
LedgerMaster::walkHashBySeq(std::uint32_t index, InboundLedger::Reason reason)
{
    std::optional<LedgerHash> ledgerHash;

    if (auto referenceLedger = validLedger_.get())
        ledgerHash = walkHashBySeq(index, referenceLedger, reason);

    return ledgerHash;
}

std::optional<LedgerHash>
LedgerMaster::walkHashBySeq(
    std::uint32_t index,
    std::shared_ptr<ReadView const> const& referenceLedger,
    InboundLedger::Reason reason)
{
    if (!referenceLedger || (referenceLedger->header().seq < index))
    {
        // Nothing we can do. No validated ledger.
        return std::nullopt;
    }

    // See if the hash for the ledger we need is in the reference ledger
    auto ledgerHash = hashOfSeq(*referenceLedger, index, journal_);
    if (ledgerHash)
        return ledgerHash;

    // The hash is not in the reference ledger. Get another ledger which can
    // be located easily and should contain the hash.
    LedgerIndex const refIndex = getCandidateLedger(index);
    auto const refHash = hashOfSeq(*referenceLedger, refIndex, journal_);
    XRPL_ASSERT(refHash, "xrpl::LedgerMaster::walkHashBySeq : found ledger");
    if (refHash)
    {
        // Try the hash and sequence of a better reference ledger just found
        auto ledger = ledgerHistory_.getLedgerByHash(*refHash);

        if (ledger)
        {
            try
            {
                ledgerHash = hashOfSeq(*ledger, index, journal_);
            }
            catch (SHAMapMissingNode const&)
            {
                ledger.reset();
            }
        }

        // Try to acquire the complete ledger
        if (!ledger)
        {
            if (auto const l = app_.getInboundLedgers().acquire(*refHash, refIndex, reason))
            {
                ledgerHash = hashOfSeq(*l, index, journal_);
                XRPL_ASSERT(
                    ledgerHash,
                    "xrpl::LedgerMaster::walkHashBySeq : has complete "
                    "ledger");
            }
        }
    }
    return ledgerHash;
}

std::shared_ptr<Ledger const>
LedgerMaster::getLedgerBySeq(std::uint32_t index)
{
    if (index <= validLedgerSeq_)
    {
        // Always prefer a validated ledger
        if (auto valid = validLedger_.get())
        {
            if (valid->header().seq == index)
                return valid;

            try
            {
                auto const hash = hashOfSeq(*valid, index, journal_);

                if (hash)
                    return ledgerHistory_.getLedgerByHash(*hash);
            }
            catch (std::exception const&)  // NOLINT(bugprone-empty-catch)
            {
                // Missing nodes are already handled
            }
        }
    }

    if (auto ret = ledgerHistory_.getLedgerBySeq(index))
        return ret;

    auto ret = closedLedger_.get();
    if (ret && (ret->header().seq == index))
        return ret;

    clearLedger(index);
    return {};
}

std::shared_ptr<Ledger const>
LedgerMaster::getLedgerByHash(uint256 const& hash)
{
    if (auto ret = ledgerHistory_.getLedgerByHash(hash))
        return ret;

    auto ret = closedLedger_.get();
    if (ret && (ret->header().hash == hash))
        return ret;

    return {};
}

void
LedgerMaster::setLedgerRangePresent(std::uint32_t minV, std::uint32_t maxV)
{
    std::scoped_lock const sl(completeLock_);
    completeLedgers_.insert(range(minV, maxV));
}

void
LedgerMaster::sweep()
{
    ledgerHistory_.sweep();
    fetchPacks_.sweep();
}

float
LedgerMaster::getCacheHitRate()
{
    return ledgerHistory_.getCacheHitRate();
}

void
LedgerMaster::clearPriorLedgers(LedgerIndex seq)
{
    std::scoped_lock const sl(completeLock_);
    if (seq > 0)
        completeLedgers_.erase(range(0u, seq - 1));
}

void
LedgerMaster::clearLedgerCachePrior(LedgerIndex seq)
{
    ledgerHistory_.clearLedgerCachePrior(seq);
}

void
LedgerMaster::takeReplay(std::unique_ptr<LedgerReplay> replay)
{
    replayData_ = std::move(replay);
}

std::unique_ptr<LedgerReplay>
LedgerMaster::releaseReplay()
{
    return std::move(replayData_);
}

void
LedgerMaster::fetchForHistory(
    std::uint32_t missing,
    bool& progress,
    InboundLedger::Reason reason,
    std::unique_lock<std::recursive_mutex>& sl)
{
    ScopeUnlock const sul{sl};
    if (auto hash = getLedgerHashForHistory(missing, reason))
    {
        XRPL_ASSERT(hash->isNonZero(), "xrpl::LedgerMaster::fetchForHistory : found ledger");
        auto ledger = getLedgerByHash(*hash);
        if (!ledger)
        {
            if (!app_.getInboundLedgers().isFailure(*hash))
            {
                ledger = app_.getInboundLedgers().acquire(*hash, missing, reason);
                if (!ledger && missing != fetchSeq_ &&
                    missing > app_.getNodeStore().earliestLedgerSeq())
                {
                    JLOG(journal_.trace()) << "fetchForHistory want fetch pack " << missing;
                    fetchSeq_ = missing;
                    getFetchPack(missing, reason);
                }
                else
                    JLOG(journal_.trace()) << "fetchForHistory no fetch pack for " << missing;
            }
            else
                JLOG(journal_.debug()) << "fetchForHistory found failed acquire";
        }
        if (ledger)
        {
            auto seq = ledger->header().seq;
            XRPL_ASSERT(seq == missing, "xrpl::LedgerMaster::fetchForHistory : sequence match");
            JLOG(journal_.trace()) << "fetchForHistory acquired " << seq;
            setFullLedger(ledger, false, false);
            int fillInProgress = 0;
            {
                std::scoped_lock const lock(mutex_);
                histLedger_ = ledger;
                fillInProgress = fillInProgress_;
            }
            if (fillInProgress == 0 &&
                app_.getRelationalDatabase().getHashByIndex(seq - 1) == ledger->header().parentHash)
            {
                {
                    // Previous ledger is in DB
                    std::scoped_lock const lock(mutex_);
                    fillInProgress_ = seq;
                }
                app_.getJobQueue().addJob(
                    JtAdvance, "TryFill", [this, ledger]() { tryFill(ledger); });
            }
            progress = true;
        }
        else
        {
            std::uint32_t fetchSz = 0;
            // Do not fetch ledger sequences lower
            // than the earliest ledger sequence
            fetchSz = app_.getNodeStore().earliestLedgerSeq();
            fetchSz = missing >= fetchSz ? std::min(ledgerFetchSize_, (missing - fetchSz) + 1) : 0;
            try
            {
                for (std::uint32_t i = 0; i < fetchSz; ++i)
                {
                    std::uint32_t const seq = missing - i;
                    if (auto h = getLedgerHashForHistory(seq, reason))
                    {
                        XRPL_ASSERT(
                            h->isNonZero(),
                            "xrpl::LedgerMaster::fetchForHistory : "
                            "prefetched ledger");
                        app_.getInboundLedgers().acquire(*h, seq, reason);
                    }
                }
            }
            catch (std::exception const& ex)
            {
                JLOG(journal_.warn()) << "Threw while prefetching: " << ex.what();
            }
        }
    }
    else
    {
        JLOG(journal_.fatal()) << "Can't find ledger following prevMissing " << missing;
        JLOG(journal_.fatal()) << "Pub:" << pubLedgerSeq_ << " Val:" << validLedgerSeq_;
        JLOG(journal_.fatal()) << "Ledgers: " << app_.getLedgerMaster().getCompleteLedgers();
        JLOG(journal_.fatal()) << "Acquire reason: "
                               << (reason == InboundLedger::Reason::HISTORY ? "HISTORY"
                                                                            : "NOT HISTORY");
        clearLedger(missing + 1);
        progress = true;
    }
}

// Try to publish ledgers, acquire missing ledgers
void
LedgerMaster::doAdvance(std::unique_lock<std::recursive_mutex>& sl)
{
    do
    {
        advanceWork_ = false;  // If there's work to do, we'll make progress
        bool progress = false;

        auto const pubLedgers = findNewLedgersToPublish(sl);
        if (pubLedgers.empty())
        {
            if (!standalone_ && !app_.getFeeTrack().isLoadedLocal() &&
                (app_.getJobQueue().getJobCount(JtPuboldledger) < 10) &&
                (validLedgerSeq_ == pubLedgerSeq_) &&
                (getValidatedLedgerAge() < kMaxLedgerAgeAcquire) &&
                (app_.getNodeStore().getWriteLoad() < kMaxWriteLoadAcquire))
            {
                // We are in sync, so can acquire
                InboundLedger::Reason const reason = InboundLedger::Reason::HISTORY;
                std::optional<std::uint32_t> missing;
                {
                    std::scoped_lock const sll(completeLock_);
                    missing = prevMissing(
                        completeLedgers_,
                        pubLedger_->header().seq,
                        app_.getNodeStore().earliestLedgerSeq());
                }
                if (missing)
                {
                    JLOG(journal_.trace()) << "tryAdvance discovered missing " << *missing;
                    if ((fillInProgress_ == 0 || *missing > fillInProgress_) &&
                        shouldAcquire(
                            validLedgerSeq_,
                            ledgerHistorySize_,
                            app_.getSHAMapStore().minimumOnline(),
                            *missing,
                            journal_))
                    {
                        JLOG(journal_.trace()) << "advanceThread should acquire";
                    }
                    else
                    {
                        missing = std::nullopt;
                    }
                }
                if (missing)
                {
                    fetchForHistory(*missing, progress, reason, sl);
                    if (validLedgerSeq_ != pubLedgerSeq_)
                    {
                        JLOG(journal_.debug()) << "tryAdvance found last valid changed";
                        progress = true;
                    }
                }
            }
            else
            {
                histLedger_.reset();
                JLOG(journal_.trace()) << "tryAdvance not fetching history";
            }
        }
        else
        {
            JLOG(journal_.trace())
                << "tryAdvance found " << pubLedgers.size() << " ledgers to publish";
            for (auto const& ledger : pubLedgers)
            {
                {
                    ScopeUnlock const sul{sl};
                    JLOG(journal_.debug()) << "tryAdvance publishing seq " << ledger->header().seq;
                    setFullLedger(ledger, true, true);
                }

                setPubLedger(ledger);

                {
                    ScopeUnlock const sul{sl};
                    app_.getOPs().pubLedger(ledger);
                }
            }

            app_.getOPs().clearNeedNetworkLedger();
            progress = newPFWork("PthFindNewLed", sl);
        }
        if (progress)
            advanceWork_ = true;
    } while (advanceWork_);
}

void
LedgerMaster::addFetchPack(uint256 const& hash, std::shared_ptr<Blob> data)
{
    fetchPacks_.canonicalizeReplaceClient(hash, data);
}

std::optional<Blob>
LedgerMaster::getFetchPack(uint256 const& hash)
{
    Blob data;
    if (fetchPacks_.retrieve(hash, data))
    {
        fetchPacks_.del(hash, false);
        if (hash == sha512Half(makeSlice(data)))
            return data;
    }
    return std::nullopt;
}

void
LedgerMaster::gotFetchPack(bool progress, std::uint32_t seq)
{
    if (!gotFetchPackThread_.test_and_set(std::memory_order_acquire))
    {
        app_.getJobQueue().addJob(JtLedgerData, "GotFetchPack", [&]() {
            app_.getInboundLedgers().gotFetchPack();
            gotFetchPackThread_.clear(std::memory_order_release);
        });
    }
}

/** Populate a fetch pack with data from the map the recipient wants.

    A recipient may or may not have the map that they are asking for. If
    they do, we can optimize the transfer by not including parts of the
    map that they are already have.

    @param have The map that the recipient already has (if any).
    @param cnt The maximum number of nodes to return.
    @param into The protocol object into which we add information.
    @param seq The sequence number of the ledger the map is a part of.
    @param withLeaves True if leaf nodes should be included.

    @note: The withLeaves parameter is configurable even though the
           code, so far, only ever sets the parameter to true.

           The rationale is that for transaction trees, it may make
           sense to not include the leaves if the fetch pack is being
           constructed for someone attempting to get a recent ledger
           for which they already have the transactions.

           However, for historical ledgers, which is the only use we
           have for fetch packs right now, it makes sense to include
           the transactions because the caller is unlikely to have
           them.
 */
static void
populateFetchPack(
    SHAMap const& want,
    SHAMap const* have,
    std::uint32_t cnt,
    protocol::TMGetObjectByHash* into,
    std::uint32_t seq,
    bool withLeaves = true)
{
    XRPL_ASSERT(cnt, "xrpl::populateFetchPack : nonzero count input");

    Serializer s(1024);

    want.visitDifferences(have, [&s, withLeaves, &cnt, into, seq](SHAMapTreeNode const& n) -> bool {
        if (!withLeaves && n.isLeaf())
            return true;

        s.erase();
        n.serializeWithPrefix(s);

        auto const& hash = n.getHash().asUInt256();

        protocol::TMIndexedObject* obj = into->add_objects();
        obj->set_ledgerseq(seq);
        obj->set_hash(hash.data(), hash.size());
        obj->set_data(s.getDataPtr(), s.getLength());

        return --cnt != 0;
    });
}

void
LedgerMaster::makeFetchPack(
    std::weak_ptr<Peer> const& wPeer,
    std::shared_ptr<protocol::TMGetObjectByHash> const& request,
    uint256 haveLedgerHash,
    UptimeClock::time_point uptime)
{
    using namespace std::chrono_literals;
    if (UptimeClock::now() > uptime + 1s)
    {
        JLOG(journal_.info()) << "Fetch pack request got stale";
        return;
    }

    if (app_.getFeeTrack().isLoadedLocal() || (getValidatedLedgerAge() > 40s))
    {
        JLOG(journal_.info()) << "Too busy to make fetch pack";
        return;
    }

    auto peer = wPeer.lock();

    if (!peer)
        return;

    auto have = getLedgerByHash(haveLedgerHash);

    if (!have)
    {
        JLOG(journal_.info()) << "Peer requests fetch pack for ledger we don't have: " << have;
        peer->charge(Resource::kFeeRequestNoReply, "get_object ledger");
        return;
    }

    if (have->open())
    {
        JLOG(journal_.warn()) << "Peer requests fetch pack from open ledger: " << have;
        peer->charge(Resource::kFeeMalformedRequest, "get_object ledger open");
        return;
    }

    if (have->header().seq < getEarliestFetch())
    {
        JLOG(journal_.debug()) << "Peer requests fetch pack that is too early";
        peer->charge(Resource::kFeeMalformedRequest, "get_object ledger early");
        return;
    }

    auto want = getLedgerByHash(have->header().parentHash);

    if (!want)
    {
        JLOG(journal_.info()) << "Peer requests fetch pack for ledger whose predecessor we "
                              << "don't have: " << have;
        peer->charge(Resource::kFeeRequestNoReply, "get_object ledger no parent");
        return;
    }

    try
    {
        Serializer hdr(128);

        protocol::TMGetObjectByHash reply;
        reply.set_query(false);

        reply.set_ledgerhash(request->ledgerhash());
        reply.set_type(protocol::TMGetObjectByHash::otFETCH_PACK);

        // Building a fetch pack:
        //  1. Add the header for the requested ledger.
        //  2. Add the nodes for the AccountStateMap of that ledger.
        //  3. If there are transactions, add the nodes for the
        //     transactions of the ledger.
        //  4. If the FetchPack now contains at least 512 entries then stop.
        //  5. If not very much time has elapsed, then loop back and repeat
        //     the same process adding the previous ledger to the FetchPack.
        do
        {
            std::uint32_t const lSeq = want->header().seq;

            {
                // Serialize the ledger header:
                hdr.erase();

                hdr.add32(HashPrefix::LedgerMaster);
                addRaw(want->header(), hdr);

                // Add the data
                protocol::TMIndexedObject* obj = reply.add_objects();
                obj->set_hash(want->header().hash.data(), want->header().hash.size());
                obj->set_data(hdr.getDataPtr(), hdr.getLength());
                obj->set_ledgerseq(lSeq);
            }

            populateFetchPack(want->stateMap(), &have->stateMap(), 16384, &reply, lSeq);

            // We use nullptr here because transaction maps are per ledger
            // and so the requestor is unlikely to already have it.
            if (want->header().txHash.isNonZero())
                populateFetchPack(want->txMap(), nullptr, 512, &reply, lSeq);

            if (reply.objects().size() >= 512)
                break;

            have = std::move(want);
            want = getLedgerByHash(have->header().parentHash);
        } while (want && UptimeClock::now() <= uptime + 1s);

        auto msg = std::make_shared<Message>(reply, protocol::mtGET_OBJECTS);

        JLOG(journal_.info()) << "Built fetch pack with " << reply.objects().size() << " nodes ("
                              << msg->getBufferSize() << " bytes)";

        peer->send(msg);
    }
    catch (std::exception const& ex)
    {
        JLOG(journal_.warn()) << "Exception building fetch pack. Exception: " << ex.what();
    }
}

std::size_t
LedgerMaster::getFetchPackCacheSize() const
{
    return fetchPacks_.getCacheSize();
}

// Returns the minimum ledger sequence in SQL database, if any.
std::optional<LedgerIndex>
LedgerMaster::minSqlSeq()
{
    return app_.getRelationalDatabase().getMinLedgerSeq();
}

std::optional<uint256>
LedgerMaster::txnIdFromIndex(uint32_t ledgerSeq, uint32_t txnIndex)
{
    uint32_t first = 0, last = 0;

    if (!getValidatedRange(first, last) || last < ledgerSeq)
        return {};

    auto const lgr = getLedgerBySeq(ledgerSeq);
    if (!lgr || lgr->txs.empty())
        return {};

    for (auto it = lgr->txs.begin(); it != lgr->txs.end(); ++it)
    {
        if (it->first && it->second && it->second->isFieldPresent(sfTransactionIndex) &&
            it->second->getFieldU32(sfTransactionIndex) == txnIndex)
            return it->first->getTransactionID();
    }

    return {};
}

}  // namespace xrpl
