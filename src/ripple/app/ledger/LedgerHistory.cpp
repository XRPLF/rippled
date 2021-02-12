//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/app/ledger/LedgerHistory.h>
#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/chrono.h>
#include <ripple/basics/contract.h>
#include <ripple/json/to_string.h>

namespace ripple {

// VFALCO TODO replace macros

#ifndef CACHED_LEDGER_NUM
#define CACHED_LEDGER_NUM 96
#endif

std::chrono::seconds constexpr CachedLedgerAge = std::chrono::minutes{2};

// FIXME: Need to clean up ledgers by index at some point

LedgerHistory::LedgerHistory(
    beast::insight::Collector::ptr const& collector,
    Application& app)
    : app_(app)
    , collector_(collector)
    , mismatch_counter_(collector->make_counter("ledger.history", "mismatch"))
    , m_ledgers_by_hash(
          "LedgerCache",
          CACHED_LEDGER_NUM,
          CachedLedgerAge,
          stopwatch(),
          app_.journal("TaggedCache"))
    , m_consensus_validated(
          "ConsensusValidated",
          64,
          std::chrono::minutes{5},
          stopwatch(),
          app_.journal("TaggedCache"))
    , j_(app.journal("LedgerHistory"))
{
}

bool
LedgerHistory::insert(std::shared_ptr<Ledger const> ledger, bool validated)
{
    if (!ledger->isImmutable())
        LogicError("mutable Ledger in insert");

    assert(ledger->stateMap().getHash().isNonZero());

    std::unique_lock sl(m_ledgers_by_hash.peekMutex());

    const bool alreadyHad = m_ledgers_by_hash.canonicalize_replace_cache(
        ledger->info().hash, ledger);
    if (validated)
        mLedgersByIndex[ledger->info().seq] = ledger->info().hash;

    return alreadyHad;
}

LedgerHash
LedgerHistory::getLedgerHash(LedgerIndex index)
{
    std::unique_lock sl(m_ledgers_by_hash.peekMutex());
    auto it = mLedgersByIndex.find(index);

    if (it != mLedgersByIndex.end())
        return it->second;

    return uint256();
}

std::shared_ptr<Ledger const>
LedgerHistory::getLedgerBySeq(LedgerIndex index)
{
    {
        std::unique_lock sl(m_ledgers_by_hash.peekMutex());
        auto it = mLedgersByIndex.find(index);

        if (it != mLedgersByIndex.end())
        {
            uint256 hash = it->second;
            sl.unlock();
            return getLedgerByHash(hash);
        }
    }

    std::shared_ptr<Ledger const> ret = loadByIndex(index, app_);

    if (!ret)
        return ret;

    assert(ret->info().seq == index);

    {
        // Add this ledger to the local tracking by index
        std::unique_lock sl(m_ledgers_by_hash.peekMutex());

        assert(ret->isImmutable());
        m_ledgers_by_hash.canonicalize_replace_client(ret->info().hash, ret);
        mLedgersByIndex[ret->info().seq] = ret->info().hash;
        return (ret->info().seq == index) ? ret : nullptr;
    }
}

std::shared_ptr<Ledger const>
LedgerHistory::getLedgerByHash(LedgerHash const& hash)
{
    auto ret = m_ledgers_by_hash.fetch(hash);

    if (ret)
    {
        assert(ret->isImmutable());
        assert(ret->info().hash == hash);
        return ret;
    }

    ret = loadByHash(hash, app_);

    if (!ret)
        return ret;

    assert(ret->isImmutable());
    assert(ret->info().hash == hash);
    m_ledgers_by_hash.canonicalize_replace_client(ret->info().hash, ret);
    assert(ret->info().hash == hash);

    return ret;
}

static void
log_one(
    ReadView const& ledger,
    uint256 const& tx,
    char const* msg,
    beast::Journal& j)
{
    auto metaData = ledger.txRead(tx).second;

    if (metaData != nullptr)
    {
        JLOGV(
            j.debug(),
            "MISMATCH on TX",
            jv("which_is_missing_tx", msg),
            jv("tx", tx),
            jv("metadata", metaData->getJson(JsonOptions::none)));
    }
    else
    {
        JLOGV(
            j.debug(),
            "MISMATCH on TX",
            jv("which_is_missing_tx", msg),
            jv("tx", tx));
    }
}

static void
log_metadata_difference(
    ReadView const& builtLedger,
    ReadView const& validLedger,
    uint256 const& tx,
    beast::Journal j)
{
    auto getMeta = [](ReadView const& ledger,
                      uint256 const& txID) -> std::shared_ptr<TxMeta> {
        auto meta = ledger.txRead(txID).second;
        if (!meta)
            return {};
        return std::make_shared<TxMeta>(txID, ledger.seq(), *meta);
    };

    auto validMetaData = getMeta(validLedger, tx);
    auto builtMetaData = getMeta(builtLedger, tx);
    assert(validMetaData != nullptr || builtMetaData != nullptr);

    if (validMetaData != nullptr && builtMetaData != nullptr)
    {
        auto const& validNodes = validMetaData->getNodes();
        auto const& builtNodes = builtMetaData->getNodes();

        bool const result_diff =
            validMetaData->getResultTER() != builtMetaData->getResultTER();

        bool const index_diff =
            validMetaData->getIndex() != builtMetaData->getIndex();

        bool const nodes_diff = validNodes != builtNodes;

        if (!result_diff && !index_diff && !nodes_diff)
        {
            JLOG(j.error()) << "MISMATCH on TX " << tx
                            << ": No apparent mismatches detected!";
            return;
        }

        if (!nodes_diff)
        {
            if (result_diff && index_diff)
            {
                // TODO support `jvGroup` construct and put "built" and "valid"
                // into different groups
                JLOGV(
                    j.debug(),
                    "MISMATCH on TX : Different result and index!",
                    jv("tx", tx),
                    jv("builtResult", builtMetaData->getResult()),
                    jv("builtIndex", builtMetaData->getIndex()),
                    jv("validResult", validMetaData->getResult()),
                    jv("validIndex", validMetaData->getIndex()));
            }
            else if (result_diff)
            {
                JLOGV(
                    j.debug(),
                    "MISMATCH on TX : Different result!",
                    jv("tx", tx),
                    jv("builtResult", builtMetaData->getResult()),
                    jv("validResult", validMetaData->getResult()));
            }
            else if (index_diff)
            {
                JLOGV(
                    j.debug(),
                    "MISMATCH on TX : Different index!",
                    jv("tx", tx),
                    jv("builtIndex", builtMetaData->getIndex()),
                    jv("validIndex", validMetaData->getIndex()));
            }
        }
        else
        {
            if (result_diff && index_diff)
            {
                JLOGV(
                    j.debug(),
                    "MISMATCH on TX : Different result, index and nodes!",
                    jv("tx", tx),
                    jv("built", builtMetaData->getJson(JsonOptions::none)),
                    jv("valid", validMetaData->getJson(JsonOptions::none)));
            }
            else if (result_diff)
            {
                JLOGV(
                    j.debug(),
                    "MISMATCH on TX : Different result and nodes!",
                    jv("tx", tx),
                    jv("builtResult", builtMetaData->getResult()),
                    jv("builtNodes", builtNodes.getJson(JsonOptions::none)),
                    jv("validResult", validMetaData->getResult()),
                    jv("validNodes", validNodes.getJson(JsonOptions::none)));
            }
            else if (index_diff)
            {
                JLOGV(
                    j.debug(),
                    "MISMATCH on TX : Different index and nodes!",
                    jv("tx", tx),
                    jv("builtIndex", builtMetaData->getIndex()),
                    jv("builtNodes", builtNodes.getJson(JsonOptions::none)),
                    jv("validIndex", validMetaData->getIndex()),
                    jv("validNodes", validNodes.getJson(JsonOptions::none)));
            }
            else  // nodes_diff
            {
                JLOGV(
                    j.debug(),
                    "MISMATCH on TX : Different nodes!",
                    jv("tx", tx),
                    jv("builtNodes", builtNodes.getJson(JsonOptions::none)),
                    jv("validNodes", validNodes.getJson(JsonOptions::none)));
            }
        }
    }
    else if (validMetaData != nullptr)
    {
        JLOGV(
            j.error(),
            "MISMATCH on TX : Metadata Difference (built has none)",
            jv("tx", tx),
            jv("validMetaData", validMetaData->getJson(JsonOptions::none)));
    }
    else  // builtMetaData != nullptr
    {
        JLOGV(
            j.error(),
            "MISMATCH on TX : Metadata Difference (valid has none)",
            jv("builtMetaData", builtMetaData->getJson(JsonOptions::none)));
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
    std::sort(
        v.begin(), v.end(), [](SHAMapItem const* lhs, SHAMapItem const* rhs) {
            return lhs->key() < rhs->key();
        });
    return v;
}

void
LedgerHistory::handleMismatch(
    LedgerHash const& built,
    LedgerHash const& valid,
    boost::optional<uint256> const& builtConsensusHash,
    boost::optional<uint256> const& validatedConsensusHash,
    Json::Value const& consensus)
{
    assert(built != valid);
    ++mismatch_counter_;

    auto builtLedger = getLedgerByHash(built);
    auto validLedger = getLedgerByHash(valid);

    if (!builtLedger || !validLedger)
    {
        JLOGV(
            j_.error(),
            "MISMATCH cannot be analyzed",
            jv("builtLedgerHash", to_string(built)),
            jv("builtLedger", builtLedger),
            jv("validLedgerHash", to_string(valid)),
            jv("validLedger", validLedger));
        return;
    }

    assert(builtLedger->info().seq == validLedger->info().seq);

    if (auto stream = j_.debug())
    {
        JLOGV(
            stream,
            "LedgerHistory::handleMismatch",
            jv("built", getJson({*builtLedger, {}})),
            jv("valid", getJson({*validLedger, {}})),
            jv("consensus", consensus));
    }

    // Determine the mismatch reason, distinguishing Byzantine
    // failure from transaction processing difference

    // Disagreement over prior ledger indicates sync issue
    if (builtLedger->info().parentHash != validLedger->info().parentHash)
    {
        JLOG(j_.error()) << "MISMATCH on prior ledger";
        return;
    }

    // Disagreement over close time indicates Byzantine failure
    if (builtLedger->info().closeTime != validLedger->info().closeTime)
    {
        JLOG(j_.error()) << "MISMATCH on close time";
        return;
    }

    if (builtConsensusHash && validatedConsensusHash)
    {
        if (builtConsensusHash != validatedConsensusHash)
            JLOGV(
                j_.error(),
                "MISMATCH on consensus transaction set",
                jv("built", to_string(*builtConsensusHash)),
                jv("validated", to_string(*validatedConsensusHash)));
        else
            JLOGV(
                j_.error(),
                "MISMATCH with same consensus transaction set",
                jv("buildConsensusHash", to_string(*builtConsensusHash)));
    }

    // Find differences between built and valid ledgers
    auto const builtTx = leaves(builtLedger->txMap());
    auto const validTx = leaves(validLedger->txMap());

    if (builtTx == validTx)
        JLOGV(
            j_.error(),
            "MISMATCH with same transactions",
            jv("size", builtTx.size()));
    else
        JLOGV(
            j_.error(),
            "MISMATCH with different transactions",
            jv("buildSize", builtTx.size()),
            jv("validSize", validTx.size()),
            jv("built", getJson({*builtLedger, {}})),
            jv("valid", getJson({*validLedger, {}})));

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
            if ((*b)->peekData() != (*v)->peekData())
            {
                // Same transaction with different metadata
                log_metadata_difference(
                    *builtLedger, *validLedger, (*b)->key(), j_);
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
    LedgerIndex index = ledger->info().seq;
    LedgerHash hash = ledger->info().hash;
    assert(!hash.isZero());

    std::unique_lock sl(m_consensus_validated.peekMutex());

    auto entry = std::make_shared<cv_entry>();
    m_consensus_validated.canonicalize_replace_client(index, entry);

    if (entry->validated && !entry->built)
    {
        if (entry->validated.get() != hash)
        {
            JLOGV(
                j_.error(),
                "MISMATCH",
                jv("seq", index),
                jv("validated", entry->validated.get()),
                jv("hash", hash));

            handleMismatch(
                hash,
                entry->validated.get(),
                consensusHash,
                entry->validatedConsensusHash,
                consensus);
        }
        else
        {
            // We validated a ledger and then built it locally
            JLOGV(j_.debug(), "MATCH late", jv("seq", index));
        }
    }

    entry->built.emplace(hash);
    entry->builtConsensusHash.emplace(consensusHash);
    entry->consensus.emplace(std::move(consensus));
}

void
LedgerHistory::validatedLedger(
    std::shared_ptr<Ledger const> const& ledger,
    boost::optional<uint256> const& consensusHash)
{
    LedgerIndex index = ledger->info().seq;
    LedgerHash hash = ledger->info().hash;
    assert(!hash.isZero());

    std::unique_lock sl(m_consensus_validated.peekMutex());

    auto entry = std::make_shared<cv_entry>();
    m_consensus_validated.canonicalize_replace_client(index, entry);

    if (entry->built && !entry->validated)
    {
        if (entry->built.get() != hash)
        {
            JLOGV(
                j_.error(),
                "MISMATCH",
                jv("seq", index),
                jv("built", entry->built.get()),
                jv("hash", hash));

            handleMismatch(
                entry->built.get(),
                hash,
                entry->builtConsensusHash,
                consensusHash,
                entry->consensus.get());
        }
        else
        {
            // We built a ledger locally and then validated it
            JLOGV(j_.debug(), "MATCH", jv("seq", index));
        }
    }

    entry->validated.emplace(hash);
    entry->validatedConsensusHash = consensusHash;
}

/** Ensure m_ledgers_by_hash doesn't have the wrong hash for a particular index
 */
bool
LedgerHistory::fixIndex(LedgerIndex ledgerIndex, LedgerHash const& ledgerHash)
{
    std::unique_lock sl(m_ledgers_by_hash.peekMutex());
    auto it = mLedgersByIndex.find(ledgerIndex);

    if ((it != mLedgersByIndex.end()) && (it->second != ledgerHash))
    {
        it->second = ledgerHash;
        return false;
    }
    return true;
}

void
LedgerHistory::tune(int size, std::chrono::seconds age)
{
    m_ledgers_by_hash.setTargetSize(size);
    m_ledgers_by_hash.setTargetAge(age);
}

void
LedgerHistory::clearLedgerCachePrior(LedgerIndex seq)
{
    for (LedgerHash it : m_ledgers_by_hash.getKeys())
    {
        auto const ledger = getLedgerByHash(it);
        if (!ledger || ledger->info().seq < seq)
            m_ledgers_by_hash.del(it, false);
    }
}

}  // namespace ripple
