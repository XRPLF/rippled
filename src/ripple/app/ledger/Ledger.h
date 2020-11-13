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

#include <ripple/basics/CountedObject.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/core/TimeKeeper.h>
#include <ripple/ledger/CachedView.h>
#include <ripple/ledger/TxMeta.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Book.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/shamap/SHAMap.h>
#include <mutex>

namespace ripple {

class Application;
class Job;
class TransactionMaster;

class SqliteStatement;

struct create_genesis_t
{
    explicit create_genesis_t() = default;
};
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
class Ledger final : public std::enable_shared_from_this<Ledger>,
                     public DigestAwareReadView,
                     public TxsRawView,
                     public CountedObject<Ledger>
{
public:
    Ledger(Ledger const&) = delete;
    Ledger&
    operator=(Ledger const&) = delete;

    /** Create the Genesis ledger.

        The Genesis ledger contains a single account whose
        AccountID is generated with a Generator using the seed
        computed from the string "masterpassphrase" and ordinal
        zero.

        The account has an XRP balance equal to the total amount
        of XRP in the system. No more XRP than the amount which
        starts in this account can ever exist, with amounts
        used to pay fees being destroyed.

        Amendments specified are enabled in the genesis ledger
    */
    Ledger(
        create_genesis_t,
        Config const& config,
        std::vector<uint256> const& amendments,
        Family& family);

    Ledger(LedgerInfo const& info, Config const& config, Family& family);

    /** Used for ledgers loaded from JSON files

        @param acquire If true, acquires the ledger if not found locally
    */
    Ledger(
        LedgerInfo const& info,
        bool& loaded,
        bool acquire,
        Config const& config,
        Family& family,
        beast::Journal j);

    /** Create a new ledger following a previous ledger

        The ledger will have the sequence number that
        follows previous, and have
        parentCloseTime == previous.closeTime.
    */
    Ledger(Ledger const& previous, NetClock::time_point closeTime);

    // used for database ledgers
    Ledger(
        std::uint32_t ledgerSeq,
        NetClock::time_point closeTime,
        Config const& config,
        Family& family);

    ~Ledger() = default;

    //
    // ReadView
    //

    bool
    open() const override
    {
        return false;
    }

    LedgerInfo const&
    info() const override
    {
        return info_;
    }

    void
    setLedgerInfo(LedgerInfo const& info)
    {
        info_ = info;
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
    exists(Keylet const& k) const override;

    bool
    exists(uint256 const& key) const;

    std::optional<uint256>
    succ(uint256 const& key, std::optional<uint256> const& last = std::nullopt)
        const override;

    std::shared_ptr<SLE const>
    read(Keylet const& k) const override;

    std::unique_ptr<sles_type::iter_base>
    slesBegin() const override;

    std::unique_ptr<sles_type::iter_base>
    slesEnd() const override;

    std::unique_ptr<sles_type::iter_base>
    slesUpperBound(uint256 const& key) const override;

    std::unique_ptr<txs_type::iter_base>
    txsBegin() const override;

    std::unique_ptr<txs_type::iter_base>
    txsEnd() const override;

    bool
    txExists(uint256 const& key) const override;

    tx_type
    txRead(key_type const& key) const override;

    //
    // DigestAwareReadView
    //

    std::optional<digest_type>
    digest(key_type const& key) const override;

    //
    // RawView
    //

    void
    rawErase(std::shared_ptr<SLE> const& sle) override;

    void
    rawInsert(std::shared_ptr<SLE> const& sle) override;

    void
    rawErase(uint256 const& key);

    void
    rawReplace(std::shared_ptr<SLE> const& sle) override;

    void
    rawDestroyXRP(XRPAmount const& fee) override
    {
        info_.drops -= fee;
    }

    //
    // TxsRawView
    //

    void
    rawTxInsert(
        uint256 const& key,
        std::shared_ptr<Serializer const> const& txn,
        std::shared_ptr<Serializer const> const& metaData) override;

    // Insert the transaction, and return the hash of the SHAMap leaf node
    // holding the transaction. The hash can be used to fetch the transaction
    // directly, instead of traversing the SHAMap
    // @param key transaction ID
    // @param txn transaction
    // @param metaData transaction metadata
    // @return hash of SHAMap leaf node that holds the transaction
    uint256
    rawTxInsertWithHash(
        uint256 const& key,
        std::shared_ptr<Serializer const> const& txn,
        std::shared_ptr<Serializer const> const& metaData);

    //--------------------------------------------------------------------------

    void
    setValidated() const
    {
        info_.validated = true;
    }

    void
    setAccepted(
        NetClock::time_point closeTime,
        NetClock::duration closeResolution,
        bool correctCloseTime,
        Config const& config);

    void
    setImmutable(Config const& config, bool rehash = true);

    bool
    isImmutable() const
    {
        return mImmutable;
    }

    /*  Mark this ledger as "should be full".

        "Full" is metadata property of the ledger, it indicates
        that the local server wants all the corresponding nodes
        in durable storage.

        This is marked `const` because it reflects metadata
        and not data that is in common with other nodes on the
        network.
    */
    void
    setFull() const
    {
        txMap_->setFull();
        stateMap_->setFull();
        txMap_->setLedgerSeq(info_.seq);
        stateMap_->setLedgerSeq(info_.seq);
    }

    void
    setTotalDrops(std::uint64_t totDrops)
    {
        info_.drops = totDrops;
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
    bool
    addSLE(SLE const& sle);

    //--------------------------------------------------------------------------

    void
    updateSkipList();

    bool
    walkLedger(beast::Journal j) const;

    bool
    assertSensible(beast::Journal ledgerJ) const;

    void
    invariants() const;
    void
    unshare() const;

    /**
     * get Negative UNL validators' master public keys
     *
     * @return the public keys
     */
    hash_set<PublicKey>
    negativeUNL() const;

    /**
     * get the to be disabled validator's master public key if any
     *
     * @return the public key if any
     */
    std::optional<PublicKey>
    validatorToDisable() const;

    /**
     * get the to be re-enabled validator's master public key if any
     *
     * @return the public key if any
     */
    std::optional<PublicKey>
    validatorToReEnable() const;

    /**
     * update the Negative UNL ledger component.
     * @note must be called at and only at flag ledgers
     *       must be called before applying UNLModify Tx
     */
    void
    updateNegativeUNL();

    /** Returns true if the ledger is a flag ledger */
    bool
    isFlagLedger() const;

    /** Returns true if the ledger directly precedes a flag ledger */
    bool
    isVotingLedger() const;

    std::shared_ptr<SLE>
    peek(Keylet const& k) const;

private:
    class sles_iter_impl;
    class txs_iter_impl;

    bool
    setup(Config const& config);

    bool mImmutable;

    std::shared_ptr<SHAMap> txMap_;
    std::shared_ptr<SHAMap> stateMap_;

    // Protects fee variables
    std::mutex mutable mutex_;

    Fees fees_;
    Rules rules_;
    LedgerInfo info_;
};

/** A ledger wrapped in a CachedView. */
using CachedLedger = CachedView<Ledger>;

std::uint32_t constexpr FLAG_LEDGER_INTERVAL = 256;
/** Returns true if the given ledgerIndex is a flag ledgerIndex */
bool
isFlagLedger(LedgerIndex seq);

//------------------------------------------------------------------------------
//
// API
//
//------------------------------------------------------------------------------

extern bool
pendSaveValidated(
    Application& app,
    std::shared_ptr<Ledger const> const& ledger,
    bool isSynchronous,
    bool isCurrent);

extern std::shared_ptr<Ledger>
loadByIndex(std::uint32_t ledgerIndex, Application& app, bool acquire = true);

extern std::tuple<std::shared_ptr<Ledger>, std::uint32_t, uint256>
loadLedgerHelper(
    std::string const& sqlSuffix,
    Application& app,
    bool acquire = true);

extern std::shared_ptr<Ledger>
loadByHash(uint256 const& ledgerHash, Application& app, bool acquire = true);

extern uint256
getHashByIndex(std::uint32_t index, Application& app);

extern bool
getHashesByIndex(
    std::uint32_t index,
    uint256& ledgerHash,
    uint256& parentHash,
    Application& app);

extern std::map<std::uint32_t, std::pair<uint256, uint256>>
getHashesByIndex(std::uint32_t minSeq, std::uint32_t maxSeq, Application& app);

// Fetch the ledger with the highest sequence contained in the database
extern std::tuple<std::shared_ptr<Ledger>, std::uint32_t, uint256>
getLatestLedger(Application& app);

// *** Reporting Mode Only ***
// Fetch all of the transactions contained in ledger from the nodestore.
// The transactions are fetched directly as a batch, instead of traversing the
// transaction SHAMap. Fetching directly is significantly faster than
// traversing, as there are less database reads, and all of the reads can
// executed concurrently. This function only works in reporting mode.
// @param ledger the ledger for which to fetch the contained transactions
// @param app reference to the Application
// @return vector of (transaction, metadata) pairs
extern std::vector<
    std::pair<std::shared_ptr<STTx const>, std::shared_ptr<STObject const>>>
flatFetchTransactions(ReadView const& ledger, Application& app);

// *** Reporting Mode Only ***
// For each nodestore hash, fetch the transaction.
// The transactions are fetched directly as a batch, instead of traversing the
// transaction SHAMap. Fetching directly is significantly faster than
// traversing, as there are less database reads, and all of the reads can
// executed concurrently. This function only works in reporting mode.
// @param nodestoreHashes hashes of the transactions to fetch
// @param app reference to the Application
// @return vector of (transaction, metadata) pairs
extern std::vector<
    std::pair<std::shared_ptr<STTx const>, std::shared_ptr<STObject const>>>
flatFetchTransactions(Application& app, std::vector<uint256>& nodestoreHashes);

/** Deserialize a SHAMapItem containing a single STTx

    Throw:

        May throw on deserializaton error
*/
std::shared_ptr<STTx const>
deserializeTx(SHAMapItem const& item);

/** Deserialize a SHAMapItem containing STTx + STObject metadata

    The SHAMap must contain two variable length
    serialization objects.

    Throw:

        May throw on deserializaton error
*/
std::pair<std::shared_ptr<STTx const>, std::shared_ptr<STObject const>>
deserializeTxPlusMeta(SHAMapItem const& item);

uint256
calculateLedgerHash(LedgerInfo const& info);

}  // namespace ripple

#endif
