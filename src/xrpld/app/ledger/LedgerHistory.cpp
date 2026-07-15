#include <xrpld/app/ledger/LedgerHistory.h>

#include <xrpld/app/ledger/LedgerPersistence.h>
#include <xrpld/app/ledger/LedgerToJson.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/json_writer.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/RippleLedgerHash.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/shamap/SHAMapItem.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace xrpl {

LedgerHistory::LedgerHistory(beast::insight::Collector::ptr const& collector, Application& app)
    : app_(app)
    , collector_(collector)
    , mismatchCounter_(collector->makeCounter("ledger.history", "mismatch"))
    , consensusValidated_(
          "ConsensusValidated",
          64,
          std::chrono::minutes{5},
          stopwatch(),
          app_.getJournal("TaggedCache"))
    , j_(app.getJournal("LedgerHistory"))
{
    auto lock = ledgerMaps_.lock();
    lock->byHash = std::make_unique<LedgerMaps::LedgersByHash>(
        "LedgerCache",
        app_.config().getValueFor(SizedItem::LedgerSize),
        std::chrono::seconds{app_.config().getValueFor(SizedItem::LedgerAge)},
        stopwatch(),
        app_.getJournal("TaggedCache"));
}

bool
LedgerHistory::insert(std::shared_ptr<Ledger const> const& ledger, bool validated)
{
    if (!ledger->isImmutable())
        logicError("mutable Ledger in insert");

    XRPL_ASSERT(
        ledger->stateMap().getHash().isNonZero(), "xrpl::LedgerHistory::insert : nonzero hash");

    auto lock = ledgerMaps_.lock();
    bool const alreadyHad = lock->byHash->canonicalizeReplaceCache(ledger->header().hash, ledger);
    if (validated)
        lock->byIndex[ledger->header().seq] = ledger->header().hash;

    return alreadyHad;
}

LedgerHash
LedgerHistory::getLedgerHash(LedgerIndex index)
{
    auto lock = ledgerMaps_.lock();
    if (auto it = lock->byIndex.find(index); it != lock->byIndex.end())
        return it->second;
    return {};
}

std::shared_ptr<Ledger const>
LedgerHistory::getLedgerBySeq(LedgerIndex index)
{
    uint256 hash;
    {
        auto lock = ledgerMaps_.lock();
        if (auto it = lock->byIndex.find(index); it != lock->byIndex.end())
            hash = it->second;
    }

    if (!hash.isZero())
        return getLedgerByHash(hash);

    Rules const rules{app_.config().features};
    Fees const fees = app_.config().fees.toFees();
    std::shared_ptr<Ledger const> ret = loadByIndex(index, rules, fees, app_);

    if (!ret)
        return ret;

    XRPL_ASSERT(
        ret->header().seq == index, "xrpl::LedgerHistory::getLedgerBySeq : result sequence match");

    {
        // Add this ledger to the local tracking by index
        auto lock = ledgerMaps_.lock();
        XRPL_ASSERT(
            ret->isImmutable(), "xrpl::LedgerHistory::getLedgerBySeq : immutable result ledger");
        lock->byHash->canonicalizeReplaceClient(ret->header().hash, ret);
        lock->byIndex[ret->header().seq] = ret->header().hash;
        return (ret->header().seq == index) ? ret : nullptr;
    }
}

std::shared_ptr<Ledger const>
LedgerHistory::getLedgerByHash(LedgerHash const& hash)
{
    std::shared_ptr<Ledger const> ret;
    {
        auto lock = ledgerMaps_.lock();
        ret = lock->byHash->fetch(hash);
    }

    if (ret)
    {
        XRPL_ASSERT(
            ret->isImmutable(),
            "xrpl::LedgerHistory::getLedgerByHash : immutable fetched "
            "ledger");
        XRPL_ASSERT(
            ret->header().hash == hash,
            "xrpl::LedgerHistory::getLedgerByHash : fetched ledger hash "
            "match");
        return ret;
    }

    Rules const rules{app_.config().features};
    Fees const fees = app_.config().fees.toFees();
    ret = loadByHash(hash, rules, fees, app_);

    if (!ret)
        return ret;

    XRPL_ASSERT(
        ret->isImmutable(), "xrpl::LedgerHistory::getLedgerByHash : immutable loaded ledger");
    XRPL_ASSERT(
        ret->header().hash == hash,
        "xrpl::LedgerHistory::getLedgerByHash : loaded ledger hash match");
    {
        auto lock = ledgerMaps_.lock();
        lock->byHash->canonicalizeReplaceClient(ret->header().hash, ret);
    }
    XRPL_ASSERT(
        ret->header().hash == hash, "xrpl::LedgerHistory::getLedgerByHash : result hash match");

    return ret;
}

