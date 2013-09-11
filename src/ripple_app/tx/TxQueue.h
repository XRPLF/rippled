//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_TXQUEUE_H_INCLUDED
#define RIPPLE_TXQUEUE_H_INCLUDED

class TxQueue : LeakChecked <TxQueue>
{
public:
    static TxQueue* New ();

    virtual ~TxQueue () { }

    // Return: true = must dispatch signature checker thread
    virtual bool addEntryForSigCheck (TxQueueEntry::ref) = 0;

    // Call only if signature is okay. Returns true if new account, must dispatch
    virtual bool addEntryForExecution (TxQueueEntry::ref) = 0;

    // Call if signature is bad (returns entry so you can run its callbacks)
    virtual TxQueueEntry::pointer removeEntry (uint256 const& txID) = 0;

    // Transaction execution interface
    virtual void getJob (TxQueueEntry::pointer&) = 0;
    virtual bool stopProcessing (TxQueueEntry::ref finishedJob) = 0;
};

#endif
