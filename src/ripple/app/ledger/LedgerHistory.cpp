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

#include <BeastConfig.h>
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

#ifndef CACHED_LEDGER_AGE
#define CACHED_LEDGER_AGE 120
#endif

// FIXME: Need to clean up ledgers by index at some point

LedgerHistory::LedgerHistory (
    beast::insight::Collector::ptr const& collector,
        Application& app)
    : app_ (app)
    , collector_ (collector)
    , mismatch_counter_ (collector->make_counter ("ledger.history", "mismatch"))
    , m_ledgers_by_hash ("LedgerCache", CACHED_LEDGER_NUM, CACHED_LEDGER_AGE,
        stopwatch(), app_.journal("TaggedCache"))
    , m_consensus_validated ("ConsensusValidated", 64, 300,
        stopwatch(), app_.journal("TaggedCache"))
    , j_ (app.journal ("LedgerHistory"))
{
}

bool
LedgerHistory::insert(
    std::shared_ptr<Ledger const> ledger,
        bool validated)
{
    if(! ledger->isImmutable())
        LogicError("mutable Ledger in insert");

    assert (ledger->stateMap().getHash ().isNonZero ());

    LedgersByHash::ScopedLockType sl (m_ledgers_by_hash.peekMutex ());

    const bool alreadyHad = m_ledgers_by_hash.canonicalize (
        ledger->info().hash, ledger, true);
    if (validated)
        mLedgersByIndex[ledger->info().seq] = ledger->info().hash;

    return alreadyHad;
}

LedgerHash LedgerHistory::getLedgerHash (LedgerIndex index)
{
    LedgersByHash::ScopedLockType sl (m_ledgers_by_hash.peekMutex ());
    auto it = mLedgersByIndex.find (index);

    if (it != mLedgersByIndex.end ())
        return it->second;

    return uint256 ();
}

std::shared_ptr<Ledger const>
LedgerHistory::getLedgerBySeq (LedgerIndex index)
{
    {
        LedgersByHash::ScopedLockType sl (m_ledgers_by_hash.peekMutex ());
        auto it = mLedgersByIndex.find (index);

        if (it != mLedgersByIndex.end ())
        {
            uint256 hash = it->second;
            sl.unlock ();
            return getLedgerByHash (hash);
        }
    }

    std::shared_ptr<Ledger const> ret = loadByIndex (index, app_);

    if (!ret)
        return ret;

    assert (ret->info().seq == index);

    {
        // Add this ledger to the local tracking by index
        LedgersByHash::ScopedLockType sl (m_ledgers_by_hash.peekMutex ());

        assert (ret->isImmutable ());
        m_ledgers_by_hash.canonicalize (ret->info().hash, ret);
        mLedgersByIndex[ret->info().seq] = ret->info().hash;
        return (ret->info().seq == index) ? ret : nullptr;
    }
}

std::shared_ptr<Ledger const>
LedgerHistory::getLedgerByHash (LedgerHash const& hash)
{
    auto ret = m_ledgers_by_hash.fetch (hash);

    if (ret)
    {
        assert (ret->isImmutable ());
        assert (ret->info().hash == hash);
        return ret;
    }

    ret = loadByHash (hash, app_);

    if (!ret)
        return ret;

    assert (ret->isImmutable ());
    assert (ret->info().hash == hash);
    m_ledgers_by_hash.canonicalize (ret->info().hash, ret);
    assert (ret->info().hash == hash);

    return ret;
}

static
void
log_one(
    ReadView const& ledger,
    uint256 const& tx,
    char const* msg,
    beast::Journal& j)
{
    auto metaData = ledger.txRead(tx).second;

    if (metaData != nullptr)
    {
        JLOG (j.debug()) << "MISMATCH on TX " << tx <<
            ": " << msg << " is missing this transaction:\n" <<
            metaData->getJson (0);
    }
    else
    {
        JLOG (j.debug()) << "MISMATCH on TX " << tx <<
            ": " << msg << " is missing this transaction.";
    }
}

