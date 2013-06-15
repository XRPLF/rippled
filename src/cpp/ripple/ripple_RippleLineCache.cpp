
AccountItems& RippleLineCache::getRippleLines (const uint160& accountID)
{
    boost::mutex::scoped_lock sl (mLock);

    boost::unordered_map <uint160, AccountItems::pointer>::iterator it = mRLMap.find (accountID);

    if (it == mRLMap.end ())
        it = mRLMap.insert (std::make_pair (accountID, boost::make_shared<AccountItems>
                                            (boost::cref (accountID), boost::cref (mLedger), AccountItem::pointer (new RippleState ())))).first;

    return *it->second;
}
