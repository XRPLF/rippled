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

#ifndef RIPPLE_APP_LEDGER_LEDGER_H_INCLUDED
#define RIPPLE_APP_LEDGER_LEDGER_H_INCLUDED

#include <ripple/shamap/SHAMap.h>
#include <ripple/app/tx/Transaction.h>
#include <ripple/app/tx/TransactionMeta.h>
#include <ripple/app/misc/AccountState.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <ripple/basics/CountedObject.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/Book.h>
#include <set>

namespace ripple {

class Job;

enum LedgerStateParms
{
    lepNONE         = 0,    // no special flags

    // input flags
    lepCREATE       = 1,    // Create if not present

    // output flags
    lepOKAY         = 2,    // success
    lepMISSING      = 4,    // No node in that slot
    lepWRONGTYPE    = 8,    // Node of different type there
    lepCREATED      = 16,   // Node was created
    lepERROR        = 32,   // error
};

class SqliteStatement;

// VFALCO TODO figure out exactly how this thing works.
//         It seems like some ledger database is stored as a global, static in the
//         class. But then what is the meaning of a Ledger object? Is this
//         really two classes in one? StoreOfAllLedgers + SingleLedgerObject?
//
/** Holds some or all of a ledger.
    This can hold just the header, a partial set of data, or the entire set
    of data. It all depends on what is in the corresponding SHAMap entry.
    Various functions are provided to populate or depopulate the caches that
    the object holds references to.

    Ledgers are constructed as either mutable or immutable.

    1) If you are the sole owner of a mutable ledger, you can do whatever you
    want with no need for locks.

    2) If you have an immutable ledger, you cannot ever change it, so no need
    for locks.

    3) Mutable ledgers cannot be shared.
*/
class Ledger
    : public std::enable_shared_from_this <Ledger>
    , public CountedObject <Ledger>
{
public:
    static char const* getCountedObjectName () { return "Ledger"; }

    typedef std::shared_ptr<Ledger>           pointer;
    typedef const std::shared_ptr<Ledger>&    ref;

    enum TransResult
    {
        TR_ERROR    = -1,
        TR_SUCCESS  = 0,
        TR_NOTFOUND = 1,
        TR_ALREADY  = 2,

        // the transaction itself is corrupt
        TR_BADTRANS = 3,

        // one of the accounts is invalid
        TR_BADACCT  = 4,

        // the sending(apply)/receiving(remove) account is broke
        TR_INSUFF   = 5,

        // account is past this transaction
        TR_PASTASEQ = 6,

        // account is missing transactions before this
        TR_PREASEQ  = 7,

        // ledger too early
        TR_BADLSEQ  = 8,

        // amount is less than Tx fee
        TR_TOOSMALL = 9,
    };

    // ledger close flags
    static const std::uint32_t sLCF_NoConsensusTime = 1;

public:

    // used for the starting bootstrap ledger
    Ledger (const RippleAddress & masterID, std::uint64_t startAmount);

    Ledger (uint256 const& parentHash, uint256 const& transHash,
            uint256 const& accountHash,
            std::uint64_t totCoins, std::uint32_t closeTime,
            std::uint32_t parentCloseTime, int closeFlags, int closeResolution,
            std::uint32_t ledgerSeq, bool & loaded);
    // used for database ledgers

    Ledger (std::uint32_t ledgerSeq, std::uint32_t closeTime);
    Ledger (Blob const & rawLedger, bool hasPrefix);
    Ledger (std::string const& rawLedger, bool hasPrefix);
    Ledger (bool dummy, Ledger & previous); // ledger after this one
    Ledger (Ledger & target, bool isMutable); // snapshot

    Ledger (Ledger const&) = delete;
    Ledger& operator= (Ledger const&) = delete;

    ~Ledger ();

    static Ledger::pointer getLastFullLedger ();
    static std::uint32_t roundCloseTime (
        std::uint32_t closeTime, std::uint32_t closeResolution);

    void updateHash ();
    void setClosed ()
    {
        mClosed = true;
    }
    void setValidated()
    {
        mValidated = true;
    }
    void setAccepted (
        std::uint32_t closeTime, int closeResolution, bool correctCloseTime);

    void setAccepted ();
    void setImmutable ();
    bool isClosed () const
    {
        return mClosed;
    }
    bool isAccepted () const
    {
        return mAccepted;
    }
    bool isValidated () const
    {
        return mValidated;
    }
    bool isImmutable () const
    {
        return mImmutable;
    }
    bool isFixed () const
    {
        return mClosed || mImmutable;
    }
    void setFull ()
    {
        mTransactionMap->setLedgerSeq (mLedgerSeq);
        mAccountStateMap->setLedgerSeq (mLedgerSeq);
    }

    // ledger signature operations
    void addRaw (Serializer & s) const;
    void setRaw (Serializer & s, bool hasPrefix);

    uint256 const& getHash ();
    uint256 const& getParentHash () const
    {
        return mParentHash;
    }
    uint256 const& getTransHash () const
    {
        return mTransHash;
    }
    uint256 const& getAccountHash () const
    {
        return mAccountHash;
    }
    std::uint64_t getTotalCoins () const
    {
        return mTotCoins;
    }
    void destroyCoins (std::uint64_t fee)
    {
        mTotCoins -= fee;
    }
    void setTotalCoins (std::uint64_t totCoins)
    {
        mTotCoins = totCoins;
    }
    std::uint32_t getCloseTimeNC () const
    {
        return mCloseTime;
    }
    std::uint32_t getParentCloseTimeNC () const
    {
        return mParentCloseTime;
    }
    std::uint32_t getLedgerSeq () const
    {
        return mLedgerSeq;
    }
    int getCloseResolution () const
    {
        return mCloseResolution;
    }
    bool getCloseAgree () const
    {
        return (mCloseFlags & sLCF_NoConsensusTime) == 0;
    }

    // close time functions
    void setCloseTime (std::uint32_t ct)
    {
        assert (!mImmutable);
        mCloseTime = ct;
    }
    void setCloseTime (boost::posix_time::ptime);
    boost::posix_time::ptime getCloseTime () const;

    // low level functions
    std::shared_ptr<SHAMap> const& peekTransactionMap () const
    {
        return mTransactionMap;
    }
    std::shared_ptr<SHAMap> const& peekAccountStateMap () const
    {
        return mAccountStateMap;
    }

    // returns false on error
    bool addSLE (SLE const& sle);

    // ledger sync functions
    void setAcquiring (void);
    bool isAcquiring (void) const;
    bool isAcquiringTx (void) const;
    bool isAcquiringAS (void) const;

    // Transaction Functions
    bool addTransaction (uint256 const& id, Serializer const& txn);
    bool addTransaction (
        uint256 const& id, Serializer const& txn, Serializer const& metaData);
    bool hasTransaction (uint256 const& TransID) const
    {
        return mTransactionMap->hasItem (TransID);
    }
    Transaction::pointer getTransaction (uint256 const& transID) const;
    bool getTransaction (
        uint256 const& transID,
        Transaction::pointer & txn, TransactionMetaSet::pointer & txMeta) const;
    bool getTransactionMeta (
        uint256 const& transID, TransactionMetaSet::pointer & txMeta) const;
    bool getMetaHex (uint256 const& transID, std::string & hex) const;

    static STTx::pointer getSTransaction (
        std::shared_ptr<SHAMapItem> const&, SHAMapTreeNode::TNType);
    STTx::pointer getSMTransaction (
        std::shared_ptr<SHAMapItem> const&, SHAMapTreeNode::TNType,
        TransactionMetaSet::pointer & txMeta) const;

    // high-level functions
    bool hasAccount (const RippleAddress & acctID) const;
    AccountState::pointer getAccountState (const RippleAddress & acctID) const;
    LedgerStateParms writeBack (LedgerStateParms parms, SLE::ref);
    SLE::pointer getAccountRoot (Account const& accountID) const;
    SLE::pointer getAccountRoot (const RippleAddress & naAccountID) const;
    void updateSkipList ();

    void visitAccountItems (
        Account const& accountID, std::function<void (SLE::ref)>) const;
    bool visitAccountItems (
        Account const& accountID,
        uint256 const& startAfter, // Entry to start after
        std::uint64_t const hint,  // Hint which page to start at
        unsigned int limit,
        std::function <bool (SLE::ref)>) const;
    void visitStateItems (std::function<void (SLE::ref)>) const;

    // database functions (low-level)
    static Ledger::pointer loadByIndex (std::uint32_t ledgerIndex);
    static Ledger::pointer loadByHash (uint256 const& ledgerHash);
    static uint256 getHashByIndex (std::uint32_t index);
    static bool getHashesByIndex (
        std::uint32_t index, uint256 & ledgerHash, uint256 & parentHash);
    static std::map< std::uint32_t, std::pair<uint256, uint256> >
                  getHashesByIndex (std::uint32_t minSeq, std::uint32_t maxSeq);
    bool pendSaveValidated (bool isSynchronous, bool isCurrent);

    // next/prev function
    SLE::pointer getSLE (uint256 const& uHash) const; // SLE is mutable
    SLE::pointer getSLEi (uint256 const& uHash) const; // SLE is immutable

    // VFALCO NOTE These seem to let you walk the list of ledgers
    //
    uint256 getFirstLedgerIndex () const;
    uint256 getLastLedgerIndex () const;

    // first node >hash
    uint256 getNextLedgerIndex (uint256 const& uHash) const;

    // first node >hash, <end
    uint256 getNextLedgerIndex (uint256 const& uHash, uint256 const& uEnd) const;

    // last node <hash
    uint256 getPrevLedgerIndex (uint256 const& uHash) const;

    // last node <hash, >begin
    uint256 getPrevLedgerIndex (uint256 const& uHash, uint256 const& uBegin) const;

    // Ledger hash table function
    uint256 getLedgerHash (std::uint32_t ledgerIndex);
    typedef std::vector<std::pair<std::uint32_t, uint256>> LedgerHashes;
    LedgerHashes getLedgerHashes () const;

    std::vector<uint256> getLedgerAmendments () const;

    std::vector<uint256> getNeededTransactionHashes (
        int max, SHAMapSyncFilter* filter) const;
    std::vector<uint256> getNeededAccountStateHashes (
        int max, SHAMapSyncFilter* filter) const;

    //
    // Offer functions
    //

    SLE::pointer getOffer (uint256 const& uIndex) const;
    SLE::pointer getOffer (Account const& account, std::uint32_t uSequence) const;

    //
    // Directory functions
    // Directories are doubly linked lists of nodes.

    // Given a directory root and and index compute the index of a node.
    static void ownerDirDescriber (SLE::ref, bool, Account const& owner);

    // Return a node: root or normal
    SLE::pointer getDirNode (uint256 const& uNodeIndex) const;

    //
    // Quality
    //

    static void qualityDirDescriber (
        SLE::ref, bool,
        Currency const& uTakerPaysCurrency, Account const& uTakerPaysIssuer,
        Currency const& uTakerGetsCurrency, Account const& uTakerGetsIssuer,
        const std::uint64_t & uRate);

    //
    // Ripple functions : credit lines
    //

    SLE::pointer
    getRippleState (uint256 const& uNode) const;

    SLE::pointer
    getRippleState (
        Account const& a, Account const& b, Currency const& currency) const;

    std::uint32_t getReferenceFeeUnits ()
    {
        // Returns the cost of the reference transaction in fee units
        updateFees ();
        return mReferenceFeeUnits;
    }

    std::uint64_t getBaseFee ()
    {
        // Returns the cost of the reference transaction in drops
        updateFees ();
        return mBaseFee;
    }

    std::uint64_t getReserve (int increments)
    {
        // Returns the required reserve in drops
        updateFees ();
        return static_cast<std::uint64_t> (increments) * mReserveIncrement
            + mReserveBase;
    }

    std::uint64_t getReserveInc ()
    {
        updateFees ();
        return mReserveIncrement;
    }

    std::uint64_t scaleFeeBase (std::uint64_t fee);
    std::uint64_t scaleFeeLoad (std::uint64_t fee, bool bAdmin);

    static std::set<std::uint32_t> getPendingSaves();

    /** Const version of getHash() which gets the current value without calling
        updateHash(). */
    uint256 const& getRawHash () const
    {
        return mHash;
    }

    bool walkLedger () const;
    bool assertSane () const;

protected:
    SLE::pointer getASNode (
        LedgerStateParms& parms, uint256 const& nodeID, LedgerEntryType let) const;

    // returned SLE is immutable
    SLE::pointer getASNodeI (uint256 const& nodeID, LedgerEntryType let) const;

    void saveValidatedLedgerAsync(Job&, bool current)
    {
        saveValidatedLedger(current);
    }
    bool saveValidatedLedger (bool current);

private:
    void initializeFees ();
    void updateFees ();

    // The basic Ledger structure, can be opened, closed, or synching
    uint256       mHash;
    uint256       mParentHash;
    uint256       mTransHash;
    uint256       mAccountHash;
    std::uint64_t mTotCoins;
    std::uint32_t mLedgerSeq;

    // when this ledger closed
    std::uint32_t mCloseTime;

    // when the previous ledger closed
    std::uint32_t mParentCloseTime;

    // the resolution for this ledger close time (2-120 seconds)
    int           mCloseResolution;

    // flags indicating how this ledger close took place
    std::uint32_t mCloseFlags;
    bool          mClosed, mValidated, mValidHash, mAccepted, mImmutable;

    // Fee units for the reference transaction
    std::uint32_t mReferenceFeeUnits;

    // Reserve basse and increment in fee units
    std::uint32_t mReserveBase, mReserveIncrement;

    // Ripple cost of the reference transaction
    std::uint64_t mBaseFee;

    std::shared_ptr<SHAMap> mTransactionMap;
    std::shared_ptr<SHAMap> mAccountStateMap;

    typedef RippleMutex StaticLockType;
    typedef std::lock_guard <StaticLockType> StaticScopedLockType;

    // Ledgers not fully saved, validated ledger present but DB may not be
    // correct yet.
    static StaticLockType sPendingSaveLock;

    static std::set<std::uint32_t>  sPendingSaves;
};

inline LedgerStateParms operator| (
    const LedgerStateParms& l1, const LedgerStateParms& l2)
{
    return static_cast<LedgerStateParms> (
        static_cast<int> (l1) | static_cast<int> (l2));
}

inline LedgerStateParms operator& (
    const LedgerStateParms& l1, const LedgerStateParms& l2)
{
    return static_cast<LedgerStateParms> (
        static_cast<int> (l1) & static_cast<int> (l2));
}

} // ripple

#endif