namespace {

// Captures the sibling's contribution when builtLedger or validatedLedger
// detects a hash mismatch, so handleMismatch can run outside the cache lock.
struct MismatchInputs
{
    LedgerHash otherHash;
    std::optional<uint256> otherConsensusHash;
    json::Value consensus;
};

}  // namespace

static void
logOne(ReadView const& ledger, uint256 const& tx, char const* msg, beast::Journal& j)
{
    auto metaData = ledger.txRead(tx).second;

    if (metaData != nullptr)
    {
        JLOG(j.debug()) << "MISMATCH on TX " << tx << ": " << msg
                        << " is missing this transaction:\n"
                        << metaData->getJson(JsonOptions::Values::None);
    }
    else
    {
        JLOG(j.debug()) << "MISMATCH on TX " << tx << ": " << msg
                        << " is missing this transaction.";
    }
}

static void
logMetadataDifference(
    ReadView const& builtLedger,
    ReadView const& validLedger,
    uint256 const& tx,
    beast::Journal j)
{
    auto getMeta = [](ReadView const& ledger, uint256 const& txID) {
        std::optional<TxMeta> ret;
        if (auto meta = ledger.txRead(txID).second)
            ret.emplace(txID, ledger.seq(), *meta);
        return ret;
    };

    auto validMetaData = getMeta(validLedger, tx);
    auto builtMetaData = getMeta(builtLedger, tx);

    XRPL_ASSERT(
        validMetaData || builtMetaData, "xrpl::logMetadataDifference : some metadata present");

    if (validMetaData && builtMetaData)
    {
        auto const& validNodes = validMetaData->getNodes();
        auto const& builtNodes = builtMetaData->getNodes();

        bool const resultDiff = validMetaData->getResultTER() != builtMetaData->getResultTER();

        bool const indexDiff = validMetaData->getIndex() != builtMetaData->getIndex();

        bool const nodesDiff = validNodes != builtNodes;

        if (!resultDiff && !indexDiff && !nodesDiff)
        {
            JLOG(j.error()) << "MISMATCH on TX " << tx << ": No apparent mismatches detected!";
            return;
        }

        if (!nodesDiff)
        {
            if (resultDiff && indexDiff)
            {
                JLOG(j.debug()) << "MISMATCH on TX " << tx << ": Different result and index!";
                JLOG(j.debug()) << " Built:"
                                << " Result: " << builtMetaData->getResult()
                                << " Index: " << builtMetaData->getIndex();
                JLOG(j.debug()) << " Valid:"
                                << " Result: " << validMetaData->getResult()
                                << " Index: " << validMetaData->getIndex();
            }
            else if (resultDiff)
            {
                JLOG(j.debug()) << "MISMATCH on TX " << tx << ": Different result!";
                JLOG(j.debug()) << " Built:"
                                << " Result: " << builtMetaData->getResult();
                JLOG(j.debug()) << " Valid:"
                                << " Result: " << validMetaData->getResult();
            }
            else if (indexDiff)
            {
                JLOG(j.debug()) << "MISMATCH on TX " << tx << ": Different index!";
                JLOG(j.debug()) << " Built:"
                                << " Index: " << builtMetaData->getIndex();
                JLOG(j.debug()) << " Valid:"
                                << " Index: " << validMetaData->getIndex();
            }
        }
        else
        {
            if (resultDiff && indexDiff)
            {
                JLOG(j.debug()) << "MISMATCH on TX " << tx
                                << ": Different result, index and nodes!";
                JLOG(j.debug()) << " Built:\n" << builtMetaData->getJson(JsonOptions::Values::None);
                JLOG(j.debug()) << " Valid:\n" << validMetaData->getJson(JsonOptions::Values::None);
            }
            else if (resultDiff)
            {
                JLOG(j.debug()) << "MISMATCH on TX " << tx << ": Different result and nodes!";
                JLOG(j.debug()) << " Built:"
                                << " Result: " << builtMetaData->getResult() << " Nodes:\n"
                                << builtNodes.getJson(JsonOptions::Values::None);
                JLOG(j.debug()) << " Valid:"
                                << " Result: " << validMetaData->getResult() << " Nodes:\n"
                                << validNodes.getJson(JsonOptions::Values::None);
            }
            else if (indexDiff)
            {
                JLOG(j.debug()) << "MISMATCH on TX " << tx << ": Different index and nodes!";
                JLOG(j.debug()) << " Built:"
                                << " Index: " << builtMetaData->getIndex() << " Nodes:\n"
                                << builtNodes.getJson(JsonOptions::Values::None);
                JLOG(j.debug()) << " Valid:"
                                << " Index: " << validMetaData->getIndex() << " Nodes:\n"
                                << validNodes.getJson(JsonOptions::Values::None);
            }
            else  // nodesDiff
            {
                JLOG(j.debug()) << "MISMATCH on TX " << tx << ": Different nodes!";
                JLOG(j.debug()) << " Built:"
                                << " Nodes:\n"
                                << builtNodes.getJson(JsonOptions::Values::None);
                JLOG(j.debug()) << " Valid:"
                                << " Nodes:\n"
                                << validNodes.getJson(JsonOptions::Values::None);
            }
        }

        return;
    }

    if (validMetaData)
    {
        JLOG(j.error()) << "MISMATCH on TX " << tx << ": Metadata Difference. Valid=\n"
                        << validMetaData->getJson(JsonOptions::Values::None);
    }

    if (builtMetaData)
    {
        JLOG(j.error()) << "MISMATCH on TX " << tx << ": Metadata Difference. Built=\n"
                        << builtMetaData->getJson(JsonOptions::Values::None);
    }
}

