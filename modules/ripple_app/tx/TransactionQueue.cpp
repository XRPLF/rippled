//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

TXQueue::TXQueue ()
    : mLock (this, "TXQueue", __FILE__, __LINE__)
    , mRunning (false)
{
}

void TXQEntry::addCallbacks (const TXQEntry& otherEntry)
{
    BOOST_FOREACH (const stCallback & callback, otherEntry.mCallbacks)
    mCallbacks.push_back (callback);
}

void TXQEntry::doCallbacks (TER result)
{
    BOOST_FOREACH (const stCallback & callback, mCallbacks)
    callback (mTxn, result);
}

bool TXQueue::addEntryForSigCheck (TXQEntry::ref entry)
{
    // we always dispatch a thread to check the signature
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    if (!mTxMap.insert (valueType (entry->getID (), entry)).second)
    {
        if (!entry->mCallbacks.empty ())
            mTxMap.left.find (entry->getID ())->second->addCallbacks (*entry);

        return false;
    }

    return true;
}

bool TXQueue::addEntryForExecution (TXQEntry::ref entry)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    entry->mSigChecked = true;

    std::pair<mapType::iterator, bool> it = mTxMap.insert (valueType (entry->getID (), entry));

    if (!it.second)
    {
        // There was an existing entry
        it.first->right->mSigChecked = true;

        if (!entry->mCallbacks.empty ())
            it.first->right->addCallbacks (*entry);
    }

    if (mRunning)
        return false;

    mRunning = true;
    return true; // A thread needs to handle this account
}

TXQEntry::pointer TXQueue::removeEntry (uint256 const& id)
{
    TXQEntry::pointer ret;

    ScopedLockType sl (mLock, __FILE__, __LINE__);

    mapType::left_map::iterator it = mTxMap.left.find (id);

    if (it != mTxMap.left.end ())
    {
        ret = it->second;
        mTxMap.left.erase (it);
    }

    return ret;
}

void TXQueue::getJob (TXQEntry::pointer& job)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    assert (mRunning);

    if (job)
        mTxMap.left.erase (job->getID ());

    mapType::left_map::iterator it = mTxMap.left.begin ();

    if (it == mTxMap.left.end () || !it->second->mSigChecked)
    {
        job.reset ();
        mRunning = false;
    }
    else
        job = it->second;
}

bool TXQueue::stopProcessing (TXQEntry::ref finishedJob)
{
    // returns true if a new thread must be dispatched
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    assert (mRunning);

    mTxMap.left.erase (finishedJob->getID ());

    mapType::left_map::iterator it = mTxMap.left.begin ();

    if ((it != mTxMap.left.end ()) && it->second->mSigChecked)
        return true;

    mRunning = false;
    return false;
}
