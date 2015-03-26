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
#include <ripple/basics/Log.h>
#include <ripple/basics/seconds_clock.h>
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
    beast::insight::Collector::ptr const& collector)
    : collector_ (collector)
    , mismatch_counter_ (collector->make_counter ("ledger.history", "mismatch"))
    , m_ledgers_by_hash ("LedgerCache", CACHED_LEDGER_NUM, CACHED_LEDGER_AGE,
        get_seconds_clock (), deprecatedLogs().journal("TaggedCache"))
    , m_consensus_validated ("ConsensusValidated", 64, 300,
        get_seconds_clock (), deprecatedLogs().journal("TaggedCache"))
{
}

bool LedgerHistory::addLedger (Ledger::pointer ledger, bool validated)
{
    assert (ledger && ledger->isImmutable ());
    assert (ledger->peekAccountStateMap ()->getHash ().isNonZero ());

    LedgersByHash::ScopedLockType sl (m_ledgers_by_hash.peekMutex ());

    const bool alreadyHad = m_ledgers_by_hash.canonicalize (ledger->getHash(), ledger, true);
    if (validated)
        mLedgersByIndex[ledger->getLedgerSeq()] = ledger->getHash();

    return alreadyHad;
}

LedgerHash LedgerHistory::getLedgerHash (LedgerIndex index)
{
    LedgersByHash::ScopedLockType sl (m_ledgers_by_hash.peekMutex ());
    std::map<std::uint32_t, uint256>::iterator it (mLedgersByIndex.find (index));

    if (it != mLedgersByIndex.end ())
        return it->second;

    return uint256 ();
}

