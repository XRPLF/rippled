//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// VFALCO TODO replace macros

#ifndef CACHED_LEDGER_NUM
#define CACHED_LEDGER_NUM 96
#endif

#ifndef CACHED_LEDGER_AGE
#define CACHED_LEDGER_AGE 120
#endif

// FIXME: Need to clean up ledgers by index at some point

LedgerHistory::LedgerHistory ()
	: mLedgersByHash ("LedgerCache", CACHED_LEDGER_NUM, CACHED_LEDGER_AGE)
	, mConsensusValidated ("ConsensusValidated", 64, 300)
{
    ;
}

void LedgerHistory::addLedger (Ledger::pointer ledger)
{
    assert (ledger && ledger->isImmutable ());
    assert (ledger->peekAccountStateMap ()->getHash ().isNonZero ());

    TaggedCache::ScopedLockType sl (mLedgersByHash.peekMutex (), __FILE__, __LINE__);

    mLedgersByHash.canonicalize (ledger->getHash(), ledger, true);
    if (ledger->isValidated())
        mLedgersByIndex[ledger->getLedgerSeq()] = ledger->getHash();
}

uint256 LedgerHistory::getLedgerHash (uint32 index)
{
    TaggedCache::ScopedLockType sl (mLedgersByHash.peekMutex (), __FILE__, __LINE__);
    std::map<uint32, uint256>::iterator it (mLedgersByIndex.find (index));

    if (it != mLedgersByIndex.end ())
        return it->second;

    sl.unlock ();
    return uint256 ();
}

Ledger::pointer LedgerHistory::getLedgerBySeq (uint32 index)
{
    TaggedCache::ScopedLockType sl (mLedgersByHash.peekMutex (), __FILE__, __LINE__);
    std::map<uint32, uint256>::iterator it (mLedgersByIndex.find (index));

    if (it != mLedgersByIndex.end ())
    {
        uint256 hash = it->second;
        sl.unlock ();
        return getLedgerByHash (hash);
    }

    sl.unlock ();

    Ledger::pointer ret (Ledger::loadByIndex (index));

    if (!ret)
        return ret;

    assert (ret->getLedgerSeq () == index);

    sl.lock (__FILE__, __LINE__);
    assert (ret->isImmutable ());
    mLedgersByHash.canonicalize (ret->getHash (), ret);
    mLedgersByIndex[ret->getLedgerSeq ()] = ret->getHash ();
    return (ret->getLedgerSeq () == index) ? ret : Ledger::pointer ();
}

Ledger::pointer LedgerHistory::getLedgerByHash (uint256 const& hash)
{
    Ledger::pointer ret = mLedgersByHash.fetch (hash);

    if (ret)
    {
        assert (ret->isImmutable ());
        assert (ret->getHash () == hash); // FIXME: We seem to be getting these
        return ret;
    }

    ret = Ledger::loadByHash (hash);

    if (!ret)
        return ret;

    assert (ret->isImmutable ());
    assert (ret->getHash () == hash);
    mLedgersByHash.canonicalize (ret->getHash (), ret);
    assert (ret->getHash () == hash);

    return ret;
}

Ledger::pointer LedgerHistory::canonicalizeLedger (Ledger::pointer ledger, bool save)
{
    assert (ledger->isImmutable ());
    uint256 h (ledger->getHash ());

    if (!save)
    {
        // return input ledger if not in map, otherwise, return corresponding map ledger
        Ledger::pointer ret = mLedgersByHash.fetch (h);

        if (ret)
            return ret;

        return ledger;
    }

    // save input ledger in map if not in map, otherwise return corresponding map ledger
    TaggedCache::ScopedLockType sl (mLedgersByHash.peekMutex (), __FILE__, __LINE__);
    mLedgersByHash.canonicalize (h, ledger);

    if (ledger->isValidated ())
        mLedgersByIndex[ledger->getLedgerSeq ()] = ledger->getHash ();

    return ledger;
}

void LedgerHistory::builtLedger (Ledger::ref ledger)
{
    LedgerIndex index = ledger->getLedgerSeq();
    LedgerHash hash = ledger->getHash();
    assert (!hash.isZero());
    TaggedCache::ScopedLockType sl(mConsensusValidated.peekMutex(), __FILE__, __LINE__);

    boost::shared_ptr< std::pair< LedgerHash, LedgerHash > > entry = boost::make_shared<std::pair< LedgerHash, LedgerHash >>();
    mConsensusValidated.canonicalize(index, entry, false);

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
    TaggedCache::ScopedLockType sl(mConsensusValidated.peekMutex(), __FILE__, __LINE__);

    boost::shared_ptr< std::pair< LedgerHash, LedgerHash > > entry = boost::make_shared<std::pair< LedgerHash, LedgerHash >>();
    mConsensusValidated.canonicalize(index, entry, false);

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

void LedgerHistory::tune (int size, int age)
{
    mLedgersByHash.setTargetSize (size);
    mLedgersByHash.setTargetAge (age);
}

// vim:ts=4
