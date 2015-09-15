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
        stopwatch(), deprecatedLogs().journal("TaggedCache"))
    , m_consensus_validated ("ConsensusValidated", 64, 300,
        stopwatch(), deprecatedLogs().journal("TaggedCache"))
{
}

bool LedgerHistory::addLedger (Ledger::pointer ledger, bool validated)
{
    assert (ledger && ledger->isImmutable ());
    assert (ledger->stateMap().getHash ().isNonZero ());

    LedgersByHash::ScopedLockType sl (m_ledgers_by_hash.peekMutex ());

    const bool alreadyHad = m_ledgers_by_hash.canonicalize (
        ledger->getHash(), ledger, true);
    if (validated)
        mLedgersByIndex[ledger->info().seq] = ledger->getHash();

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

Ledger::pointer LedgerHistory::getLedgerBySeq (LedgerIndex index)
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

    Ledger::pointer ret = loadByIndex (index, app_);

    if (!ret)
        return ret;

    assert (ret->info().seq == index);

    {
        // Add this ledger to the local tracking by index
        LedgersByHash::ScopedLockType sl (m_ledgers_by_hash.peekMutex ());

        assert (ret->isImmutable ());
        m_ledgers_by_hash.canonicalize (ret->getHash (), ret);
        mLedgersByIndex[ret->info().seq] = ret->getHash ();
        return (ret->info().seq == index) ? ret : Ledger::pointer ();
    }
}

Ledger::pointer LedgerHistory::getLedgerByHash (LedgerHash const& hash)
{
    Ledger::pointer ret = m_ledgers_by_hash.fetch (hash);

    if (ret)
    {
        assert (ret->isImmutable ());
        assert (ret->getHash () == hash);
        return ret;
    }

    ret = loadByHash (hash, app_);

    if (!ret)
        return ret;

    assert (ret->isImmutable ());
    assert (ret->getHash () == hash);
    m_ledgers_by_hash.canonicalize (ret->getHash (), ret);
    assert (ret->getHash () == hash);

    return ret;
}

static
void
log_one(Ledger::pointer ledger, uint256 const& tx, char const* msg)
{
    auto metaData = ledger->txRead(tx).second;

    if (metaData != nullptr)
    {
        WriteLog (lsDEBUG, LedgerMaster) << "MISMATCH on TX " << tx <<
            ": " << msg << " is missing this transaction:\n" <<
            metaData->getJson (0);
    }
    else
    {
        WriteLog (lsDEBUG, LedgerMaster) << "MISMATCH on TX " << tx <<
            ": " << msg << " is missing this transaction.";
    }
}

