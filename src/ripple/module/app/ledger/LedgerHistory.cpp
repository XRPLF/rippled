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

LedgerHistory::LedgerHistory ()
    : m_ledgers_by_hash ("LedgerCache", CACHED_LEDGER_NUM, CACHED_LEDGER_AGE,
        get_seconds_clock (), LogPartition::getJournal <TaggedCacheLog> ())
    , m_consensus_validated ("ConsensusValidated", 64, 300,
        get_seconds_clock (), LogPartition::getJournal <TaggedCacheLog> ())
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
        if (entry->first.isNonZero() && (entry->first != hash))
        {
            WriteLog (lsERROR, LedgerMaster) << "MISMATCH: seq=" << index << " built:" << entry->first << " then:" << hash;
        }
        if (entry->second.isNonZero() && (entry->second != hash))
        {
            WriteLog (lsERROR, LedgerMaster) << "MISMATCH: seq=" << index << " validated:" << entry->second << " accepted:" << hash;
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
        if (entry->second.isNonZero() && (entry->second != hash))
        {
            WriteLog (lsERROR, LedgerMaster) << "MISMATCH: seq=" << index << " validated:" << entry->second << " then:" << hash;
        }
        if (entry->first.isNonZero() && (entry->first != hash))
        {
            WriteLog (lsERROR, LedgerMaster) << "MISMATCH: seq=" << index << " built:" << entry->first << " validated:" << hash;
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

} // ripple
