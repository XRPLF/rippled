//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_TXQUEUEENTRY_H_INCLUDED
#define RIPPLE_TXQUEUEENTRY_H_INCLUDED

// Allow transactions to be signature checked out of sequence but retired in sequence
class TxQueueEntry
{
public:
    typedef boost::shared_ptr<TxQueueEntry> pointer;
    typedef const boost::shared_ptr<TxQueueEntry>& ref;
    typedef FUNCTION_TYPE<void (Transaction::pointer, TER)> stCallback; // must complete immediately

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

#endif