static
void
log_metadata_difference(
    Ledger::pointer builtLedger, Ledger::pointer validLedger, uint256 const& tx)
{
    auto getMeta = [](Ledger const& ledger,
        uint256 const& txID) -> std::shared_ptr<TxMeta>
    {
        auto meta = ledger.txRead(txID).second;
        if (!meta)
            return {};
        return std::make_shared<TxMeta> (txID, ledger.seq(), *meta);
    };

    auto validMetaData = getMeta (*validLedger, tx);
    auto builtMetaData = getMeta (*builtLedger, tx);
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
            WriteLog (lsERROR, LedgerMaster) << "MISMATCH on TX " << tx <<
                ": No apparent mismatches detected!";
            return;
        }

        if (!nodes_diff)
        {
            if (result_diff && index_diff)
            {
                WriteLog (lsDEBUG, LedgerMaster) << "MISMATCH on TX " << tx <<
                    ": Different result and index!";
                WriteLog (lsDEBUG, LedgerMaster) << " Built:" <<
                    " Result: " << builtMetaData->getResult () <<
                    " Index: " << builtMetaData->getIndex ();
                WriteLog (lsDEBUG, LedgerMaster) << " Valid:" <<
                    " Result: " << validMetaData->getResult () <<
                    " Index: " << validMetaData->getIndex ();
            }
            else if (result_diff)
            {
                WriteLog (lsDEBUG, LedgerMaster) << "MISMATCH on TX " << tx <<
                    ": Different result!";
                WriteLog (lsDEBUG, LedgerMaster) << " Built:" <<
                    " Result: " << builtMetaData->getResult ();
                WriteLog (lsDEBUG, LedgerMaster) << " Valid:" <<
                    " Result: " << validMetaData->getResult ();
            }
            else if (index_diff)
            {
                WriteLog (lsDEBUG, LedgerMaster) << "MISMATCH on TX " << tx <<
                    ": Different index!";
                WriteLog (lsDEBUG, LedgerMaster) << " Built:" <<
                    " Index: " << builtMetaData->getIndex ();
                WriteLog (lsDEBUG, LedgerMaster) << " Valid:" <<
                    " Index: " << validMetaData->getIndex ();
            }
        }
        else
        {
            if (result_diff && index_diff)
            {
                WriteLog (lsDEBUG, LedgerMaster) << "MISMATCH on TX " << tx <<
                    ": Different result, index and nodes!";
                WriteLog (lsDEBUG, LedgerMaster) << " Built:\n" <<
                    builtMetaData->getJson (0);
                WriteLog (lsDEBUG, LedgerMaster) << " Valid:\n" <<
                    validMetaData->getJson (0);
            }
            else if (result_diff)
            {
                WriteLog (lsDEBUG, LedgerMaster) << "MISMATCH on TX " << tx <<
                    ": Different result and nodes!";
                WriteLog (lsDEBUG, LedgerMaster) << " Built:" <<
                    " Result: " << builtMetaData->getResult () <<
                    " Nodes:\n" << builtNodes.getJson (0);
                WriteLog (lsDEBUG, LedgerMaster) << " Valid:" <<
                    " Result: " << validMetaData->getResult () <<
                    " Nodes:\n" << validNodes.getJson (0);
            }
            else if (index_diff)
            {
                WriteLog (lsDEBUG, LedgerMaster) << "MISMATCH on TX " << tx <<
                    ": Different index and nodes!";
                WriteLog (lsDEBUG, LedgerMaster) << " Built:" <<
                    " Index: " << builtMetaData->getIndex () <<
                    " Nodes:\n" << builtNodes.getJson (0);
                WriteLog (lsDEBUG, LedgerMaster) << " Valid:" <<
                    " Index: " << validMetaData->getIndex () <<
                    " Nodes:\n" << validNodes.getJson (0);
            }
            else // nodes_diff
            {
                WriteLog (lsDEBUG, LedgerMaster) << "MISMATCH on TX " << tx <<
                    ": Different nodes!";
                WriteLog (lsDEBUG, LedgerMaster) << " Built:" <<
                    " Nodes:\n" << builtNodes.getJson (0);
                WriteLog (lsDEBUG, LedgerMaster) << " Valid:" <<
                    " Nodes:\n" << validNodes.getJson (0);
            }
        }
    }
    else if (validMetaData != nullptr)
    {
        WriteLog (lsERROR, LedgerMaster) << "MISMATCH on TX " << tx <<
            ": Metadata Difference (built has none)\n" <<
            validMetaData->getJson (0);
    }
    else // builtMetaData != nullptr
    {
        WriteLog (lsERROR, LedgerMaster) << "MISMATCH on TX " << tx <<
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
    LedgerHash const& built, LedgerHash const& valid)
{
    assert (built != valid);
    ++mismatch_counter_;

    Ledger::pointer builtLedger = getLedgerByHash (built);
    Ledger::pointer validLedger = getLedgerByHash (valid);

    if (!builtLedger || !validLedger)
    {
        WriteLog (lsERROR, LedgerMaster) << "MISMATCH cannot be analyzed:" <<
            " builtLedger: " << to_string (built) << " -> " << builtLedger <<
            " validLedger: " << to_string (valid) << " -> " << validLedger;
        return;
    }

    assert (builtLedger->info().seq == validLedger->info().seq);

    // Determine the mismatch reason
    // Distinguish Byzantine failure from transaction processing difference

    if (builtLedger->info().parentHash != validLedger->info().parentHash)
    {
        // Disagreement over prior ledger indicates sync issue
        WriteLog (lsERROR, LedgerMaster) << "MISMATCH on prior ledger";
        return;
    }

    if (builtLedger->info().closeTime != validLedger->info().closeTime)
    {
        // Disagreement over close time indicates Byzantine failure
        WriteLog (lsERROR, LedgerMaster) << "MISMATCH on close time";
        return;
    }

    // Find differences between built and valid ledgers
    auto const builtTx = leaves(builtLedger->txMap());
    auto const validTx = leaves(validLedger->txMap());
    if (builtTx == validTx)
        WriteLog (lsERROR, LedgerMaster) <<
            "MISMATCH with same " << builtTx.size() << " transactions";
    else
        WriteLog (lsERROR, LedgerMaster) << "MISMATCH with " <<
            builtTx.size() << " built and " <<
            validTx.size() << " valid transactions.";

    WriteLog(lsERROR, LedgerMaster) << "built\n" <<
        getJson(*builtLedger);
    WriteLog(lsERROR, LedgerMaster) << "valid\n" <<
        getJson(*validLedger);

    // Log all differences between built and valid ledgers
    auto b = builtTx.begin();
    auto v = validTx.begin();
    while(b != builtTx.end() && v != validTx.end())
    {
        if ((*b)->key() < (*v)->key())
        {
            log_one (builtLedger, (*b)->key(), "valid");
            ++b;
        }
        else if ((*b)->key() > (*v)->key())
        {
            log_one(validLedger, (*v)->key(), "built");
            ++v;
        }
        else
        {
            if ((*b)->peekData() != (*v)->peekData())
            {
                // Same transaction with different metadata
                log_metadata_difference(builtLedger, validLedger, (*b)->key());
            }
            ++b;
            ++v;
        }
    }
    for (; b != builtTx.end(); ++b)
        log_one (builtLedger, (*b)->key(), "valid");
    for (; v != validTx.end(); ++v)
        log_one (validLedger, (*v)->key(), "built");
}

