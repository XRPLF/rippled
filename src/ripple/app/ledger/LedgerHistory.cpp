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

uint256 LedgerHistory::getLedgerHash (std::uint32_t index)
{
    LedgersByHash::ScopedLockType sl (m_ledgers_by_hash.peekMutex ());
    std::map<std::uint32_t, uint256>::iterator it (mLedgersByIndex.find (index));

    if (it != mLedgersByIndex.end ())
        return it->second;

    return uint256 ();
}

Ledger::pointer LedgerHistory::getLedgerBySeq (std::uint32_t index)
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

Ledger::pointer LedgerHistory::getLedgerByHash (uint256 const& hash)
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

static void addLeaf (std::vector <uint256> &vec, SHAMapItem::ref item)
{
    vec.push_back (item->getTag ());
}

void LedgerHistory::handleMismatch (LedgerHash const& built, LedgerHash  const& valid)
{
    assert (built != valid);
    ++mismatch_counter_;

    Ledger::pointer builtLedger = getLedgerByHash (built);
    Ledger::pointer validLedger = getLedgerByHash (valid);

    if (builtLedger && validLedger)
    {
        assert (builtLedger->getLedgerSeq() == validLedger->getLedgerSeq());

        // Determine the mismatch reason
        // Distinguish Byzantine failure from transaction processing difference

        if (builtLedger->getParentHash() != validLedger->getParentHash())
        {
            // Disagreement over prior ledger indicates sync issue
            WriteLog (lsERROR, LedgerMaster) << "MISMATCH on prior ledger";
        }
        else if (builtLedger->getCloseTimeNC() != validLedger->getCloseTimeNC())
        {
            // Disagreement over close time indicates Byzantine failure
            WriteLog (lsERROR, LedgerMaster) << "MISMATCH on close time";
        }
        else
        {
            std::vector <uint256> builtTx, validTx;
            builtLedger->peekTransactionMap()->visitLeaves(
                std::bind (&addLeaf, std::ref (builtTx), std::placeholders::_1));
            validLedger->peekTransactionMap()->visitLeaves(
                std::bind (&addLeaf, std::ref (validTx), std::placeholders::_1));
            std::sort (builtTx.begin(), builtTx.end());
            std::sort (validTx.begin(), validTx.end());

            if (builtTx == validTx)
            {
                // Disagreement with same prior ledger, close time, and transactions
                // indicates a transaction processing difference
                WriteLog (lsERROR, LedgerMaster) <<
                    "MISMATCH with same " << builtTx.size() << " tx";
            }
            else
            {
                std::vector <uint256> notBuilt, notValid;
                std::set_difference (
                    validTx.begin(), validTx.end(),
                    builtTx.begin(), builtTx.end(),
                    std::inserter (notBuilt, notBuilt.begin()));
                std::set_difference (
                    builtTx.begin(), builtTx.end(),
                    validTx.begin(), validTx.end(),
                    std::inserter (notValid, notValid.begin()));

                // This can be either a disagreement over the consensus
                // set or difference in which transactions were rejected
                // as invalid

                WriteLog (lsERROR, LedgerMaster) << "MISMATCH tx differ "
                    << builtTx.size() << " built, " << validTx.size() << " valid";
                for (auto const& t : notBuilt)
                {
                    WriteLog (lsERROR, LedgerMaster) << "MISMATCH built without " << t;
                }
                for (auto const& t : notValid)
                {
                    WriteLog (lsERROR, LedgerMaster) << "MISMATCH valid without " << t;
                }
            }
        }
    }
    else
        WriteLog (lsERROR, LedgerMaster) << "MISMATCH cannot be analyzed";
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
        bool mismatch (false);

        if (entry->first.isNonZero() && (entry->first != hash))
        {
            WriteLog (lsERROR, LedgerMaster) << "MISMATCH: seq=" << index << " built:" << entry->first << " then:" << hash;
            mismatch = true;
        }
        if (entry->second.isNonZero() && (entry->second != hash))
        {
            WriteLog (lsERROR, LedgerMaster) << "MISMATCH: seq=" << index << " validated:" << entry->second << " accepted:" << hash;
            mismatch = true;
        }

        if (mismatch)
            handleMismatch (hash, entry->first);

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
        bool mismatch (false);

        if (entry->second.isNonZero() && (entry->second != hash))
        {
            WriteLog (lsERROR, LedgerMaster) << "MISMATCH: seq=" << index << " validated:" << entry->second << " then:" << hash;
            mismatch = true;
        }

        if (entry->first.isNonZero() && (entry->first != hash))
        {
            WriteLog (lsERROR, LedgerMaster) << "MISMATCH: seq=" << index << " built:" << entry->first << " validated:" << hash;
            mismatch = true;
        }

        if (mismatch)
            handleMismatch (entry->second, hash);

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

} // ripple
