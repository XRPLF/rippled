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

LedgerHistory::LedgerHistory () : mLedgersByHash ("LedgerCache", CACHED_LEDGER_NUM, CACHED_LEDGER_AGE)
{
    ;
}

void LedgerHistory::addLedger (Ledger::pointer ledger)
{
    assert (ledger->isImmutable ());
    mLedgersByHash.canonicalize (ledger->getHash (), ledger, true);
}

void LedgerHistory::addValidatedLedger (Ledger::pointer ledger)
{
    assert (ledger && ledger->isValidated () && ledger->isImmutable ());
    assert (ledger->peekAccountStateMap ()->getHash ().isNonZero ());
    uint256 h (ledger->getHash ());
    boost::recursive_mutex::scoped_lock sl (mLedgersByHash.peekMutex ());
    mLedgersByHash.canonicalize (h, ledger);
    mLedgersByIndex[ledger->getLedgerSeq ()] = ledger->getHash ();

    ledger->pendSaveValidated ();
}

uint256 LedgerHistory::getLedgerHash (uint32 index)
{
    boost::recursive_mutex::scoped_lock sl (mLedgersByHash.peekMutex ());
    std::map<uint32, uint256>::iterator it (mLedgersByIndex.find (index));

    if (it != mLedgersByIndex.end ())
        return it->second;

    sl.unlock ();
    return uint256 ();
}

Ledger::pointer LedgerHistory::getLedgerBySeq (uint32 index)
{
    boost::recursive_mutex::scoped_lock sl (mLedgersByHash.peekMutex ());
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

    sl.lock ();
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
    boost::recursive_mutex::scoped_lock sl (mLedgersByHash.peekMutex ());
    mLedgersByHash.canonicalize (h, ledger);

    if (ledger->isValidated ())
        mLedgersByIndex[ledger->getLedgerSeq ()] = ledger->getHash ();

    return ledger;
}

void LedgerHistory::tune (int size, int age)
{
    mLedgersByHash.setTargetSize (size);
    mLedgersByHash.setTargetAge (age);
}

// vim:ts=4