static
void
log_metadata_difference(
    ReadView const& builtLedger,
    ReadView const& validLedger,
    uint256 const& tx,
    beast::Journal j)
{
    auto getMeta = [j](ReadView const& ledger,
        uint256 const& txID) -> std::shared_ptr<TxMeta>
    {
        auto meta = ledger.txRead(txID).second;
        if (!meta)
            return {};
        return std::make_shared<TxMeta> (txID, ledger.seq(), *meta, j);
    };

    auto validMetaData = getMeta (validLedger, tx);
    auto builtMetaData = getMeta (builtLedger, tx);
    assert(validMetaData != nullptr || builtMetaData != nullptr);

    if (validMetaData != nullptr && builtMetaData != nullptr)
    {
        auto const& validNodes = validMetaData->getNodes ();
        auto const& builtNodes = builtMetaData->getNodes ();

        bool const result_diff =
            validMetaData->getResultTER () != builtMetaData->getResultTER ();

        bool const index_diff =
            validMetaData->getIndex() != builtMetaData->getIndex ();

        bool const nodes_diff = validNodes != builtNodes;

        if (!result_diff && !index_diff && !nodes_diff)
        {
            JLOG (j.error()) << "MISMATCH on TX " << tx <<
                ": No apparent mismatches detected!";
            return;
        }

        if (!nodes_diff)
        {
            if (result_diff && index_diff)
            {
                JLOG (j.debug()) << "MISMATCH on TX " << tx <<
                    ": Different result and index!";
                JLOG (j.debug()) << " Built:" <<
                    " Result: " << builtMetaData->getResult () <<
                    " Index: " << builtMetaData->getIndex ();
                JLOG (j.debug()) << " Valid:" <<
                    " Result: " << validMetaData->getResult () <<
                    " Index: " << validMetaData->getIndex ();
            }
            else if (result_diff)
            {
                JLOG (j.debug()) << "MISMATCH on TX " << tx <<
                    ": Different result!";
                JLOG (j.debug()) << " Built:" <<
                    " Result: " << builtMetaData->getResult ();
                JLOG (j.debug()) << " Valid:" <<
                    " Result: " << validMetaData->getResult ();
            }
            else if (index_diff)
            {
                JLOG (j.debug()) << "MISMATCH on TX " << tx <<
                    ": Different index!";
                JLOG (j.debug()) << " Built:" <<
                    " Index: " << builtMetaData->getIndex ();
                JLOG (j.debug()) << " Valid:" <<
                    " Index: " << validMetaData->getIndex ();
            }
        }
        else
        {
            if (result_diff && index_diff)
            {
                JLOG (j.debug()) << "MISMATCH on TX " << tx <<
                    ": Different result, index and nodes!";
                JLOG (j.debug()) << " Built:\n" <<
                    builtMetaData->getJson (0);
                JLOG (j.debug()) << " Valid:\n" <<
                    validMetaData->getJson (0);
            }
            else if (result_diff)
            {
                JLOG (j.debug()) << "MISMATCH on TX " << tx <<
                    ": Different result and nodes!";
                JLOG (j.debug()) << " Built:" <<
                    " Result: " << builtMetaData->getResult () <<
                    " Nodes:\n" << builtNodes.getJson (0);
                JLOG (j.debug()) << " Valid:" <<
                    " Result: " << validMetaData->getResult () <<
                    " Nodes:\n" << validNodes.getJson (0);
            }
            else if (index_diff)
            {
                JLOG (j.debug()) << "MISMATCH on TX " << tx <<
                    ": Different index and nodes!";
                JLOG (j.debug()) << " Built:" <<
                    " Index: " << builtMetaData->getIndex () <<
                    " Nodes:\n" << builtNodes.getJson (0);
                JLOG (j.debug()) << " Valid:" <<
                    " Index: " << validMetaData->getIndex () <<
                    " Nodes:\n" << validNodes.getJson (0);
            }
            else // nodes_diff
            {
                JLOG (j.debug()) << "MISMATCH on TX " << tx <<
                    ": Different nodes!";
                JLOG (j.debug()) << " Built:" <<
                    " Nodes:\n" << builtNodes.getJson (0);
                JLOG (j.debug()) << " Valid:" <<
                    " Nodes:\n" << validNodes.getJson (0);
            }
        }
    }
    else if (validMetaData != nullptr)
    {
        JLOG (j.error()) << "MISMATCH on TX " << tx <<
            ": Metadata Difference (built has none)\n" <<
            validMetaData->getJson (0);
    }
    else // builtMetaData != nullptr
    {
        JLOG (j.error()) << "MISMATCH on TX " << tx <<
            ": Metadata Difference (valid has none)\n" <<
            builtMetaData->getJson (0);
    }
}

//------------------------------------------------------------------------------

// Return list of leaves sorted by key
static
std::vector<SHAMapItem const*>
leaves (SHAMap const& sm)
{
    std::vector<SHAMapItem const*> v;
    for (auto const& item : sm)
        v.push_back(&item);
    std::sort(v.begin(), v.end(),
        [](SHAMapItem const* lhs, SHAMapItem const* rhs)
                { return lhs->key() < rhs->key(); });
    return v;
}

