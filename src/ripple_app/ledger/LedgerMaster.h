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

#ifndef RIPPLE_LEDGERMASTER_H_INCLUDED
#define RIPPLE_LEDGERMASTER_H_INCLUDED

// Tracks the current ledger and any ledgers in the process of closing
// Tracks ledger history
// Tracks held transactions

// VFALCO TODO Rename to Ledgers
//        It sounds like this holds all the ledgers...
//
class LedgerMaster
    : public Stoppable
{
protected:
    explicit LedgerMaster (Stoppable& parent);

public:
    typedef FUNCTION_TYPE <void (Ledger::ref)> callback;

public:
    typedef RippleRecursiveMutex LockType;
    typedef LockType::ScopedLockType ScopedLockType;
    typedef LockType::ScopedUnlockType ScopedUnlockType;

    static LedgerMaster* New (Stoppable& parent, Journal journal);

    virtual ~LedgerMaster () = 0;

    virtual uint32 getCurrentLedgerIndex () = 0;

    virtual LockType& peekMutex () = 0;

    // The current ledger is the ledger we believe new transactions should go in
    virtual Ledger::ref getCurrentLedger () = 0;

    // An immutable snapshot of the current ledger
    virtual Ledger::ref getCurrentSnapshot () = 0;

    // The finalized ledger is the last closed/accepted ledger
    virtual Ledger::ref getClosedLedger () = 0;

    // The validated ledger is the last fully validated ledger
    virtual Ledger::ref getValidatedLedger () = 0;

    // This is the last ledger we published to clients and can lag the validated ledger
    virtual Ledger::ref getPublishedLedger () = 0;

    virtual int getPublishedLedgerAge () = 0;
    virtual int getValidatedLedgerAge () = 0;
    virtual bool isCaughtUp(std::string& reason) = 0;

    virtual TER doTransaction (
        SerializedTransaction::ref txn,
            TransactionEngineParams params, bool& didApply) = 0;

    virtual int getMinValidations () = 0;

    virtual void setMinValidations (int v) = 0;

    virtual void pushLedger (Ledger::pointer newLedger) = 0;
    virtual void pushLedger (Ledger::pointer newLCL, Ledger::pointer newOL) = 0;
    virtual void storeLedger (Ledger::pointer) = 0;
    virtual void forceValid (Ledger::pointer) = 0;

    virtual void setFullLedger (Ledger::pointer ledger, bool isSynchronous, bool isCurrent) = 0;

    virtual void switchLedgers (Ledger::pointer lastClosed, Ledger::pointer newCurrent) = 0;

    virtual void failedSave(uint32 seq, uint256 const& hash) = 0;

    virtual std::string getCompleteLedgers () = 0;

    virtual Ledger::pointer closeLedger (bool recoverHeldTransactions) = 0;

    /** Get a ledger's hash by sequence number using the cache
    */
    virtual uint256 getHashBySeq (uint32 index) = 0;

    /** Walk to a ledger's hash using the skip list
    */
    virtual uint256 walkHashBySeq (uint32 index) = 0;
    virtual uint256 walkHashBySeq (uint32 index, Ledger::ref referenceLedger) = 0;

    virtual Ledger::pointer findAcquireLedger (uint32 index, uint256 const& hash) = 0;

    virtual Ledger::pointer getLedgerBySeq (uint32 index) = 0;

    virtual Ledger::pointer getLedgerByHash (uint256 const& hash) = 0;

    virtual void setLedgerRangePresent (uint32 minV, uint32 maxV) = 0;

    virtual uint256 getLedgerHash(uint32 desiredSeq, Ledger::ref knownGoodLedger) = 0;

    virtual void addHeldTransaction (Transaction::ref trans) = 0;
    virtual void fixMismatch (Ledger::ref ledger) = 0;

    virtual bool haveLedgerRange (uint32 from, uint32 to) = 0;
    virtual bool haveLedger (uint32 seq) = 0;
    virtual void clearLedger (uint32 seq) = 0;
    virtual bool getValidatedRange (uint32& minVal, uint32& maxVal) = 0;
    virtual bool getFullValidatedRange (uint32& minVal, uint32& maxVal) = 0;

    virtual void tune (int size, int age) = 0;
    virtual void sweep () = 0;
    virtual float getCacheHitRate () = 0;
    virtual void addValidateCallback (callback& c) = 0;

    virtual void checkAccept (Ledger::ref ledger) = 0;
    virtual void checkAccept (uint256 const& hash) = 0;

    virtual void tryAdvance () = 0;
    virtual void newPathRequest () = 0;
    virtual bool isNewPathRequest () = 0;
    virtual void newOrderBookDB () = 0;

    virtual bool fixIndex (LedgerIndex ledgerIndex, LedgerHash const& ledgerHash) = 0;
    virtual void doLedgerCleaner(const Json::Value& parameters) = 0;

    virtual PropertyStream::Source& getPropertySource () = 0;

    static bool shouldAcquire (uint32 currentLedgerID, uint32 ledgerHistory, uint32 targetLedger);
};

#endif