//------------------------------------------------------------------------------

// Return list of leaves sorted by key
static std::vector<SHAMapItem const*>
leaves(SHAMap const& sm)
{
    std::vector<SHAMapItem const*> v;
    for (auto const& item : sm)
        v.push_back(&item);
    std::ranges::sort(
        v, [](SHAMapItem const* lhs, SHAMapItem const* rhs) { return lhs->key() < rhs->key(); });
    return v;
}

void
LedgerHistory::handleMismatch(
    LedgerHash const& built,
    LedgerHash const& valid,
    std::optional<uint256> const& builtConsensusHash,
    std::optional<uint256> const& validatedConsensusHash,
    json::Value const& consensus)
{
    XRPL_ASSERT(built != valid, "xrpl::LedgerHistory::handleMismatch : unequal hashes");
    ++mismatchCounter_;

    auto builtLedger = getLedgerByHash(built);
    auto validLedger = getLedgerByHash(valid);

    if (!builtLedger || !validLedger)
    {
        JLOG(j_.error()) << "MISMATCH cannot be analyzed:"
                         << " builtLedger: " << to_string(built) << " -> " << builtLedger
                         << " validLedger: " << to_string(valid) << " -> " << validLedger;
        return;
    }

    XRPL_ASSERT(
        builtLedger->header().seq == validLedger->header().seq,
        "xrpl::LedgerHistory::handleMismatch : sequence match");

    if (auto stream = j_.debug())
    {
        stream << "Built: " << getJson({*builtLedger, {}});
        stream << "Valid: " << getJson({*validLedger, {}});
        stream << "Consensus: " << consensus;
    }

    // Determine the mismatch reason, distinguishing Byzantine
    // failure from transaction processing difference

    // Disagreement over prior ledger indicates sync issue
    if (builtLedger->header().parentHash != validLedger->header().parentHash)
    {
        JLOG(j_.error()) << "MISMATCH on prior ledger";
        return;
    }

    // Disagreement over close time indicates Byzantine failure
    if (builtLedger->header().closeTime != validLedger->header().closeTime)
    {
        JLOG(j_.error()) << "MISMATCH on close time";
        return;
    }

    if (builtConsensusHash && validatedConsensusHash)
    {
        if (builtConsensusHash != validatedConsensusHash)
        {
            JLOG(j_.error()) << "MISMATCH on consensus transaction set "
                             << " built: " << to_string(*builtConsensusHash)
                             << " validated: " << to_string(*validatedConsensusHash);
        }
        else
        {
            JLOG(j_.error()) << "MISMATCH with same consensus transaction set: "
                             << to_string(*builtConsensusHash);
        }
    }

    // Find differences between built and valid ledgers
    auto const builtTx = leaves(builtLedger->txMap());
    auto const validTx = leaves(validLedger->txMap());

    if (builtTx == validTx)
    {
        JLOG(j_.error()) << "MISMATCH with same " << builtTx.size() << " transactions";
    }
    else
    {
        JLOG(j_.error()) << "MISMATCH with " << builtTx.size() << " built and " << validTx.size()
                         << " valid transactions.";
    }

    JLOG(j_.error()) << "built\n" << getJson({*builtLedger, {}});
    JLOG(j_.error()) << "valid\n" << getJson({*validLedger, {}});

    // Log all differences between built and valid ledgers
    auto b = builtTx.begin();
    auto v = validTx.begin();
    while (b != builtTx.end() && v != validTx.end())
    {
        if ((*b)->key() < (*v)->key())
        {
            logOne(*builtLedger, (*b)->key(), "valid", j_);
            ++b;
        }
        else if ((*b)->key() > (*v)->key())
        {
            logOne(*validLedger, (*v)->key(), "built", j_);
            ++v;
        }
        else
        {
            if ((*b)->slice() != (*v)->slice())
            {
                // Same transaction with different metadata
                logMetadataDifference(*builtLedger, *validLedger, (*b)->key(), j_);
            }
            ++b;
            ++v;
        }
    }
    for (; b != builtTx.end(); ++b)
        logOne(*builtLedger, (*b)->key(), "valid", j_);
    for (; v != validTx.end(); ++v)
        logOne(*validLedger, (*v)->key(), "built", j_);
}

