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
#include <ripple/ledger/SLECache.h>
#include <ripple/ledger/View.h>
#include <ripple/app/misc/AccountState.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <ripple/basics/CountedObject.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/Book.h>
#include <beast/utility/Journal.h>
#include <boost/optional.hpp>
#include <mutex>

namespace ripple {

class Job;
class TransactionMaster;

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
    , public BasicView
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
    //
    // BasicView
    //
    //--------------------------------------------------------------------------

    bool
    exists (Keylet const& k) const override;

    boost::optional<uint256>
    succ (uint256 const& key, boost::optional<
        uint256> last = boost::none) const override;

    std::shared_ptr<SLE const>
    read (Keylet const& k) const override;

    bool
    unchecked_erase (uint256 const& key) override;

    void
    unchecked_insert (std::shared_ptr<SLE>&& sle) override;

    void
    unchecked_replace (std::shared_ptr<SLE>&& sle) override;

    BasicView const*
    parent() const override
    {
        return nullptr;
    }

    //--------------------------------------------------------------------------

    /** Hint that the contents have changed.
        Thread Safety:
            Not thread safe
        Effects:
            The next call to getHash will return updated hashes
    */
    void
    touch()
    {
        mValidHash = false;
    }

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

    // VFALCO Rename to closed
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
        txMap_->setLedgerSeq (seq_);
        stateMap_->setLedgerSeq (seq_);
    }

    // ledger signature operations
    void addRaw (Serializer& s) const;
    void setRaw (SerialIter& sit, bool hasPrefix);

    /** Return the hash of the ledger.
        This will recalculate the hash if necessary.
    */
    uint256 const&
    getHash();

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

    LedgerIndex
    seq() const
    {
        return seq_;
    }

    // DEPRECATED
    std::uint32_t getLedgerSeq () const
    {
        return seq_;
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

    // VFALCO NOTE We should ensure that there are
    //             always valid state and tx maps
    //             and get rid of these functions.
    bool
    haveStateMap() const
    {
        return stateMap_ != nullptr;
    }

    bool
    haveTxMap() const
    {
        return txMap_ != nullptr;
    }

    SHAMap const&
    stateMap() const
    {
        return *stateMap_;
    }

    SHAMap&
    stateMap()
    {
        return *stateMap_;
    }

    SHAMap const&
    txMap() const
    {
        return *txMap_;
    }

    SHAMap&
    txMap()
    {
        return *txMap_;
    }

    // returns false on error
    bool addSLE (SLE const& sle);

    // ledger sync functions
    void setAcquiring (void);
    bool isAcquiring (void) const;
    bool isAcquiringTx (void) const;
    bool isAcquiringAS (void) const;

    //--------------------------------------------------------------------------

    void updateSkipList ();

    void visitStateItems (std::function<void (SLE::ref)>) const;

    bool pendSaveValidated (bool isSynchronous, bool isCurrent);

    // first node >hash, <last
    uint256 getNextLedgerIndex (uint256 const& hash,
        boost::optional<uint256> const& last = boost::none) const;

    std::vector<uint256> getNeededTransactionHashes (
        int max, SHAMapSyncFilter* filter) const;

    std::vector<uint256> getNeededAccountStateHashes (
        int max, SHAMapSyncFilter* filter) const;

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

    bool walkLedger () const;

    bool assertSane ();

    // database functions (low-level)
    static Ledger::pointer loadByIndex (std::uint32_t ledgerIndex);

    static Ledger::pointer loadByHash (uint256 const& ledgerHash);

    static uint256 getHashByIndex (std::uint32_t index);

    static bool getHashesByIndex (
        std::uint32_t index, uint256 & ledgerHash, uint256 & parentHash);

    static std::map< std::uint32_t, std::pair<uint256, uint256> >
                  getHashesByIndex (std::uint32_t minSeq, std::uint32_t maxSeq);

protected:
    void saveValidatedLedgerAsync(Job&, bool current)
    {
        saveValidatedLedger(current);
    }
    bool saveValidatedLedger (bool current);

private:
    // ledger close flags
    static const std::uint32_t sLCF_NoConsensusTime = 1;

    std::shared_ptr<SLE>
    peek (Keylet const& k) const;

    void
    updateHash();

    // Updates the fees cached in the ledger.
    // Safe to call concurrently. We shouldn't be storing
    // fees in the Ledger object, they should be a local side-structure
    // associated with a particular module (rpc, tx processing, consensus)
    //
    void deprecatedUpdateCachedFees() const;

    // The basic Ledger structure, can be opened, closed, or synching
    uint256 mHash; // VFALCO This could be boost::optional<uint256>
    uint256 mParentHash;
    uint256 mTransHash;
    uint256 mAccountHash;
    std::uint64_t mTotCoins;
    std::uint32_t seq_;

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

    std::shared_ptr<SHAMap> txMap_;
    std::shared_ptr<SHAMap> stateMap_;

    // Protects fee variables
    std::mutex mutable mutex_;

    // Ripple cost of the reference transaction
    std::uint64_t mutable mBaseFee = 0;

    // Fee units for the reference transaction
    std::uint32_t mutable mReferenceFeeUnits = 0;

    // Reserve base in drops
    std::uint32_t mutable mReserveBase = 0;
    // Reserve increment in drops
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
cachedRead (Ledger const& ledger, uint256 const& key, SLECache& cache,
    boost::optional<LedgerEntryType> type = boost::none);

// DEPRECATED
// VFALCO This could return by value
//        This should take AccountID parameter
AccountState::pointer
getAccountState (Ledger const& ledger,
    RippleAddress const& accountID,
        SLECache& cache);

/** Return the hash of a ledger by sequence.
    The hash is retrieved by looking up the "skip list"
    in the passed ledger. As the skip list is limited
    in size, if the requested ledger sequence number is
    out of the range of ledgers represented in the skip
    list, then boost::none is returned.
    @return The hash of the ledger with the
            given sequence number or boost::none.
*/
boost::optional<uint256>
hashOfSeq (Ledger& ledger, LedgerIndex seq,
    SLECache& cache, beast::Journal journal);

/** Find a ledger index from which we could easily get the requested ledger

    The index that we return should meet two requirements:
        1) It must be the index of a ledger that has the hash of the ledger
            we are looking for. This means that its sequence must be equal to
            greater than the sequence that we want but not more than 256 greater
            since each ledger contains the hashes of the 256 previous ledgers.

        2) Its hash must be easy for us to find. This means it must be 0 mod 256
            because every such ledger is permanently enshrined in a LedgerHashes
            page which we can easily retrieve via the skip list.
*/
inline
LedgerIndex
getCandidateLedger (LedgerIndex requested)
{
    return (requested + 255) & (~255);
}

//------------------------------------------------------------------------------

// VFALCO Should this take Slice? Should id be called key or hash? Or txhash?
bool addTransaction (Ledger& ledger,
        uint256 const& id, Serializer const& txn);

bool addTransaction (Ledger& ledger,
    uint256 const& id, Serializer const& txn, Serializer const& metaData);

inline
bool hasTransaction (Ledger const& ledger,
    uint256 const& TransID)
{
    return ledger.txMap().hasItem (TransID);
}

// VFALCO NOTE This is called from only one place
Transaction::pointer
getTransaction (Ledger const& ledger,
    uint256 const& transID, TransactionMaster& cache);

// VFALCO NOTE This is called from only one place
bool
getTransaction (Ledger const& ledger,
    uint256 const& transID, Transaction::pointer & txn,
        TransactionMetaSet::pointer & txMeta,
            TransactionMaster& cache);

bool
getTransactionMeta (Ledger const&,
    uint256 const& transID,
        TransactionMetaSet::pointer & txMeta);

// VFALCO NOTE This is called from only one place
bool
getMetaHex (Ledger const& ledger,
    uint256 const& transID, std::string & hex);

void
ownerDirDescriber (SLE::ref, bool, AccountID const& owner);

// VFALCO NOTE This is referenced from only one place
void
qualityDirDescriber (
    SLE::ref, bool,
    Currency const& uTakerPaysCurrency, AccountID const& uTakerPaysIssuer,
    Currency const& uTakerGetsCurrency, AccountID const& uTakerGetsIssuer,
    const std::uint64_t & uRate);

//------------------------------------------------------------------------------

std::uint32_t
getParentCloseTimeNC (BasicView const& view);

} // ripple

#endif