void LedgerHistory::builtLedger (Ledger::ref ledger)
{
    LedgerIndex index = ledger->info().seq;
    LedgerHash hash = ledger->getHash();
    assert (!hash.isZero());
    ConsensusValidated::ScopedLockType sl (
        m_consensus_validated.peekMutex());

    auto entry = std::make_shared<std::pair< LedgerHash, LedgerHash >>();
    m_consensus_validated.canonicalize(index, entry, false);

    if (entry->first != hash)
    {
        if (entry->second.isNonZero() && (entry->second != hash))
        {
            WriteLog (lsERROR, LedgerMaster) << "MISMATCH: seq=" << index
                << " validated:" << entry->second
                << " then:" << hash;
            handleMismatch (hash, entry->first);
        }
        entry->first = hash;
    }
}

void LedgerHistory::validatedLedger (Ledger::ref ledger)
{
    LedgerIndex index = ledger->info().seq;
    LedgerHash hash = ledger->getHash();
    assert (!hash.isZero());
    ConsensusValidated::ScopedLockType sl (
        m_consensus_validated.peekMutex());

    auto entry = std::make_shared<std::pair<LedgerHash, LedgerHash>>();
    m_consensus_validated.canonicalize(index, entry, false);

    if (entry->second != hash)
    {
        if (entry->first.isNonZero() && (entry->first != hash))
        {
            WriteLog (lsERROR, LedgerMaster) << "MISMATCH: seq=" << index
                << " built:" << entry->first
                << " then:" << hash;
            handleMismatch (entry->first, hash);
        }

        entry->second = hash;
    }
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
        if (getLedgerByHash (it)->info().seq < seq)
            m_ledgers_by_hash.del (it, false);
    }
}

} // ripple
