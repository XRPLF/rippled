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
#include <ripple/app/ledger/SLECache.h>
#include <ripple/app/misc/AccountState.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <ripple/basics/CountedObject.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/Book.h>
#include <boost/optional.hpp>
#include <mutex>

namespace ripple {

class Job;

class SqliteStatement;

/** Holds a ledger.

    The ledger is composed of two SHAMaps. The state map holds all of the
    ledger entries such as account roots and order books. The tx map holds
    all of the transactions and associated metadata that made it into that
    particular ledger. Most of the operations on a ledger are concerned
    with the state map.
    
    A View provides a structured interface to manipulate the state map in
    a reversible way, with facilities to automatically produce metadata
    when applying changes.

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

    using pointer = std::shared_ptr<Ledger>;
    using ref     = const std::shared_ptr<Ledger>&;

    Ledger (Ledger const&) = delete;
    Ledger& operator= (Ledger const&) = delete;

    // used for the starting bootstrap ledger
    Ledger (RippleAddress const& masterID,
        std::uint64_t startAmount);

    // Used for ledgers loaded from JSON files
    Ledger (uint256 const& parentHash, uint256 const& transHash,
            uint256 const& accountHash,
            std::uint64_t totCoins, std::uint32_t closeTime,
            std::uint32_t parentCloseTime, int closeFlags, int closeResolution,
            std::uint32_t ledgerSeq, bool & loaded);

    // used for database ledgers
    Ledger (std::uint32_t ledgerSeq, std::uint32_t closeTime);

    Ledger (void const* data,
        std::size_t size, bool hasPrefix);

    // Create a new ledger that follows this one
    // VFALCO `previous` should be const
    Ledger (bool dummy, Ledger& previous);

    // Create a new ledger that's a snapshot of this one
    Ledger (Ledger const& target, bool isMutable);

    ~Ledger();

    //--------------------------------------------------------------------------

    /** Returns `true` if a ledger entry exists. */
    bool
    exists (uint256 const& key) const;

    /** Return the state item for a key.
        The item may not be modified.
        @return The serialized ledger entry or empty
                if the key does not exist.
    */
    std::shared_ptr<SHAMapItem const>
    find (uint256 const& key) const;

    /** Add a new state SLE.
        Effects:
            assert if the key already exists.
            The key in the state map is associated
                with an unflattened copy of the SLE.
        @note The key is taken from the SLE.
    */
    void
    insert (SLE const& sle);

    /** Fetch a modifiable state SLE.
        Effects:
            Gives the caller ownership of an
                unflattened copy of the SLE.
        @return `empty` if the key is not present
    */
    boost::optional<SLE>
    fetch (uint256 const& key) const;

    /** Replace an existing state SLE.
        Effects:
            assert if key does not already exist.
            The previous flattened SLE associated with
                the key is released.
            The key in the state map is associated
                with a flattened copy of the SLE.
        @note The key is taken from the SLE
    */
    void
    replace (SLE const& sle);

    /** Remove an state SLE.
        Effects:
            assert if the key does not exist.
            The flattened SLE associated with the key
                is released from the state map.
    */
    void
    erase (uint256 const& key);

    //--------------------------------------------------------------------------

    void updateHash ();

    void setClosed ()
    {
        mClosed = true;
    }
    
    void setValidated()
    {
        mValidated = true;
    }
    
    void setAccepted (std::uint32_t closeTime,
        int closeResolution, bool correctCloseTime);

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

    void setFull ()
    {
        mTransactionMap->setLedgerSeq (mLedgerSeq);
        mAccountStateMap->setLedgerSeq (mLedgerSeq);
    }

    // ledger signature operations
    void addRaw (Serializer& s) const;
    void setRaw (SerialIter& sit, bool hasPrefix);

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

    SLE::pointer getAccountRoot (Account const& accountID) const;

    SLE::pointer getAccountRoot (const RippleAddress & naAccountID) const;

    void updateSkipList ();

    void visitStateItems (std::function<void (SLE::ref)>) const;

    bool pendSaveValidated (bool isSynchronous, bool isCurrent);

    // Retrieve ledger entries
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

    std::vector<uint256> getNeededTransactionHashes (
        int max, SHAMapSyncFilter* filter) const;

    std::vector<uint256> getNeededAccountStateHashes (
        int max, SHAMapSyncFilter* filter) const;

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

    std::uint32_t getReferenceFeeUnits() const
    {
        // Returns the cost of the reference transaction in fee units
        deprecatedUpdateCachedFees ();
        return mReferenceFeeUnits;
    }

