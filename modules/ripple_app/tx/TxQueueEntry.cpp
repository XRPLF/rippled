//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

void TxQueueEntry::addCallbacks (const TxQueueEntry& otherEntry)
{
    BOOST_FOREACH (const stCallback & callback, otherEntry.mCallbacks)
    mCallbacks.push_back (callback);
}

void TxQueueEntry::doCallbacks (TER result)
{
    BOOST_FOREACH (const stCallback & callback, mCallbacks)
    callback (mTxn, result);
}

