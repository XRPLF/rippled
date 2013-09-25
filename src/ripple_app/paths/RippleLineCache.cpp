//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

RippleLineCache::RippleLineCache (Ledger::ref l)
    : mLock (this, "RippleLineCache", __FILE__, __LINE__)
    , mLedger (l)
{
}

AccountItems& RippleLineCache::getRippleLines (const uint160& accountID)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    boost::unordered_map <uint160, AccountItems::pointer>::iterator it = mRLMap.find (accountID);

    if (it == mRLMap.end ())
        it = mRLMap.insert (std::make_pair (accountID, boost::make_shared<AccountItems>
                                            (boost::cref (accountID), boost::cref (mLedger), AccountItem::pointer (new RippleState ())))).first;

    return *it->second;
}