void
LedgerHistory::builtLedger(
    std::shared_ptr<Ledger const> const& ledger,
    uint256 const& consensusHash,
    json::Value consensus)
{
    LedgerIndex const index = ledger->header().seq;
    LedgerHash const hash = ledger->header().hash;
    XRPL_ASSERT(!hash.isZero(), "xrpl::LedgerHistory::builtLedger : nonzero hash");

    std::optional<MismatchInputs> mismatch;

    consensusValidated_.fetchAndModify(index, [&](CvEntry& entry) {
        if (entry.validated && !entry.built)
        {
            SOMETIMES(true, "xrpl::LedgerHistory::builtLedger : validated arrived first");
            if (entry.validated.value() != hash)
            {
                SOMETIMES(true, "xrpl::LedgerHistory::builtLedger : validated-first mismatch");
                JLOG(j_.error()) << "MISMATCH: seq=" << index
                                 << " validated:" << entry.validated.value() << " then:" << hash;
                mismatch = MismatchInputs{
                    .otherHash = entry.validated.value(),
                    .otherConsensusHash = entry.validatedConsensusHash,
                    .consensus = consensus};
            }
            else
            {
                // We validated a ledger and then built it locally
                JLOG(j_.debug()) << "MATCH: seq=" << index << " late";
            }
        }

        entry.built.emplace(hash);
        entry.builtConsensusHash.emplace(consensusHash);
        entry.consensus.emplace(std::move(consensus));
    });

    if (mismatch)
    {
        handleMismatch(
            hash,
            mismatch->otherHash,
            consensusHash,
            mismatch->otherConsensusHash,
            mismatch->consensus);
    }
}