void LedgerHistory::handleMismatch (
    LedgerHash const& built,
    LedgerHash const& valid,
    Json::Value const& consensus)
{
    assert (built != valid);
    ++mismatch_counter_;

    auto builtLedger = getLedgerByHash (built);
    auto validLedger = getLedgerByHash (valid);

    if (!builtLedger || !validLedger)
    {
        JLOG (j_.error()) << "MISMATCH cannot be analyzed:" <<
            " builtLedger: " << to_string (built) << " -> " << builtLedger <<
            " validLedger: " << to_string (valid) << " -> " << validLedger;
        return;
    }

    assert (builtLedger->info().seq == validLedger->info().seq);

    if (auto stream = j_.debug())
    {
        stream << "Built: " << getJson (*builtLedger);
        stream << "Valid: " << getJson (*validLedger);
        stream << "Consensus: " << consensus;
    }

    // Determine the mismatch reason, distinguishing Byzantine
    // failure from transaction processing difference

    // Disagreement over prior ledger indicates sync issue
    if (builtLedger->info().parentHash != validLedger->info().parentHash)
    {
        JLOG (j_.error()) << "MISMATCH on prior ledger";
        return;
    }

    // Disagreement over close time indicates Byzantine failure
    if (builtLedger->info().closeTime != validLedger->info().closeTime)
    {
        JLOG (j_.error()) << "MISMATCH on close time";
        return;
    }

    // Find differences between built and valid ledgers
    auto const builtTx = leaves(builtLedger->txMap());
    auto const validTx = leaves(validLedger->txMap());

    if (builtTx == validTx)
        JLOG (j_.error()) <<
            "MISMATCH with same " << builtTx.size() <<
            " transactions";
    else
        JLOG (j_.error()) << "MISMATCH with " <<
            builtTx.size() << " built and " <<
            validTx.size() << " valid transactions.";

    JLOG (j_.error()) << "built\n" << getJson(*builtLedger);
    JLOG (j_.error()) << "valid\n" << getJson(*validLedger);

    // Log all differences between built and valid ledgers
    auto b = builtTx.begin();
    auto v = validTx.begin();
    while(b != builtTx.end() && v != validTx.end())
    {
        if ((*b)->key() < (*v)->key())
        {
            log_one (*builtLedger, (*b)->key(), "valid", j_);
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
                    *builtLedger,
                    *validLedger, (*b)->key(), j_);
            }
            ++b;
            ++v;
        }
    }
    for (; b != builtTx.end(); ++b)
        log_one (*builtLedger, (*b)->key(), "valid", j_);
    for (; v != validTx.end(); ++v)
        log_one (*validLedger, (*v)->key(), "built", j_);
}

void LedgerHistory::builtLedger (
    std::shared_ptr<Ledger const> const& ledger,
    Json::Value consensus)
{
    LedgerIndex index = ledger->info().seq;
    LedgerHash hash = ledger->info().hash;
    assert (!hash.isZero());

    ConsensusValidated::ScopedLockType sl (
        m_consensus_validated.peekMutex());

    auto entry = std::make_shared<cv_entry>();
    m_consensus_validated.canonicalize(index, entry, false);

    if (entry->validated && ! entry->built)
    {
        if (entry->validated.get() != hash)
        {
            JLOG (j_.error()) << "MISMATCH: seq=" << index
                << " validated:" << entry->validated.get()
                << " then:" << hash;
            handleMismatch (hash, entry->validated.get(), consensus);
        }
        else
        {
            // We validated a ledger and then built it locally
            JLOG (j_.debug()) << "MATCH: seq=" << index << " late";
        }
    }

    entry->built.emplace (hash);
    entry->consensus.emplace (std::move (consensus));
}

void LedgerHistory::validatedLedger (
    std::shared_ptr<Ledger const> const& ledger)
{
    LedgerIndex index = ledger->info().seq;
    LedgerHash hash = ledger->info().hash;
    assert (!hash.isZero());

    ConsensusValidated::ScopedLockType sl (
        m_consensus_validated.peekMutex());

    auto entry = std::make_shared<cv_entry>();
    m_consensus_validated.canonicalize(index, entry, false);

    if (entry->built && ! entry->validated)
    {
        if (entry->built.get() != hash)
        {
            JLOG (j_.error()) << "MISMATCH: seq=" << index
                << " built:" << entry->built.get()
                << " then:" << hash;
            handleMismatch (entry->built.get(), hash, entry->consensus.get());
        }
        else
        {
            // We built a ledger locally and then validated it
            JLOG (j_.debug()) << "MATCH: seq=" << index;
        }
    }

    entry->validated.emplace (hash);
}

/** Ensure m_ledgers_by_hash doesn't have the wrong hash for a particular index
*/
bool LedgerHistory::fixIndex (
    LedgerIndex ledgerIndex, LedgerHash const& ledgerHash)
{
    LedgersByHash::ScopedLockType sl (m_ledgers_by_hash.peekMutex ());
    auto it = mLedgersByIndex.find (ledgerIndex);

    if ((it != mLedgersByIndex.end ()) && (it->second != ledgerHash) )
    {
        it->second = ledgerHash;
        return false;
    }
    return true;
}

void LedgerHistory::tune (int size, int age)
{
    m_ledgers_by_hash.setTargetSize (size);
    m_ledgers_by_hash.setTargetAge (age);
}

void LedgerHistory::clearLedgerCachePrior (LedgerIndex seq)
{
    for (LedgerHash it: m_ledgers_by_hash.getKeys())
    {
        auto const ledger = getLedgerByHash (it);
        assert(ledger);
        if (!ledger || ledger->info().seq < seq)
            m_ledgers_by_hash.del (it, false);
    }
}

} // ripple
