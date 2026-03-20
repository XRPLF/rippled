#include <xrpld/app/ledger/LedgerHistory.h>
#include <xrpld/app/ledger/LedgerToJson.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/contract.h>
#include <xrpl/json/to_string.h>

namespace xrpl {

LedgerHistory::LedgerHistory(beast::insight::Collector::ptr const& collector, Application& app)
    : app_(app)
    , collector_(collector)
    , mismatch_counter_(collector->make_counter("ledger.history", "mismatch"))
    , m_consensus_validated(
          "ConsensusValidated",
          64,
          std::chrono::minutes{5},
          stopwatch(),
          app_.journal("TaggedCache"))
    , j_(app.journal("LedgerHistory"))
{
    auto lock = ledger_maps_.lock();
    lock->by_hash = std::make_unique<LedgerMaps::LedgersByHash>(
        "LedgerCache",
        app_.config().getValueFor(SizedItem::ledgerSize),
        std::chrono::seconds{app_.config().getValueFor(SizedItem::ledgerAge)},
        stopwatch(),
        app_.journal("TaggedCache"));
}

bool
LedgerHistory::insert(std::shared_ptr<Ledger const> const& ledger, bool validated)
{
    if (!ledger->isImmutable())
        LogicError("mutable Ledger in insert");

    XRPL_ASSERT(
        ledger->stateMap().getHash().isNonZero(), "xrpl::LedgerHistory::insert : nonzero hash");

    auto lock = ledger_maps_.lock();
    bool const alreadyHad =
        lock->by_hash->canonicalize_replace_cache(ledger->header().hash, ledger);
    if (validated)
        lock->by_index[ledger->header().seq] = ledger->header().hash;

    return alreadyHad;
}

LedgerHash
LedgerHistory::getLedgerHash(LedgerIndex index)
{
    auto lock = ledger_maps_.lock();
    if (auto it = lock->by_index.find(index); it != lock->by_index.end())
        return it->second;
    return {};
}

std::shared_ptr<Ledger const>
LedgerHistory::getLedgerBySeq(LedgerIndex index)
{
    uint256 hash;
    {
        auto lock = ledger_maps_.lock();
        if (auto it = lock->by_index.find(index); it != lock->by_index.end())
        {
            hash = it->second;
        }
        else
        {
            hash = {};
        }
    }

    if (!hash.isZero())
        return getLedgerByHash(hash);

    std::shared_ptr<Ledger const> ret = loadByIndex(index, app_);

    if (!ret)
        return ret;

    XRPL_ASSERT(
        ret->header().seq == index, "xrpl::LedgerHistory::getLedgerBySeq : result sequence match");

    {
        // Add this ledger to the local tracking by index
        auto lock = ledger_maps_.lock();
        XRPL_ASSERT(
            ret->isImmutable(), "xrpl::LedgerHistory::getLedgerBySeq : immutable result ledger");
        lock->by_hash->canonicalize_replace_client(ret->header().hash, ret);
        lock->by_index[ret->header().seq] = ret->header().hash;
        return (ret->header().seq == index) ? ret : nullptr;
    }
}

std::shared_ptr<Ledger const>
LedgerHistory::getLedgerByHash(LedgerHash const& hash)
{
    std::shared_ptr<Ledger const> ret;
    {
        auto lock = ledger_maps_.lock();
        ret = lock->by_hash->fetch(hash);
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

    ret = loadByHash(hash, app_);

    if (!ret)
        return ret;

    XRPL_ASSERT(
        ret->isImmutable(), "xrpl::LedgerHistory::getLedgerByHash : immutable loaded ledger");
    XRPL_ASSERT(
        ret->header().hash == hash,
        "xrpl::LedgerHistory::getLedgerByHash : loaded ledger hash match");
    {
        auto lock = ledger_maps_.lock();
        lock->by_hash->canonicalize_replace_client(ret->header().hash, ret);
    }
    XRPL_ASSERT(
        ret->header().hash == hash, "xrpl::LedgerHistory::getLedgerByHash : result hash match");

    return ret;
}

static void
log_one(ReadView const& ledger, uint256 const& tx, char const* msg, beast::Journal& j)
{
    auto metaData = ledger.txRead(tx).second;

    if (metaData != nullptr)
    {
        JLOG(j.debug()) << "MISMATCH on TX " << tx << ": " << msg
                        << " is missing this transaction:\n"
                        << metaData->getJson(JsonOptions::none);
    }
    else
    {
        JLOG(j.debug()) << "MISMATCH on TX " << tx << ": " << msg
                        << " is missing this transaction.";
    }
}

static void
log_metadata_difference(
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
        validMetaData || builtMetaData, "xrpl::log_metadata_difference : some metadata present");

    if (validMetaData && builtMetaData)
    {
        auto const& validNodes = validMetaData->getNodes();
        auto const& builtNodes = builtMetaData->getNodes();

        bool const result_diff = validMetaData->getResultTER() != builtMetaData->getResultTER();

        bool const index_diff = validMetaData->getIndex() != builtMetaData->getIndex();

        bool const nodes_diff = validNodes != builtNodes;

        if (!result_diff && !index_diff && !nodes_diff)
        {
            JLOG(j.error()) << "MISMATCH on TX " << tx << ": No apparent mismatches detected!";
            return;
        }

        if (!nodes_diff)
        {
            if (result_diff && index_diff)
            {
                JLOG(j.debug()) << "MISMATCH on TX " << tx << ": Different result and index!";
                JLOG(j.debug()) << " Built:"
                                << " Result: " << builtMetaData->getResult()
                                << " Index: " << builtMetaData->getIndex();
                JLOG(j.debug()) << " Valid:"
                                << " Result: " << validMetaData->getResult()
                                << " Index: " << validMetaData->getIndex();
            }
            else if (result_diff)
            {
                JLOG(j.debug()) << "MISMATCH on TX " << tx << ": Different result!";
                JLOG(j.debug()) << " Built:"
                                << " Result: " << builtMetaData->getResult();
                JLOG(j.debug()) << " Valid:"
                                << " Result: " << validMetaData->getResult();
            }
            else if (index_diff)
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
            if (result_diff && index_diff)
            {
                JLOG(j.debug()) << "MISMATCH on TX " << tx
                                << ": Different result, index and nodes!";
                JLOG(j.debug()) << " Built:\n" << builtMetaData->getJson(JsonOptions::none);
                JLOG(j.debug()) << " Valid:\n" << validMetaData->getJson(JsonOptions::none);
            }
            else if (result_diff)
            {
                JLOG(j.debug()) << "MISMATCH on TX " << tx << ": Different result and nodes!";
                JLOG(j.debug()) << " Built:"
                                << " Result: " << builtMetaData->getResult() << " Nodes:\n"
                                << builtNodes.getJson(JsonOptions::none);
                JLOG(j.debug()) << " Valid:"
                                << " Result: " << validMetaData->getResult() << " Nodes:\n"
                                << validNodes.getJson(JsonOptions::none);
            }
            else if (index_diff)
            {
                JLOG(j.debug()) << "MISMATCH on TX " << tx << ": Different index and nodes!";
                JLOG(j.debug()) << " Built:"
                                << " Index: " << builtMetaData->getIndex() << " Nodes:\n"
                                << builtNodes.getJson(JsonOptions::none);
                JLOG(j.debug()) << " Valid:"
                                << " Index: " << validMetaData->getIndex() << " Nodes:\n"
                                << validNodes.getJson(JsonOptions::none);
            }
            else  // nodes_diff
            {
                JLOG(j.debug()) << "MISMATCH on TX " << tx << ": Different nodes!";
                JLOG(j.debug()) << " Built:"
                                << " Nodes:\n"
                                << builtNodes.getJson(JsonOptions::none);
                JLOG(j.debug()) << " Valid:"
                                << " Nodes:\n"
                                << validNodes.getJson(JsonOptions::none);
            }
        }

        return;
    }

    if (validMetaData)
    {
        JLOG(j.error()) << "MISMATCH on TX " << tx << ": Metadata Difference. Valid=\n"
                        << validMetaData->getJson(JsonOptions::none);
    }

    if (builtMetaData)
    {
        JLOG(j.error()) << "MISMATCH on TX " << tx << ": Metadata Difference. Built=\n"
                        << builtMetaData->getJson(JsonOptions::none);
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
    std::sort(v.begin(), v.end(), [](SHAMapItem const* lhs, SHAMapItem const* rhs) {
        return lhs->key() < rhs->key();
    });
    return v;
}

void
LedgerHistory::handleMismatch(
    LedgerHash const& built,
    LedgerHash const& valid,
    std::optional<uint256> const& builtConsensusHash,
    std::optional<uint256> const& validatedConsensusHash,
    Json::Value const& consensus)
{
    XRPL_ASSERT(built != valid, "xrpl::LedgerHistory::handleMismatch : unequal hashes");
    ++mismatch_counter_;

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
            JLOG(j_.error()) << "MISMATCH with same consensus transaction set: "
                             << to_string(*builtConsensusHash);
    }

    // Find differences between built and valid ledgers
    auto const builtTx = leaves(builtLedger->txMap());
    auto const validTx = leaves(validLedger->txMap());

    if (builtTx == validTx)
    {
        JLOG(j_.error()) << "MISMATCH with same " << builtTx.size() << " transactions";
    }
    else
        JLOG(j_.error()) << "MISMATCH with " << builtTx.size() << " built and " << validTx.size()
                         << " valid transactions.";

    JLOG(j_.error()) << "built\n" << getJson({*builtLedger, {}});
    JLOG(j_.error()) << "valid\n" << getJson({*validLedger, {}});

    // Log all differences between built and valid ledgers
    auto b = builtTx.begin();
    auto v = validTx.begin();
    while (b != builtTx.end() && v != validTx.end())
    {
        if ((*b)->key() < (*v)->key())
        {
            log_one(*builtLedger, (*b)->key(), "valid", j_);
            ++b;
        }
        else if ((*b)->key() > (*v)->key())
        {
            log_one(*validLedger, (*v)->key(), "built", j_);
            ++v;
        }
        else
        {
            if ((*b)->slice() != (*v)->slice())
            {
                // Same transaction with different metadata
                log_metadata_difference(*builtLedger, *validLedger, (*b)->key(), j_);
            }
            ++b;
            ++v;
        }
    }
    for (; b != builtTx.end(); ++b)
        log_one(*builtLedger, (*b)->key(), "valid", j_);
    for (; v != validTx.end(); ++v)
        log_one(*validLedger, (*v)->key(), "built", j_);
}

void
LedgerHistory::builtLedger(
    std::shared_ptr<Ledger const> const& ledger,
    uint256 const& consensusHash,
    Json::Value consensus)
{
    LedgerIndex index = ledger->header().seq;
    LedgerHash hash = ledger->header().hash;
    XRPL_ASSERT(!hash.isZero(), "xrpl::LedgerHistory::builtLedger : nonzero hash");

    m_consensus_validated.fetch_and_modify(index, [&](cv_entry& entry) {
        if (entry.validated && !entry.built)
        {
            if (entry.validated.value() != hash)
            {
                JLOG(j_.error()) << "MISMATCH: seq=" << index
                                 << " validated:" << entry.validated.value() << " then:" << hash;
                handleMismatch(
                    hash,
                    entry.validated.value(),
                    consensusHash,
                    entry.validatedConsensusHash,
                    consensus);
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
}

void
LedgerHistory::validatedLedger(
    std::shared_ptr<Ledger const> const& ledger,
    std::optional<uint256> const& consensusHash)
{
    LedgerIndex index = ledger->header().seq;
    LedgerHash hash = ledger->header().hash;
    XRPL_ASSERT(!hash.isZero(), "xrpl::LedgerHistory::validatedLedger : nonzero hash");

    m_consensus_validated.fetch_and_modify(index, [&](cv_entry& entry) {
        if (entry.built && !entry.validated)
        {
            if (entry.built.value() != hash)
            {
                JLOG(j_.error()) << "MISMATCH: seq=" << index << " built:" << entry.built.value()
                                 << " then:" << hash;
                handleMismatch(
                    entry.built.value(),
                    hash,
                    entry.builtConsensusHash,
                    consensusHash,
                    entry.consensus.value());
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
}

/** Ensure m_ledgers_by_hash doesn't have the wrong hash for a particular index
 */
bool
LedgerHistory::fixIndex(LedgerIndex ledgerIndex, LedgerHash const& ledgerHash)
{
    auto lock = ledger_maps_.lock();
    if (auto it = lock->by_index.find(ledgerIndex); it != lock->by_index.end())
    {
        if (it->second != ledgerHash)
        {
            lock->by_index[ledgerIndex] = ledgerHash;
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

    {
        auto lock = ledger_maps_.lock();
        for (LedgerHash it : lock->by_hash->getKeys())
        {
            auto const ledger = getLedgerByHash(it);
            if (!ledger || ledger->header().seq < seq)
            {
                lock->by_hash->del(it, false);
                ++hashesCleared;
            }
        }
        cacheSize = lock->by_hash->size();

        auto it = lock->by_index.begin();
        while (it != lock->by_index.end())
        {
            if (it->first < seq)
            {
                it = lock->by_index.erase(it);
                ++indexesCleared;
            }
            else
            {
                ++it;
            }
        }
        indexSize = lock->by_index.size();
    }

    JLOG(j_.debug()) << "LedgersByHash: cleared " << hashesCleared << " entries before seq " << seq
                     << " (total now " << cacheSize << ")";

    JLOG(j_.debug()) << "LedgersByIndex: cleared " << indexesCleared << " index entries before seq "
                     << seq << " (total now " << indexSize << ")";
}

}  // namespace xrpl