void
LedgerHistory::validatedLedger(
    std::shared_ptr<Ledger const> const& ledger,
    std::optional<uint256> const& consensusHash)
{
    LedgerIndex const index = ledger->header().seq;
    LedgerHash const hash = ledger->header().hash;
    XRPL_ASSERT(!hash.isZero(), "xrpl::LedgerHistory::validatedLedger : nonzero hash");

    std::optional<MismatchInputs> mismatch;

    consensusValidated_.fetchAndModify(index, [&](CvEntry& entry) {
        if (entry.built && !entry.validated)
        {
            SOMETIMES(true, "xrpl::LedgerHistory::validatedLedger : built arrived first");
            XRPL_ASSERT(
                entry.consensus.has_value(),
                "xrpl::LedgerHistory::validatedLedger : consensus set when built set");
            if (entry.built.value() != hash)
            {
                SOMETIMES(true, "xrpl::LedgerHistory::validatedLedger : built-first mismatch");
                JLOG(j_.error()) << "MISMATCH: seq=" << index << " built:" << entry.built.value()
                                 << " then:" << hash;
                mismatch = MismatchInputs{
                    .otherHash = entry.built.value(),
                    .otherConsensusHash = entry.builtConsensusHash,
                    .consensus =
                        entry.consensus.value()};  // NOLINT(bugprone-unchecked-optional-access)
                                                   // consensus always emplaced with built
            }
            else
            {
                // We built a ledger locally and then validated it
                JLOG(j_.debug()) << "MATCH: seq=" << index;
            }
        }

        entry.validated.emplace(hash);
        entry.validatedConsensusHash = consensusHash;
    });

    if (mismatch)
    {
        handleMismatch(
            mismatch->otherHash,
            hash,
            mismatch->otherConsensusHash,
            consensusHash,
            mismatch->consensus);
    }
}

/** Ensure byHash doesn't have the wrong hash for a particular index
 */
bool
LedgerHistory::fixIndex(LedgerIndex ledgerIndex, LedgerHash const& ledgerHash)
{
    auto lock = ledgerMaps_.lock();
    if (auto it = lock->byIndex.find(ledgerIndex); it != lock->byIndex.end())
    {
        if (it->second != ledgerHash)
        {
            it->second = ledgerHash;
            return false;
        }
    }
    return true;
}

void
LedgerHistory::clearLedgerCachePrior(LedgerIndex seq)
{
    std::size_t hashesCleared = 0;
    std::size_t indexesCleared = 0;
    std::size_t cacheSize = 0;
    std::size_t indexSize = 0;

    std::vector<LedgerHash> const keys = [this] {
        auto lock = ledgerMaps_.lock();
        return lock->byHash->getKeys();
    }();

    for (LedgerHash const& it : keys)
    {
        auto const ledger = getLedgerByHash(it);
        if (!ledger || ledger->header().seq < seq)
        {
            auto lock = ledgerMaps_.lock();
            lock->byHash->del(it, false);
            ++hashesCleared;
        }
    }

    {
        auto lock = ledgerMaps_.lock();
        cacheSize = lock->byHash->size();

        indexesCleared =
            std::erase_if(lock->byIndex, [seq](auto const& kv) { return kv.first < seq; });
        indexSize = lock->byIndex.size();

        ALWAYS(
            lock->byIndex.empty() || lock->byIndex.begin()->first >= seq,
            "xrpl::LedgerHistory::clearLedgerCachePrior : byIndex pruned to seq");
    }

    JLOG(j_.debug()) << "LedgersByHash: cleared " << hashesCleared << " entries before seq " << seq
                     << " (total now " << cacheSize << ")";

    JLOG(j_.debug()) << "LedgersByIndex: cleared " << indexesCleared << " index entries before seq "
                     << seq << " (total now " << indexSize << ")";
}

}  // namespace xrpl
