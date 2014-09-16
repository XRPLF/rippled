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

#ifndef RIPPLE_TXQUEUEENTRY_H_INCLUDED
#define RIPPLE_TXQUEUEENTRY_H_INCLUDED

namespace ripple {

// Allow transactions to be signature checked out of sequence but retired in sequence
class TxQueueEntry
{
public:
    typedef std::shared_ptr<TxQueueEntry> pointer;
    typedef const std::shared_ptr<TxQueueEntry>& ref;
    typedef std::function<void (Transaction::pointer, TER)> stCallback; // must complete immediately

public:
    TxQueueEntry (Transaction::ref tx, bool sigChecked) : mTxn (tx), mSigChecked (sigChecked)
    {
    }

    TxQueueEntry () : mSigChecked (false)
    {
    }

    Transaction::ref getTransaction () const
    {
        return mTxn;
    }

    bool getSigChecked () const
    {
        return mSigChecked;
    }

    uint256 const& getID () const
    {
        return mTxn->getID ();
    }

    void doCallbacks (TER);

private:
    friend class TxQueueImp;

    void addCallbacks (const TxQueueEntry& otherEntry);

    Transaction::pointer    mTxn;
    bool                    mSigChecked;
    std::list<stCallback>   mCallbacks;
};

} // ripple

#endif
