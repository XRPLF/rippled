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

#ifndef RIPPLE_LEDGER_H
#define RIPPLE_LEDGER_H

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

#define LEDGER_JSON_DUMP_TXRP   0x10000000
#define LEDGER_JSON_DUMP_STATE  0x20000000
#define LEDGER_JSON_EXPAND      0x40000000
#define LEDGER_JSON_FULL        0x80000000

class SqliteStatement;

class LedgerBase
{
protected:
    LedgerBase ();

    // VFALCO TODO eliminate the need for friends
    friend class TransactionEngine;
    friend class Transactor;

    typedef RippleRecursiveMutex LockType;
    typedef std::lock_guard <LockType> ScopedLockType;
    LockType mLock;
};

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
*/
class Ledger
    : public std::enable_shared_from_this <Ledger>
    , public LedgerBase
    , public CountedObject <Ledger>
    , public beast::Uncopyable
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

    Ledger (uint256 const & parentHash, uint256 const & transHash,
            uint256 const & accountHash,
            std::uint64_t totCoins, std::uint32_t closeTime,
            std::uint32_t parentCloseTime, int closeFlags, int closeResolution,
            std::uint32_t ledgerSeq, bool & loaded);
    // used for database ledgers

    Ledger (std::uint32_t ledgerSeq, std::uint32_t closeTime);
    Ledger (Blob const & rawLedger, bool hasPrefix);
    Ledger (const std::string & rawLedger, bool hasPrefix);
    Ledger (bool dummy, Ledger & previous); // ledger after this one
    Ledger (Ledger & target, bool isMutable); // snapshot

    ~Ledger ();

    static Ledger::pointer getSQL (const std::string & sqlStatement);
    static Ledger::pointer getSQL1 (SqliteStatement*);
    static void getSQL2 (Ledger::ref);
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
    bool isClosed ()
    {
        return mClosed;
    }
    bool isAccepted ()
    {
        return mAccepted;
    }
    bool isValidated ()
    {
        return mValidated;
    }
    bool isImmutable ()
    {
        return mImmutable;
    }
    bool isFixed ()
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

    uint256 getHash ();
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
    SHAMap::ref peekTransactionMap ()
    {
        return mTransactionMap;
    }
    SHAMap::ref peekAccountStateMap ()
    {
        return mAccountStateMap;
    }
    void dropCache ()
    {
        assert (isImmutable ());

        if (mTransactionMap)
            mTransactionMap->dropCache ();

        if (mAccountStateMap)
            mAccountStateMap->dropCache ();
    }

    // returns false on error
    bool addSLE (SLE const& sle);

    // ledger sync functions
    void setAcquiring (void);
    bool isAcquiring (void);
    bool isAcquiringTx (void);
    bool isAcquiringAS (void);

    // Transaction Functions
    bool addTransaction (uint256 const& id, Serializer const& txn);
    bool addTransaction (
        const uint256& id, Serializer const& txn, Serializer const& metaData);
    bool hasTransaction (uint256 const & TransID) const
    {
        return mTransactionMap->hasItem (TransID);
    }
    Transaction::pointer getTransaction (uint256 const & transID) const;
    bool getTransaction (
        uint256 const & transID,
        Transaction::pointer & txn, TransactionMetaSet::pointer & txMeta);
    bool getTransactionMeta (
        uint256 const & transID, TransactionMetaSet::pointer & txMeta);
    bool getMetaHex (uint256 const & transID, std::string & hex);

    static SerializedTransaction::pointer getSTransaction (
        SHAMapItem::ref, SHAMapTreeNode::TNType);
    SerializedTransaction::pointer getSMTransaction (
        SHAMapItem::ref, SHAMapTreeNode::TNType,
        TransactionMetaSet::pointer & txMeta);

    // high-level functions
    bool hasAccount (const RippleAddress & acctID);
    AccountState::pointer getAccountState (const RippleAddress & acctID);
    LedgerStateParms writeBack (LedgerStateParms parms, SLE::ref);
    SLE::pointer getAccountRoot (Account const& accountID);
    SLE::pointer getAccountRoot (const RippleAddress & naAccountID);
    void updateSkipList ();
    void visitAccountItems (
        Account const& acctID, std::function<void (SLE::ref)>);
    void visitStateItems (std::function<void (SLE::ref)>);

    // database functions (low-level)
    static Ledger::pointer loadByIndex (std::uint32_t ledgerIndex);
    static Ledger::pointer loadByHash (uint256 const & ledgerHash);
    static uint256 getHashByIndex (std::uint32_t index);
    static bool getHashesByIndex (
        std::uint32_t index, uint256 & ledgerHash, uint256 & parentHash);
    static std::map< std::uint32_t, std::pair<uint256, uint256> >
                  getHashesByIndex (std::uint32_t minSeq, std::uint32_t maxSeq);
    bool pendSaveValidated (bool isSynchronous, bool isCurrent);

    // next/prev function
    SLE::pointer getSLE (uint256 const & uHash); // SLE is mutable
    SLE::pointer getSLEi (uint256 const & uHash); // SLE is immutable

    // VFALCO NOTE These seem to let you walk the list of ledgers
    //
    uint256 getFirstLedgerIndex ();
    uint256 getLastLedgerIndex ();

    // first node >hash
    uint256 getNextLedgerIndex (uint256 const & uHash);

    // first node >hash, <end
    uint256 getNextLedgerIndex (uint256 const & uHash, uint256 const & uEnd);

    // last node <hash
    uint256 getPrevLedgerIndex (uint256 const & uHash);

    // last node <hash, >begin
    uint256 getPrevLedgerIndex (uint256 const & uHash, uint256 const & uBegin);

    // Ledger hash table function
    static uint256 getLedgerHashIndex ();
    static uint256 getLedgerHashIndex (std::uint32_t desiredLedgerIndex);
    static int getLedgerHashOffset (std::uint32_t desiredLedgerIndex);
    static int getLedgerHashOffset (
        std::uint32_t desiredLedgerIndex, std::uint32_t currentLedgerIndex);

    uint256 getLedgerHash (std::uint32_t ledgerIndex);
    typedef std::vector<std::pair<std::uint32_t, uint256>> LedgerHashes;
    LedgerHashes getLedgerHashes ();

    static uint256 getLedgerAmendmentIndex ();
    static uint256 getLedgerFeeIndex ();
    std::vector<uint256> getLedgerAmendments ();

    std::vector<uint256> getNeededTransactionHashes (
        int max, SHAMapSyncFilter* filter);
    std::vector<uint256> getNeededAccountStateHashes (
        int max, SHAMapSyncFilter* filter);

    // index calculation functions
    static uint256 getAccountRootIndex (Account const&);

    static uint256 getAccountRootIndex (const RippleAddress & account)
    {
        return getAccountRootIndex (account.getAccountID ());
    }

    //
    // Generator Map functions
    //

    SLE::pointer getGenerator (Account const& uGeneratorID);
    static uint256 getGeneratorIndex (Account const& uGeneratorID);

    //
    // Nickname functions
    //

    static uint256 getNicknameHash (const std::string & strNickname)
    {
        Serializer s (strNickname);
        return s.getSHA256 ();
    }

    NicknameState::pointer getNicknameState (uint256 const & uNickname);
    NicknameState::pointer getNicknameState (const std::string & strNickname)
    {
        return getNicknameState (getNicknameHash (strNickname));
    }

    SLE::pointer getNickname (uint256 const & uNickname);
    SLE::pointer getNickname (const std::string & strNickname)
    {
        return getNickname (getNicknameHash (strNickname));
    }

    static uint256 getNicknameIndex (uint256 const & uNickname);

    //
    // Order book functions
    //

    // Order book dirs have a base so we can use next to step through them in
    // quality order.
    static uint256 getBookBase (Book const&);

    //
    // Offer functions
    //

    SLE::pointer getOffer (uint256 const & uIndex);
    SLE::pointer getOffer (Account const & account, std::uint32_t uSequence)
    {
        return getOffer (getOfferIndex (account, uSequence));
    }

    // The index of an offer.
    static uint256 getOfferIndex (
        Account const & account, std::uint32_t uSequence);

    //
    // Owner functions
    //

    // VFALCO NOTE This is a simple math operation that converts the account ID
    //             into a 256 bit object (I think....need to research this)
    //
    // All items controlled by an account are here: offers
    static uint256 getOwnerDirIndex (Account const &account);

    //
    // Directory functions
    // Directories are doubly linked lists of nodes.

    // Given a directory root and and index compute the index of a node.
    static uint256 getDirNodeIndex (
        uint256 const & uDirRoot, const std::uint64_t uNodeIndex = 0);
    static void ownerDirDescriber (SLE::ref, bool, Account const& owner);

    // Return a node: root or normal
    SLE::pointer getDirNode (uint256 const & uNodeIndex);

    //
    // Quality
    //

    static uint256 getQualityIndex (
        uint256 const & uBase, const std::uint64_t uNodeDir = 0);
    static uint256 getQualityNext (uint256 const & uBase);
    static std::uint64_t getQuality (uint256 const & uBase);
    static void qualityDirDescriber (
        SLE::ref, bool,
        Currency const& uTakerPaysCurrency, Account const& uTakerPaysIssuer,
        Currency const& uTakerGetsCurrency, Account const& uTakerGetsIssuer,
        const std::uint64_t & uRate);

    //
    // Ripple functions : credit lines
    //
    //
    // Index of node which is the ripple state between two accounts for a
    // currency.
    //
    // VFALCO NOTE Rename these to make it clear they are simple functions that
    //             don't access global variables. e.g.
    //             "calculateKeyFromRippleStateAndAddress"
    static uint256 getRippleStateIndex (
        const RippleAddress&, const RippleAddress&, Currency const&);
    static uint256 getRippleStateIndex (
        Account const& a, Account const& b, Currency const& uCurrency)
    {
        return getRippleStateIndex (
            RippleAddress::createAccountID (a),
            RippleAddress::createAccountID (b), uCurrency);
    }

    SLE::pointer getRippleState (uint256 const & uNode);

    SLE::pointer getRippleState (
        RippleAddress const& a, RippleAddress const& b,
        Currency const& currency)
    {
        return getRippleState (getRippleStateIndex (a, b, currency));
    }

    SLE::pointer getRippleState (
        Account const& a, Account const& b, Currency const& currency)
    {
        return getRippleState (getRippleStateIndex (
            RippleAddress::createAccountID (a),
            RippleAddress::createAccountID (b), currency));
    }

    std::uint32_t getReferenceFeeUnits ()
    {
        updateFees ();
        return mReferenceFeeUnits;
    }

    std::uint64_t getBaseFee ()
    {
        updateFees ();
        return mBaseFee;
    }

    std::uint64_t getReserve (int increments)
    {
        updateFees ();
        return scaleFeeBase (
            static_cast<std::uint64_t> (increments) * mReserveIncrement
            + mReserveBase);
    }

    std::uint64_t getReserveInc ()
    {
        updateFees ();
        return mReserveIncrement;
    }

    std::uint64_t scaleFeeBase (std::uint64_t fee);
    std::uint64_t scaleFeeLoad (std::uint64_t fee, bool bAdmin);

    static std::set<std::uint32_t> getPendingSaves();

    Json::Value getJson (int options);
    void addJson (Json::Value&, int options);

    bool walkLedger ();
    bool assertSane ();

protected:
    SLE::pointer getASNode (
        LedgerStateParms& parms, uint256 const& nodeID, LedgerEntryType let);

    // returned SLE is immutable
    SLE::pointer getASNodeI (uint256 const& nodeID, LedgerEntryType let);

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

    SHAMap::pointer mTransactionMap;
    SHAMap::pointer mAccountStateMap;

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