Ledger::pointer LedgerHistory::getLedgerBySeq (LedgerIndex index)
{
    {
        LedgersByHash::ScopedLockType sl (m_ledgers_by_hash.peekMutex ());
        std::map <std::uint32_t, uint256>::iterator it (mLedgersByIndex.find (index));

        if (it != mLedgersByIndex.end ())
        {
            uint256 hash = it->second;
            sl.unlock ();
            return getLedgerByHash (hash);
        }
    }

    Ledger::pointer ret (Ledger::loadByIndex (index));

    if (!ret)
        return ret;

    assert (ret->getLedgerSeq () == index);

    {
        // Add this ledger to the local tracking by index
        LedgersByHash::ScopedLockType sl (m_ledgers_by_hash.peekMutex ());

        assert (ret->isImmutable ());
        m_ledgers_by_hash.canonicalize (ret->getHash (), ret);
        mLedgersByIndex[ret->getLedgerSeq ()] = ret->getHash ();
        return (ret->getLedgerSeq () == index) ? ret : Ledger::pointer ();
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

    ret = Ledger::loadByHash (hash);

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
    TransactionMetaSet::pointer metaData;
    ledger->getTransactionMeta(tx, metaData);

    if (metaData != nullptr)
    {
        WriteLog (lsERROR, LedgerMaster) << "MISMATCH on TX " << tx <<
            ": " << msg << " is missing this transaction:\n" <<
            metaData->getJson (0);
    }
    else
    {
        WriteLog (lsERROR, LedgerMaster) << "MISMATCH on TX " << tx <<
            ": " << msg << " is missing this transaction.";
    }
}

static
void
log_metadata_difference(Ledger::pointer builtLedger, Ledger::pointer validLedger,
                        uint256 const& tx)
{
    TransactionMetaSet::pointer validMetaData;
    validLedger->getTransactionMeta(tx, validMetaData);
    TransactionMetaSet::pointer builtMetaData;
    builtLedger->getTransactionMeta(tx, builtMetaData);
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
                WriteLog (lsERROR, LedgerMaster) << "MISMATCH on TX " << tx <<
                    ": Different result and index!";
                WriteLog (lsERROR, LedgerMaster) << " Built:" <<
                    " Result: " << builtMetaData->getResult () <<
                    " Index: " << builtMetaData->getIndex ();
                WriteLog (lsERROR, LedgerMaster) << " Valid:" <<
                    " Result: " << validMetaData->getResult () <<
                    " Index: " << validMetaData->getIndex ();
            }
            else if (result_diff)
            {
                WriteLog (lsERROR, LedgerMaster) << "MISMATCH on TX " << tx <<
                    ": Different result!";
                WriteLog (lsERROR, LedgerMaster) << " Built:" <<
                    " Result: " << builtMetaData->getResult ();
                WriteLog (lsERROR, LedgerMaster) << " Valid:" <<
                    " Result: " << validMetaData->getResult ();
            }
            else if (index_diff)
            {
                WriteLog (lsERROR, LedgerMaster) << "MISMATCH on TX " << tx <<
                    ": Different index!";
                WriteLog (lsERROR, LedgerMaster) << " Built:" <<
                    " Index: " << builtMetaData->getIndex ();
                WriteLog (lsERROR, LedgerMaster) << " Valid:" <<
                    " Index: " << validMetaData->getIndex ();
            }
        }
        else
        {
            if (result_diff && index_diff)
            {
                WriteLog (lsERROR, LedgerMaster) << "MISMATCH on TX " << tx <<
                    ": Different result, index and nodes!";
                WriteLog (lsERROR, LedgerMaster) << " Built:\n" <<
                    builtMetaData->getJson (0);
                WriteLog (lsERROR, LedgerMaster) << " Valid:\n" <<
                    validMetaData->getJson (0);
            }
            else if (result_diff)
            {
                WriteLog (lsERROR, LedgerMaster) << "MISMATCH on TX " << tx <<
                    ": Different result and nodes!";
                WriteLog (lsERROR, LedgerMaster) << " Built:" <<
                    " Result: " << builtMetaData->getResult () <<
                    " Nodes:\n" << builtNodes.getJson (0);
                WriteLog (lsERROR, LedgerMaster) << " Valid:" <<
                    " Result: " << validMetaData->getResult () <<
                    " Nodes:\n" << validNodes.getJson (0);
            }
            else if (index_diff)
            {
                WriteLog (lsERROR, LedgerMaster) << "MISMATCH on TX " << tx <<
                    ": Different index and nodes!";
                WriteLog (lsERROR, LedgerMaster) << " Built:" <<
                    " Index: " << builtMetaData->getIndex () <<
                    " Nodes:\n" << builtNodes.getJson (0);
                WriteLog (lsERROR, LedgerMaster) << " Valid:" <<
                    " Index: " << validMetaData->getIndex () <<
                    " Nodes:\n" << validNodes.getJson (0);
            }
            else // nodes_diff
            {
                WriteLog (lsERROR, LedgerMaster) << "MISMATCH on TX " << tx <<
                    ": Different nodes!";
                WriteLog (lsERROR, LedgerMaster) << " Built:" <<
                    " Nodes:\n" << builtNodes.getJson (0);
                WriteLog (lsERROR, LedgerMaster) << " Valid:" <<
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

void LedgerHistory::handleMismatch (LedgerHash const& built, LedgerHash const& valid)
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

    assert (builtLedger->getLedgerSeq() == validLedger->getLedgerSeq());

    // Determine the mismatch reason
    // Distinguish Byzantine failure from transaction processing difference

    if (builtLedger->getParentHash() != validLedger->getParentHash())
    {
        // Disagreement over prior ledger indicates sync issue
        WriteLog (lsERROR, LedgerMaster) << "MISMATCH on prior ledger";
        return;
    }

    if (builtLedger->getCloseTimeNC() != validLedger->getCloseTimeNC())
    {
        // Disagreement over close time indicates Byzantine failure
        WriteLog (lsERROR, LedgerMaster) << "MISMATCH on close time";
        return;
    }

    // Find differences between built and valid ledgers
    using SHAMapItemInfo = std::pair<uint256, Blob>;
    std::vector <SHAMapItemInfo> builtTx, validTx;
    // Get built ledger hashes and metadata
    builtLedger->peekTransactionMap()->visitLeaves(
        [&builtTx](std::shared_ptr<SHAMapItem> const& item)
        {
            builtTx.push_back({item->getTag(), item->peekData()});
        });
    // Get valid ledger hashes and metadata
    validLedger->peekTransactionMap()->visitLeaves(
        [&validTx](std::shared_ptr<SHAMapItem> const& item)
        {
            validTx.push_back({item->getTag(), item->peekData()});
        });

    // Sort both by hash
    std::sort (builtTx.begin(), builtTx.end(),
                    [](SHAMapItemInfo const& x, SHAMapItemInfo const& y)
                        {return x.first < y.first;});
    std::sort (validTx.begin(), validTx.end(),
                    [](SHAMapItemInfo const& x, SHAMapItemInfo const& y)
                        {return x.first < y.first;});

    if (builtTx == validTx)
    {
        WriteLog (lsERROR, LedgerMaster) <<
            "MISMATCH with same " << builtTx.size() << " transactions";
        return;
    }

    WriteLog (lsERROR, LedgerMaster) << "MISMATCH with " <<
        builtTx.size() << " built and " <<
        validTx.size() << " valid transactions.";

    // Log all differences between built and valid ledgers
    auto b = builtTx.cbegin();
    auto be = builtTx.cend();
    auto v = validTx.cbegin();
    auto ve = validTx.cend();
    while (b != be && v != ve)
    {
        if (b->first < v->first)
        {
            // b->first in built but not in valid
            log_one(builtLedger, b->first, "valid");
            ++b;
        }
        else if (v->first < b->first)
        {
            // v->first in valid but not in built
            log_one(validLedger, v->first, "built");
            ++v;
        }
        else  // b->first == v->first, same transaction
        {
            if (b->second != v->second)
            {
                // Same transaction with different metadata
                log_metadata_difference(builtLedger, validLedger, b->first);
            }
            ++b;
            ++v;
        }
    }
    // all of these are in built but not in valid
    for (; b != be; ++b)
        log_one(builtLedger, b->first, "valid");
    // all of these are in valid but not in built
    for (; v != ve; ++v)
        log_one(validLedger, v->first, "built");
}

void LedgerHistory::builtLedger (Ledger::ref ledger)
{
    LedgerIndex index = ledger->getLedgerSeq();
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
    LedgerIndex index = ledger->getLedgerSeq();
    LedgerHash hash = ledger->getHash();
    assert (!hash.isZero());
    ConsensusValidated::ScopedLockType sl (
        m_consensus_validated.peekMutex());

    std::shared_ptr< std::pair< LedgerHash, LedgerHash > > entry = std::make_shared<std::pair< LedgerHash, LedgerHash >>();
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
bool LedgerHistory::fixIndex (LedgerIndex ledgerIndex, LedgerHash const& ledgerHash)
{
    LedgersByHash::ScopedLockType sl (m_ledgers_by_hash.peekMutex ());
    std::map<std::uint32_t, uint256>::iterator it (mLedgersByIndex.find (ledgerIndex));

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
        if (getLedgerByHash (it)->getLedgerSeq() < seq)
            m_ledgers_by_hash.del (it, false);
    }
}

} // ripple
