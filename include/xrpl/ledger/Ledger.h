#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/CachedView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/shamap/SHAMap.h>

namespace xrpl {

class ServiceRegistry;
class Job;
class TransactionMaster;

class SqliteStatement;

struct CreateGenesisT
{
    explicit CreateGenesisT() = default;
};
extern CreateGenesisT const kCreateGenesis;

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

    Ledger(Ledger&&) = delete;
    Ledger&
    operator=(Ledger&&) = delete;

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
        CreateGenesisT,
        Rules rules,
        Fees const& fees,
        std::vector<uint256> const& amendments,
        Family& family);

    Ledger(LedgerHeader const& info, Rules rules, Family& family);

    /** Used for ledgers loaded from JSON files

        @param acquire If true, acquires the ledger if not found locally

        @note The fees parameter provides default values, but setup() may
              override them from the ledger state if fee-related SLEs exist.
    */
    Ledger(
        LedgerHeader const& info,
        bool& loaded,
        bool acquire,
        Rules rules,
        Fees const& fees,
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
        Rules rules,
        Fees const& fees,
        Family& family);

    ~Ledger() override = default;

    //
    // ReadView
    //

    bool
    open() const override
    {
        return false;
    }

    LedgerHeader const&
    header() const override
    {
        return header_;
    }

    void
    setLedgerInfo(LedgerHeader const& info)
    {
        header_ = info;
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
    succ(uint256 const& key, std::optional<uint256> const& last = std::nullopt) const override;

    SLE::const_pointer
    read(Keylet const& k) const override;

    std::unique_ptr<SlesType::iter_base>
    slesBegin() const override;

    std::unique_ptr<SlesType::iter_base>
    slesEnd() const override;

    std::unique_ptr<SlesType::iter_base>
    slesUpperBound(uint256 const& key) const override;

    std::unique_ptr<TxsType::iter_base>
    txsBegin() const override;

    std::unique_ptr<TxsType::iter_base>
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
    rawErase(SLE::ref sle) override;

    void
    rawInsert(SLE::ref sle) override;

    void
    rawErase(uint256 const& key);

    void
    rawReplace(SLE::ref sle) override;

    void
    rawDestroyXRP(XRPAmount const& fee) override
    {
        header_.drops -= fee;
    }

    //
    // TxsRawView
    //

    void
    rawTxInsert(
        uint256 const& key,
        std::shared_ptr<Serializer const> const& txn,
        std::shared_ptr<Serializer const> const& metaData) override;

    //--------------------------------------------------------------------------

    void
    setValidated() const
    {
        header_.validated = true;
    }

    void
    setAccepted(
        NetClock::time_point closeTime,
        NetClock::duration closeResolution,
        bool correctCloseTime);

    void
    setImmutable(bool rehash = true);

    bool
    isImmutable() const
    {
        return immutable_;
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
        txMap_.setFull();
        txMap_.setLedgerSeq(header_.seq);
        stateMap_.setFull();
        stateMap_.setLedgerSeq(header_.seq);
    }

    void
    setTotalDrops(std::uint64_t totDrops)
    {
        header_.drops = totDrops;
    }

    SHAMap const&
    stateMap() const
    {
        return stateMap_;
    }

    SHAMap&
    stateMap()
    {
        return stateMap_;
    }

    SHAMap const&
    txMap() const
    {
        return txMap_;
    }

    SHAMap&
    txMap()
    {
        return txMap_;
    }

    // returns false on error
    bool
    addSLE(SLE const& sle);

    //--------------------------------------------------------------------------

    void
    updateSkipList();

    bool
    walkLedger(beast::Journal j, bool parallel = false) const;

    bool
    isSensible() const;

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

    SLE::pointer
    peek(Keylet const& k) const;

private:
    class SlesIterImpl;
    class TxsIterImpl;

    bool
    setup();

    /** @brief Deserialize a SHAMapItem containing a single STTx.
     *
     * @param item The SHAMapItem to deserialize.
     * @return A shared pointer to the deserialized transaction.
     * @throw May throw on deserialization error.
     */
    static std::shared_ptr<STTx const>
    deserializeTx(SHAMapItem const& item);

    /** @brief Deserialize a SHAMapItem containing STTx + STObject metadata.
     *
     * The SHAMapItem must contain two variable length serialization objects.
     *
     * @param item The SHAMapItem to deserialize.
     * @return A pair containing shared pointers to the deserialized transaction
     *         and metadata.
     * @throw May throw on deserialization error.
     */
    static std::pair<std::shared_ptr<STTx const>, std::shared_ptr<STObject const>>
    deserializeTxPlusMeta(SHAMapItem const& item);

    bool immutable_;

    // A SHAMap containing the transactions associated with this ledger.
    SHAMap mutable txMap_;

    // A SHAMap containing the state objects for this ledger.
    SHAMap mutable stateMap_;

    // Protects fee variables
    std::mutex mutable mutex_;

    Fees fees_;
    Rules rules_;
    LedgerHeader header_;
    beast::Journal j_;
};

/** A ledger wrapped in a CachedView. */
using CachedLedger = CachedView<Ledger>;

}  // namespace xrpl
