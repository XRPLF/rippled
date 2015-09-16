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

#include <ripple/ledger/TxMeta.h>
#include <ripple/ledger/View.h>
#include <ripple/ledger/CachedView.h>
#include <ripple/app/tx/Transaction.h>
#include <ripple/basics/CountedObject.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/Book.h>
#include <ripple/shamap/SHAMap.h>
#include <beast/utility/Journal.h>
#include <boost/optional.hpp>
#include <mutex>

namespace ripple {

class Application;
class Job;
class TransactionMaster;

class SqliteStatement;

struct create_genesis_t {};
extern create_genesis_t const create_genesis;

/** Holds a ledger.

    The ledger is composed of two SHAMaps. The state map holds all of the
    ledger entries such as account roots and order books. The tx map holds
    all of the transactions and associated metadata that made it into that
    particular ledger. Most of the operations on a ledger are concerned
    with the state map.

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

    @note Presented to clients as ReadView
    @note Calls virtuals in the constructor, so marked as final
*/
class Ledger final
    : public std::enable_shared_from_this <Ledger>
    , public DigestAwareReadView
    , public TxsRawView
    , public CountedObject <Ledger>
{
public:
    static char const* getCountedObjectName () { return "Ledger"; }

    using pointer = std::shared_ptr<Ledger>;
    using ref     = const std::shared_ptr<Ledger>&;

    Ledger (Ledger const&) = delete;
    Ledger& operator= (Ledger const&) = delete;

    /** Create the Genesis ledger.

        The Genesis ledger contains a single account whose
        AccountID is generated with a Generator using the seed
        computed from the string "masterpassphrase" and ordinal
        zero.

        The account has an XRP balance equal to the total amount
        of XRP in the system. No more XRP than the amount which
        starts in this account can ever exist, with amounts
        used to pay fees being destroyed.
    */
    Ledger (create_genesis_t, Config const& config, Family& family);

    // Used for ledgers loaded from JSON files
    Ledger (uint256 const& parentHash, uint256 const& transHash,
            uint256 const& accountHash,
            std::uint64_t totDrops, std::uint32_t closeTime,
            std::uint32_t parentCloseTime, int closeFlags, int closeResolution,
            std::uint32_t ledgerSeq, bool & loaded, Config const& config,
            Family& family);

    // Create a new ledger that's a snapshot of this one
    Ledger (Ledger const& target, bool isMutable);

    /** Create a new open ledger

        The ledger will have the sequence number that
        follows previous, and have
        parentCloseTime == previous.closeTime.
    */
    Ledger (open_ledger_t, Ledger const& previous);

    Ledger (void const* data,
        std::size_t size, bool hasPrefix,
            Config const& config, Family& family);

    // used for database ledgers
    Ledger (std::uint32_t ledgerSeq,
        std::uint32_t closeTime, Config const& config,
            Family& family);

    ~Ledger();

    //
    // ReadView
    //

    LedgerInfo const&
    info() const
    {
        return info_;
    }

    Fees const&
    fees() const override
    {
        return fees_;
    }

    Rules const&
    rules() const override
    {
        return rules_;
    }

    bool
    exists (Keylet const& k) const override;

    boost::optional<uint256>
    succ (uint256 const& key, boost::optional<
        uint256> const& last = boost::none) const override;

    std::shared_ptr<SLE const>
    read (Keylet const& k) const override;

    std::unique_ptr<sles_type::iter_base>
    slesBegin() const override;

    std::unique_ptr<sles_type::iter_base>
    slesEnd() const override;

    std::unique_ptr<txs_type::iter_base>
    txsBegin() const override;

    std::unique_ptr<txs_type::iter_base>
    txsEnd() const override;

    bool
    txExists (uint256 const& key) const override;

    tx_type
    txRead (key_type const& key) const override;

    //
    // DigestAwareReadView
    //

    boost::optional<digest_type>
    digest (key_type const& key) const override;

    //
    // RawView
    //

    void
    rawErase (std::shared_ptr<
        SLE> const& sle) override;

    void
    rawInsert (std::shared_ptr<
        SLE> const& sle) override;

    void
    rawReplace (std::shared_ptr<
        SLE> const& sle) override;

    void
    rawDestroyXRP (XRPAmount const& fee) override
    {
        info_.drops -= fee;
    }

    //
    // TxsRawView
    //

    void
    rawTxInsert (uint256 const& key,
        std::shared_ptr<Serializer const
            > const& txn, std::shared_ptr<
                Serializer const> const& metaData) override;

    //--------------------------------------------------------------------------

    void setClosed()
    {
        info_.open = false;
    }

    void setValidated()
    {
        info_.validated = true;
    }

    void setAccepted (std::uint32_t closeTime,
        int closeResolution, bool correctCloseTime);

    void setImmutable ();

    bool isImmutable () const
    {
        return mImmutable;
    }

    // Indicates that all ledger entries
    // are available locally. For example,
    // all in the NodeStore and memory.
    void setFull ()
    {
        txMap_->setLedgerSeq (info_.seq);
        stateMap_->setLedgerSeq (info_.seq);
    }

    // ledger signature operations
    void addRaw (Serializer& s) const;
    void setRaw (SerialIter& sit, bool hasPrefix, Family& family);

    // DEPRECATED
    // Remove contract.h include
    uint256 const&
    getHash() const
    {
        return info_.hash;
    }

    void setTotalDrops (std::uint64_t totDrops)
    {
        info_.drops = totDrops;
    }

    // close time functions
    void setCloseTime (std::uint32_t when)
    {
        assert (!mImmutable);
        info_.closeTime = when;
    }

    void setCloseTime (boost::posix_time::ptime);

    boost::posix_time::ptime getCloseTime () const;

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

    // DEPRECATED use fees()
    std::uint64_t getReserve (int increments) const
    {
        // Returns the required reserve in drops
        deprecatedUpdateCachedFees ();
        return static_cast<std::uint64_t> (increments) * mReserveIncrement
            + mReserveBase;
    }

    // DEPRECATED use fees()
    std::uint64_t getReserveInc () const
    {
        deprecatedUpdateCachedFees ();
        return mReserveIncrement;
    }

    bool walkLedger () const;

    bool assertSane ();

private:
    class sles_iter_impl;
    class txs_iter_impl;

    void
    setup (Config const& config);

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

    bool mValidHash = false;
    bool mImmutable;

    std::shared_ptr<SHAMap> txMap_;
    std::shared_ptr<SHAMap> stateMap_;

    // Protects fee variables
    std::mutex mutable mutex_;

    Fees fees_;
    Rules rules_;
    LedgerInfo info_;

    // Ripple cost of the reference transaction
    std::uint64_t mutable mBaseFee = 0;

    // Fee units for the reference transaction
    std::uint32_t mutable mReferenceFeeUnits = 0;

    // Reserve base in drops
    std::uint32_t mutable mReserveBase = 0;
    // Reserve increment in drops
    std::uint32_t mutable mReserveIncrement = 0;
};

/** A ledger wrapped in a CachedView. */
using CachedLedger = CachedView<Ledger>;

//------------------------------------------------------------------------------
//
// API
//
//------------------------------------------------------------------------------

extern
bool
pendSaveValidated(
    Application& app,
    std::shared_ptr<Ledger> const& ledger,
    bool isSynchronous,
    bool isCurrent);

extern
Ledger::pointer
loadByIndex (std::uint32_t ledgerIndex,
    Application& app);

extern
std::tuple<Ledger::pointer, std::uint32_t, uint256>
loadLedgerHelper(std::string const& sqlSuffix,
    Application& app);

extern
Ledger::pointer
loadByHash (uint256 const& ledgerHash, Application& app);

extern
uint256
getHashByIndex(std::uint32_t index, Application& app);

extern
bool
getHashesByIndex(std::uint32_t index,
    uint256 &ledgerHash, uint256& parentHash,
        Application& app);

extern
std::map< std::uint32_t, std::pair<uint256, uint256>>
getHashesByIndex (std::uint32_t minSeq, std::uint32_t maxSeq,
    Application& app);

/** Deserialize a SHAMapItem containing a single STTx

    Throw:

        May throw on deserializaton error
*/
std::shared_ptr<STTx const>
deserializeTx (SHAMapItem const& item);

/** Deserialize a SHAMapItem containing STTx + STObject metadata

    The SHAMap must contain two variable length
    serialization objects.

    Throw:

        May throw on deserializaton error
*/
std::pair<std::shared_ptr<
    STTx const>, std::shared_ptr<
        STObject const>>
deserializeTxPlusMeta (SHAMapItem const& item);

// DEPRECATED
inline
std::shared_ptr<SLE const>
cachedRead (ReadView const& ledger, uint256 const& key,
    boost::optional<LedgerEntryType> type = boost::none)
{
    if (type)
        return ledger.read(Keylet(*type, key));
    return ledger.read(keylet::unchecked(key));
}

//------------------------------------------------------------------------------

void
ownerDirDescriber (SLE::ref, bool, AccountID const& owner);

// VFALCO NOTE This is referenced from only one place
void
qualityDirDescriber (
    SLE::ref, bool,
    Currency const& uTakerPaysCurrency, AccountID const& uTakerPaysIssuer,
    Currency const& uTakerGetsCurrency, AccountID const& uTakerGetsIssuer,
    const std::uint64_t & uRate, Application& app);

} // ripple

#endif
