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

#include <ripple/module/app/tx/TxQueue.h>
#include <boost/bimap.hpp>
#include <boost/bimap/unordered_set_of.hpp>
#include <boost/bimap/list_of.hpp>

namespace ripple {

class TxQueueImp
    : public TxQueue
    , public beast::LeakChecked <TxQueueImp>
{
public:
    TxQueueImp ()
        : mRunning (false)
    {
    }

    bool addEntryForSigCheck (TxQueueEntry::ref entry)
    {
        // we always dispatch a thread to check the signature
        ScopedLockType sl (mLock);

        if (!mTxMap.insert (valueType (entry->getID (), entry)).second)
        {
            if (!entry->mCallbacks.empty ())
                mTxMap.left.find (entry->getID ())->second->addCallbacks (*entry);

            return false;
        }

        return true;
    }

    bool addEntryForExecution (TxQueueEntry::ref entry)
    {
        ScopedLockType sl (mLock);

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

    TxQueueEntry::pointer removeEntry (uint256 const& id)
    {
        TxQueueEntry::pointer ret;

        ScopedLockType sl (mLock);

        mapType::left_map::iterator it = mTxMap.left.find (id);

        if (it != mTxMap.left.end ())
        {
            ret = it->second;
            mTxMap.left.erase (it);
        }

        return ret;
    }

    void getJob (TxQueueEntry::pointer& job)
    {
        ScopedLockType sl (mLock);
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

    bool stopProcessing (TxQueueEntry::ref finishedJob)
    {
        // returns true if a new thread must be dispatched
        ScopedLockType sl (mLock);
        assert (mRunning);

        mTxMap.left.erase (finishedJob->getID ());

        mapType::left_map::iterator it = mTxMap.left.begin ();

        if ((it != mTxMap.left.end ()) && it->second->mSigChecked)
            return true;

        mRunning = false;
        return false;
    }

private:
    typedef boost::bimaps::unordered_set_of<uint256>    leftType;
    typedef boost::bimaps::list_of<TxQueueEntry::pointer>   rightType;
    typedef boost::bimap<leftType, rightType>           mapType;
    typedef mapType::value_type                         valueType;

    typedef RippleMutex LockType;
    typedef std::lock_guard <LockType> ScopedLockType;
    LockType mLock;

    mapType         mTxMap;
    bool            mRunning;
};

//------------------------------------------------------------------------------

TxQueue* TxQueue::New ()
{
    return new TxQueueImp;
}

} // ripple