    std::uint64_t getBaseFee() const
    {
        // Returns the cost of the reference transaction in drops
        deprecatedUpdateCachedFees ();
        return mBaseFee;
    }

    std::uint64_t getReserve (int increments) const
    {
        // Returns the required reserve in drops
        deprecatedUpdateCachedFees ();
        return static_cast<std::uint64_t> (increments) * mReserveIncrement
            + mReserveBase;
    }

    std::uint64_t getReserveInc () const
    {
        deprecatedUpdateCachedFees ();
        return mReserveIncrement;
    }

    /** Const version of getHash() which gets the current value without calling
        updateHash(). */
    uint256 const& getRawHash () const
    {
        return mHash;
    }

    bool walkLedger () const;

    bool assertSane () const;

    // database functions (low-level)
    static Ledger::pointer loadByIndex (std::uint32_t ledgerIndex);

    static Ledger::pointer loadByHash (uint256 const& ledgerHash);

    static uint256 getHashByIndex (std::uint32_t index);

    static bool getHashesByIndex (
        std::uint32_t index, uint256 & ledgerHash, uint256 & parentHash);

    static std::map< std::uint32_t, std::pair<uint256, uint256> >
                  getHashesByIndex (std::uint32_t minSeq, std::uint32_t maxSeq);

protected:
    // returned SLE is immutable
    SLE::pointer getASNodeI (uint256 const& nodeID, LedgerEntryType let) const;

    void saveValidatedLedgerAsync(Job&, bool current)
    {
        saveValidatedLedger(current);
    }
    bool saveValidatedLedger (bool current);

private:
    // ledger close flags
    static const std::uint32_t sLCF_NoConsensusTime = 1;

    SLE::pointer getFeeNode (uint256 const& nodeID) const;

    // Updates the fees cached in the ledger.
    // Safe to call concurrently. We shouldn't be storing
    // fees in the Ledger object, they should be a local side-structure
    // associated with a particular module (rpc, tx processing, consensus)
    //
    void deprecatedUpdateCachedFees() const;

    // The basic Ledger structure, can be opened, closed, or synching
    uint256       mHash; // VFALCO This could be boost::optional<uint256>
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
    bool mClosed = false;
    bool mValidated = false;
    bool mValidHash = false;
    bool mAccepted = false;
    bool mImmutable;

    std::shared_ptr<SHAMap> mTransactionMap;
    std::shared_ptr<SHAMap> mAccountStateMap;

    // Protects fee variables
    std::mutex mutable mutex_;

    // Ripple cost of the reference transaction
    std::uint64_t mutable mBaseFee = 0;

    // Fee units for the reference transaction
    std::uint32_t mutable mReferenceFeeUnits = 0;

    // Reserve base in fee units
    std::uint32_t mutable mReserveBase = 0;
    // Reserve increment in fee units
    std::uint32_t mutable mReserveIncrement = 0;
};

//------------------------------------------------------------------------------
//
// API
//
//------------------------------------------------------------------------------

std::tuple<Ledger::pointer, std::uint32_t, uint256>
loadLedgerHelper(std::string const& sqlSuffix);

/** SLE cache-aware deserialized state SLE fetch.
    Effects:
        If the key exists, the item is flattened
            and added to the SLE cache.
    The returned object may not be modified.
    @param type An optional LedgerEntryType. If type is
                engaged and the SLE's type does not match,
                an empty shared_ptr is returned.
    @return `empty` if the key is not present
*/
std::shared_ptr<SLE const>
fetch (Ledger const& ledger, uint256 const& key, SLECache& cache,
    boost::optional<LedgerEntryType> type = boost::none);

/** Iterate all items in an account's owner directory. */
void
forEachItem (Ledger const& ledger, Account const& id, SLECache& cache,
    std::function<void (std::shared_ptr<SLE const> const&)> f);

/** Iterate all items after an item in an owner directory.
    @param after The key of the item to start after
    @param hint The directory page containing `after`
    @param limit The maximum number of items to return
    @return `false` if the iteration failed
*/
bool
forEachItemAfter (Ledger const& ledger, Account const& id, SLECache& cache,
    uint256 const& after, std::uint64_t const hint, unsigned int limit,
        std::function <bool (std::shared_ptr<SLE const> const&)>);

// DEPRECATED
// VFALCO This could return by value
//        This should take AccountID parameter
AccountState::pointer
getAccountState (Ledger const& ledger,
    RippleAddress const& accountID);

} // ripple

#endif
