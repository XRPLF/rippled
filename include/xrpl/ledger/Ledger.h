/** @file
 *  Declares the Ledger class — the central data structure of the XRP Ledger
 *  daemon — together with supporting types for genesis ledger construction
 *  and the CachedLedger alias.
 */

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

/** Tag type used to select the genesis-ledger constructor of Ledger.
 *
 *  Pass the singleton `kCREATE_GENESIS` constant to construct ledger
 *  sequence 1.  The explicit constructor prevents accidental conversions.
 */
struct CreateGenesisT
{
    explicit CreateGenesisT() = default;
};
/** Singleton tag constant passed to the genesis-ledger constructor. */
extern CreateGenesisT const kCREATE_GENESIS;

/** Immutable or mutable snapshot of the XRP Ledger at a single sequence number.
 *
 *  A Ledger owns two SHAMap Merkle–radix trees: `stateMap_` (all account
 *  state — account roots, trust lines, offers, escrows, amendments, fee
 *  settings, etc.) and `txMap_` (every transaction together with its
 *  execution metadata that produced this ledger's state).
 *
 *  **Mutable/immutable lifecycle:**
 *  - A freshly constructed ledger begins mutable; it must not be shared
 *    across threads while mutable.
 *  - After `setImmutable()` is called the ledger hashes are finalised,
 *    both SHAMaps are locked, and the object may be shared freely without
 *    any locking.  Any attempt to mutate the SHAMaps after this point will
 *    assert.
 *  - `setAccepted()` is the standard close-time + `setImmutable()` sequence
 *    used after consensus.
 *
 *  The class inherits `DigestAwareReadView` (read + per-entry digest),
 *  `TxsRawView` (raw state and transaction mutation), and
 *  `CountedObject<Ledger>` (intrusive diagnostics).  It is marked `final`
 *  because constructors call virtual functions through `setup()`.
 *
 *  @note Presented to most callers through the `ReadView` interface.
 *  @note `txMap_` and `stateMap_` are declared `mutable` to allow
 *      `setFull()` and iterator operations in `const` contexts without
 *      compromising the logical-constness contract.
 *  @see CachedLedger — the standard shareable form used at rest.
 */
class Ledger final : public std::enable_shared_from_this<Ledger>,
                     public DigestAwareReadView,
                     public TxsRawView,
                     public CountedObject<Ledger>
{
public:
    /** Copying and moving are prohibited.
     *
     *  Ledger objects are always owned through `std::shared_ptr`.  Shared
     *  ownership combined with the mutable-→-immutable transition makes
     *  value-semantic copies unsafe and unnecessary.
     */
    Ledger(Ledger const&) = delete;
    Ledger&
    operator=(Ledger const&) = delete;

    Ledger(Ledger&&) = delete;
    Ledger&
    operator=(Ledger&&) = delete;

    /** Construct ledger sequence 1 (the genesis ledger).
     *
     *  Seeds a single master account whose `AccountID` is derived
     *  deterministically from the seed of `"masterpassphrase"`, credits it
     *  with `kINITIAL_XRP` drops, inserts the `sfAmendments` SLE for any
     *  pre-enabled amendments, and inserts the fee schedule SLE using either
     *  drop-native fields (`sfBaseFeeDrops`, etc.) when `featureXRPFees` is
     *  among `amendments`, or legacy integer fields otherwise.  Ends with
     *  `setImmutable()`.
     *
     *  @param rules   Protocol rules in effect at genesis.
     *  @param fees    Initial fee schedule (base fee, reserve, increment).
     *  @param amendments  Amendments that are enabled from ledger 1 onward.
     *      Determines which fee-field format is used for the genesis fee SLE.
     *  @param family  Node-store family that owns the SHAMap backing storage.
     */
    Ledger(
        CreateGenesisT,
        Rules rules,
        Fees const& fees,
        std::vector<uint256> const& amendments,
        Family& family);

    /** Construct an immutable header-only placeholder ledger.
     *
     *  Creates SHAMaps initialised with the root hashes from `info` but does
     *  not attempt to fetch SHAMap nodes from the node store.  The canonical
     *  ledger hash is computed immediately from the header fields.  Used for
     *  skeleton or partial ledgers reconstructed from database metadata.
     *
     *  @param info    Fully populated ledger header (must include root hashes).
     *  @param rules   Protocol rules in effect for this ledger.
     *  @param family  Node-store family for the underlying SHAMaps.
     */
    Ledger(LedgerHeader const& info, Rules rules, Family& family);

    /** Restore a ledger from its header, fetching SHAMap roots from the node store.
     *
     *  Constructs both SHAMaps with the root hashes from `info` and calls
     *  `fetchRoot()` on each.  If either root is absent from the node store,
     *  `loaded` is set to `false`; when `acquire` is also `true`, async
     *  acquisition is triggered via `family.missingNodeAcquireByHash()`.
     *  The resulting ledger is always immutable.
     *
     *  @param info    Ledger header, including `txHash` and `accountHash` roots.
     *  @param loaded  Set to `false` on return if either SHAMap root was missing.
     *  @param acquire If `true`, trigger async node acquisition when `loaded`
     *      would be set to `false`.
     *  @param rules   Protocol rules in effect for this ledger.
     *  @param fees    Default fee values; `setup()` will override these from the
     *      on-ledger fee SLE if one exists.
     *  @param family  Node-store family for the underlying SHAMaps.
     *  @param j       Journal for missing-root warnings.
     */
    Ledger(
        LedgerHeader const& info,
        bool& loaded,
        bool acquire,
        Rules rules,
        Fees const& fees,
        Family& family,
        beast::Journal j);

    /** Create the next mutable ledger in the chain following `previous`.
     *
     *  The new ledger has sequence `previous.seq() + 1`.  Its `stateMap_`
     *  is a copy-on-write snapshot of `previous.stateMap_` so state changes
     *  do not affect the closed parent.  Its `txMap_` starts empty (a fresh
     *  SHAMap for the new round's transactions).  `parentCloseTime` is set
     *  to `previous.closeTime`; the close-time resolution is advanced via
     *  `getNextLedgerTimeResolution`.
     *
     *  @param previous  The preceding closed ledger; must be immutable.
     *  @param closeTime Proposed close time for the new ledger.
     */
    Ledger(Ledger const& previous, NetClock::time_point closeTime);

    /** Construct a mutable empty ledger for database reconstruction.
     *
     *  Creates an empty, mutable ledger at `ledgerSeq` and calls `setup()`
     *  to initialise `fees_` and `rules_` from any state entries that may
     *  already exist.  Used when the node store needs to rebuild a ledger
     *  from raw DB data outside the normal consensus flow.
     *
     *  @param ledgerSeq  Target ledger sequence number.
     *  @param closeTime  Close time to record in the ledger header.
     *  @param rules      Protocol rules for this ledger.
     *  @param fees       Initial fee schedule (may be overridden by `setup()`).
     *  @param family     Node-store family for the underlying SHAMaps.
     */
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

    /** Always returns `false`; Ledger objects are never open. */
    bool
    open() const override
    {
        return false;
    }

    /** Returns the ledger header (sequence, hashes, close time, drops, etc.). */
    LedgerHeader const&
    header() const override
    {
        return header_;
    }

    /** Overwrite the in-memory ledger header wholesale.
     *
     *  Used during ledger reconstruction from external data before the
     *  ledger is made immutable.  Do not call on an immutable ledger.
     *
     *  @param info  New header to install.
     */
    void
    setLedgerInfo(LedgerHeader const& info)
    {
        header_ = info;
    }

    /** Returns the fee schedule parsed from the on-ledger fee SLE. */
    Fees const&
    fees() const override
    {
        return fees_;
    }

    /** Returns the protocol rules in effect for this ledger. */
    Rules const&
    rules() const override
    {
        return rules_;
    }

    /** Returns `true` if the state map contains an entry matching `k`.
     *
     *  @param k  Keylet identifying the ledger entry (type + key).
     */
    bool
    exists(Keylet const& k) const override;

    /** Returns `true` if the state map contains an entry at the raw key.
     *
     *  @param key  256-bit SHAMap key to look up (no type check).
     */
    bool
    exists(uint256 const& key) const;

    /** Find the smallest state-map key strictly greater than `key`.
     *
     *  @param key   Lower bound (exclusive) for the search.
     *  @param last  If set, keys >= `last` are not returned.
     *  @return The next key, or `std::nullopt` if none exists in range.
     */
    std::optional<uint256>
    succ(uint256 const& key, std::optional<uint256> const& last = std::nullopt) const override;

    /** Deserialize and return the state entry identified by `k`.
     *
     *  Checks the keylet type against the deserialized SLE; returns
     *  `nullptr` if the key is missing or the type check fails.
     *
     *  @param k  Keylet specifying the key and expected ledger-entry type.
     *  @return Shared pointer to the immutable SLE, or `nullptr`.
     */
    std::shared_ptr<SLE const>
    read(Keylet const& k) const override;

    /** Return a begin iterator over all state-map entries. */
    std::unique_ptr<SlesType::iter_base>
    slesBegin() const override;

    /** Return a past-the-end iterator over all state-map entries. */
    std::unique_ptr<SlesType::iter_base>
    slesEnd() const override;

    /** Return an iterator to the first state-map entry with key > `key`. */
    std::unique_ptr<SlesType::iter_base>
    slesUpperBound(uint256 const& key) const override;

    /** Return a begin iterator over all transaction-map entries.
     *
     *  @note Transactions are yielded with metadata for closed ledgers and
     *      without metadata for open ledgers (always closed for `Ledger`).
     */
    std::unique_ptr<TxsType::iter_base>
    txsBegin() const override;

    /** Return a past-the-end iterator over all transaction-map entries. */
    std::unique_ptr<TxsType::iter_base>
    txsEnd() const override;

    /** Returns `true` if the transaction map contains an entry for `key`. */
    bool
    txExists(uint256 const& key) const override;

    /** Deserialize and return the transaction (plus metadata) for `key`.
     *
     *  For a closed ledger both the `STTx` and the `STObject` metadata are
     *  returned.  Returns an empty pair if the key is not present.
     *
     *  @param key  Transaction ID to look up.
     *  @return Pair of `(STTx const*, STObject const*)` shared pointers;
     *      either or both may be null on miss.
     */
    tx_type
    txRead(key_type const& key) const override;

    //
    // DigestAwareReadView
    //

    /** Return the Merkle hash of the state-map leaf at `key`.
     *
     *  Used by `CachedView` to detect whether a cached SLE is stale.
     *  Returns `std::nullopt` if no entry exists at `key`.
     *
     *  @note The current implementation loads the SHAMap item from the node
     *      store as a side-effect; see the inline comment in `Ledger.cpp`.
     *
     *  @param key  256-bit state-map key to hash.
     *  @return The leaf node hash, or `std::nullopt` if absent.
     */
    std::optional<digest_type>
    digest(key_type const& key) const override;

    //
    // RawView
    //

    /** Remove the state entry whose key matches `sle->key()`.
     *
     *  Calls `logicError` if the key does not exist in the state map.
     *
     *  @param sle  Entry to remove; only the key is used.
     */
    void
    rawErase(std::shared_ptr<SLE> const& sle) override;

    /** Insert a new state entry for `sle`.
     *
     *  Serializes the SLE and adds it to the state SHAMap.  Calls
     *  `logicError` if an entry with the same key already exists.
     *
     *  @param sle  Entry to insert; must not already be present.
     */
    void
    rawInsert(std::shared_ptr<SLE> const& sle) override;

    /** Remove the state entry at the raw key `key`.
     *
     *  Overload for callers that hold only the key rather than an SLE.
     *  Calls `logicError` if the key does not exist.
     *
     *  @param key  256-bit state-map key of the entry to remove.
     */
    void
    rawErase(uint256 const& key);

    /** Replace (overwrite) an existing state entry with `sle`.
     *
     *  Serializes the SLE and updates the state SHAMap in place.  Calls
     *  `logicError` if no entry exists at `sle->key()`.
     *
     *  @param sle  Replacement entry; key must already be present.
     */
    void
    rawReplace(std::shared_ptr<SLE> const& sle) override;

    /** Burn `fee` drops from the ledger's total XRP supply.
     *
     *  Implements XRPL's deflationary model: transaction fees are
     *  permanently destroyed rather than redistributed.  Decrements
     *  `header_.drops` directly.
     *
     *  @param fee  Amount to deduct from the total coin supply.
     */
    void
    rawDestroyXRP(XRPAmount const& fee) override
    {
        header_.drops -= fee;
    }

    //
    // TxsRawView
    //

    /** Append a transaction + metadata blob to the transaction map.
     *
     *  Encodes `txn` and `metaData` as two back-to-back variable-length
     *  fields and inserts the result at `key`.  Asserts that `metaData`
     *  is non-null (open ledgers must not call this).  Calls `logicError`
     *  if `key` is already present (duplicate transaction).
     *
     *  @param key       Transaction ID (SHAMap key).
     *  @param txn       Serialized transaction blob.
     *  @param metaData  Serialized transaction metadata blob; must be non-null.
     */
    void
    rawTxInsert(
        uint256 const& key,
        std::shared_ptr<Serializer const> const& txn,
        std::shared_ptr<Serializer const> const& metaData) override;

    //--------------------------------------------------------------------------

    /** Mark this ledger as validated by the network.
     *
     *  Sets `header_.validated = true`.  This is a local-node annotation
     *  only; it does not affect the consensus hash or any on-ledger state.
     */
    void
    setValidated() const
    {
        header_.validated = true;
    }

    /** Finalise timing fields and transition this ledger to immutable.
     *
     *  Records `closeTime`, `closeResolution`, and the close-flag
     *  (`kS_LCF_NO_CONSENSUS_TIME` when `correctCloseTime` is `false`),
     *  then delegates to `setImmutable()`.
     *
     *  @pre `!open()` — the ledger must already be closed.
     *
     *  @param closeTime        Agreed consensus close time.
     *  @param closeResolution  Resolution used to bin the close time.
     *  @param correctCloseTime `true` if consensus agreed on the close time;
     *      `false` sets the no-consensus-time flag in the header.
     */
    void
    setAccepted(
        NetClock::time_point closeTime,
        NetClock::duration closeResolution,
        bool correctCloseTime);

    /** Compute hashes and lock the ledger against further mutation.
     *
     *  When `rehash` is `true` (the default): computes `header_.txHash`
     *  and `header_.accountHash` from the respective SHAMap roots, then
     *  computes the canonical ledger hash via `calculateLedgerHash()`.
     *  Regardless of `rehash`, sets `immutable_ = true`, calls
     *  `setImmutable()` on both SHAMaps, and calls `setup()` to populate
     *  `fees_` and `rules_` from the state map.
     *
     *  @param rehash  If `false`, skip hash computation (used when the
     *      hashes are already known, e.g. on load from the database).
     */
    void
    setImmutable(bool rehash = true);

    /** Returns `true` if `setImmutable()` has been called on this ledger. */
    bool
    isImmutable() const
    {
        return immutable_;
    }

    /** Tell the node store to retain all SHAMap nodes for this ledger.
     *
     *  "Full" is a local storage policy: when set, the node store will keep
     *  all state-map and transaction-map nodes for this ledger in durable
     *  storage rather than evicting them.  Declared `const` because fullness
     *  is node-local metadata — two nodes holding the same ledger may differ
     *  on this property without affecting consensus.
     */
    void
    setFull() const
    {
        txMap_.setFull();
        txMap_.setLedgerSeq(header_.seq);
        stateMap_.setFull();
        stateMap_.setLedgerSeq(header_.seq);
    }

    /** Overwrite the total XRP supply recorded in the ledger header.
     *
     *  Used when building ledgers from external data sources (e.g. JSON
     *  import) before the ledger is made immutable.
     *
     *  @param totDrops  New total supply in drops.
     */
    void
    setTotalDrops(std::uint64_t totDrops)
    {
        header_.drops = totDrops;
    }

    /** Returns a read-only reference to the state SHAMap. */
    SHAMap const&
    stateMap() const
    {
        return stateMap_;
    }

    /** Returns a mutable reference to the state SHAMap.
     *
     *  @note Only valid while the ledger is mutable.
     */
    SHAMap&
    stateMap()
    {
        return stateMap_;
    }

    /** Returns a read-only reference to the transaction SHAMap. */
    SHAMap const&
    txMap() const
    {
        return txMap_;
    }

    /** Returns a mutable reference to the transaction SHAMap.
     *
     *  @note Only valid while the ledger is mutable.
     */
    SHAMap&
    txMap()
    {
        return txMap_;
    }

    /** Serialize `sle` and add it directly to the state SHAMap.
     *
     *  Convenience wrapper used during ledger construction from external
     *  data sources.  Unlike `rawInsert`, this does not assert on failure.
     *
     *  @param sle  State entry to serialize and insert.
     *  @return `true` on success; `false` if the key already exists or the
     *      underlying `SHAMap::addItem` call fails.
     */
    bool
    addSLE(SLE const& sle);

    //--------------------------------------------------------------------------

    /** Update the two-tier skip list stored in the state map.
     *
     *  The skip list enables O(1) historical hash lookup.  This method
     *  maintains two SLEs:
     *  - `keylet::skip(prevIndex)` — a permanent record written for every
     *    256-aligned predecessor sequence; stores up to 256 ancestor hashes.
     *  - `keylet::skip()` — a rolling window of the 256 most recent parent
     *    hashes; oldest entry is evicted when the list is full.
     *
     *  Must be called on a mutable ledger before `setImmutable()`.
     */
    void
    updateSkipList();

    /** Verify that every SHAMap node for this ledger is reachable.
     *
     *  Walks both the state map and the transaction map and collects missing
     *  node reports.  Logs the first missing node of each type to `j`.
     *
     *  @param j        Journal to receive missing-node diagnostics.
     *  @param parallel If `true`, walks the state map using parallel
     *      traversal (faster on multi-core hardware).
     *  @return `true` if both maps are fully present; `false` if any nodes
     *      are missing.
     */
    bool
    walkLedger(beast::Journal j, bool parallel = false) const;

    /** Perform basic sanity checks on the ledger header vs. SHAMap hashes.
     *
     *  Verifies that `header_.hash`, `header_.accountHash`, and
     *  `header_.txHash` are all non-zero and that the account and
     *  transaction hashes match the actual SHAMap roots.
     *
     *  @return `true` if all checks pass.
     */
    bool
    isSensible() const;

    /** Assert internal SHAMap invariants for both the state and tx maps.
     *
     *  Delegates to `SHAMap::invariants()` on each map.  Intended for
     *  debug-build integrity checks.
     */
    void
    invariants() const;

    /** Release copy-on-write sharing of SHAMap nodes.
     *
     *  After a copy-on-write snapshot is made (e.g. in the successor
     *  constructor), internal SHAMap nodes may be shared between the parent
     *  and child ledgers.  Calling `unshare()` on the mutable child forces
     *  a deep copy so the two trees are fully independent.
     */
    void
    unshare() const;

    /** Read the current set of Negative UNL validators from the state map.
     *
     *  The Negative UNL is a consensus mechanism that temporarily removes
     *  chronically offline validators without breaking liveness.  This
     *  method reads the `sfDisabledValidators` array from the
     *  `keylet::negativeUNL()` SLE.
     *
     *  @return Master public keys of all currently disabled validators;
     *      empty if no Negative UNL entry exists or it has no members.
     */
    hash_set<PublicKey>
    negativeUNL() const;

    /** Return the validator scheduled for disabling at the next flag ledger.
     *
     *  Reads `sfValidatorToDisable` from the Negative UNL SLE, if present.
     *
     *  @return The validator's master public key, or `std::nullopt` if none
     *      is pending.
     */
    std::optional<PublicKey>
    validatorToDisable() const;

    /** Return the validator scheduled for re-enabling at the next flag ledger.
     *
     *  Reads `sfValidatorToReEnable` from the Negative UNL SLE, if present.
     *
     *  @return The validator's master public key, or `std::nullopt` if none
     *      is pending.
     */
    std::optional<PublicKey>
    validatorToReEnable() const;

    /** Apply the pending Negative UNL changes recorded in the state map.
     *
     *  Promotes `sfValidatorToDisable` into `sfDisabledValidators` and
     *  removes `sfValidatorToReEnable` from that array.  If the resulting
     *  disabled set is empty, the entire Negative UNL SLE is deleted.
     *
     *  @note Must be called exactly once per flag ledger (sequence divisible
     *      by 256) and *before* any `UNLModify` transaction is applied.
     */
    void
    updateNegativeUNL();

    /** Returns `true` if this is a flag ledger (sequence divisible by 256).
     *
     *  Flag ledgers carry out amendment votes, fee votes, and Negative UNL
     *  updates.  These actions must not occur on non-flag ledgers.
     */
    bool
    isFlagLedger() const;

    /** Returns `true` if this ledger directly precedes a flag ledger.
     *
     *  Voting ledgers (flagSeq − 1) are where validators cast their
     *  amendment and fee preferences before the flag-ledger processing pass.
     */
    bool
    isVotingLedger() const;

    /** Deserialize and return a mutable SLE at keylet `k`.
     *
     *  Unlike `read()`, the returned SLE is not `const` and may be passed
     *  to `rawReplace()` or `rawErase()`.  Returns `nullptr` if the key
     *  is absent or the keylet type check fails.
     *
     *  @note The caller must use the returned pointer only with the same
     *      `Ledger` instance; crossing to another view is a `LogicError`.
     *
     *  @param k  Keylet identifying the entry.
     *  @return Mutable SLE, or `nullptr` if not found.
     */
    std::shared_ptr<SLE>
    peek(Keylet const& k) const;

private:
    /** SHAMap-backed iterator implementation for `ReadView::sles`. */
    class SlesIterImpl;

    /** SHAMap-backed iterator implementation for `ReadView::txs`.
     *
     *  Deserializes with metadata for closed ledgers, without for open ones.
     */
    class TxsIterImpl;

    /** Populate `fees_` and `rules_` from the current state map.
     *
     *  Reads `keylet::fees()` and applies the fee fields to `fees_`, then
     *  rebuilds `rules_` via `makeRulesGivenLedger`.  Returns `false` if a
     *  `SHAMapMissingNode` is caught or if the fee SLE contains an illegal
     *  combination of old and new fee fields; otherwise returns `true`.
     *
     *  @note Called by every constructor and by `setImmutable()`.
     */
    bool
    setup();

    /** Deserialize a SHAMapItem containing a single `STTx`.
     *
     *  Used by `TxsIterImpl` for open ledgers (no metadata).
     *
     *  @param item  The SHAMap leaf to deserialize.
     *  @return Shared pointer to the deserialized transaction.
     *  @throw May throw on deserialization error.
     */
    static std::shared_ptr<STTx const>
    deserializeTx(SHAMapItem const& item);

    /** Deserialize a SHAMapItem containing an `STTx` followed by `STObject` metadata.
     *
     *  The item must encode two back-to-back variable-length fields: the
     *  serialized transaction blob first, then the metadata blob.
     *
     *  @param item  The SHAMap leaf to deserialize.
     *  @return Pair of shared pointers to the transaction and its metadata.
     *  @throw May throw on deserialization error.
     */
    static std::pair<std::shared_ptr<STTx const>, std::shared_ptr<STObject const>>
    deserializeTxPlusMeta(SHAMapItem const& item);

    /** `true` after `setImmutable()` has been called; mutations are forbidden. */
    bool immutable_;

    /** Merkle–radix tree of transactions + metadata keyed by transaction ID.
     *
     *  Declared `mutable` so `setFull()` and iterator accessors can be
     *  called in `const` contexts without violating logical immutability.
     */
    SHAMap mutable txMap_;

    /** Merkle–radix tree of all ledger state entries (SLEs) keyed by their
     *  256-bit key.
     *
     *  Declared `mutable` for the same reason as `txMap_`.
     */
    SHAMap mutable stateMap_;

    /** Guards `fees_` during the narrow mutable window before `setImmutable()`
     *  completes; not held on the read path once the ledger is immutable.
     */
    std::mutex mutable mutex_;

    Fees fees_;         /**< Fee schedule parsed from the on-ledger fee SLE. */
    Rules rules_;       /**< Protocol rules derived from enabled amendments. */
    LedgerHeader header_; /**< Sequence, hashes, close time, coin supply, etc. */
    beast::Journal j_;  /**< Journal for constructor and `setup()` diagnostics. */
};

/** Standard shareable ledger type used at rest in most of the server.
 *
 *  `CachedView<Ledger>` layers an `unordered_map` in front of the raw
 *  `Ledger`, caching deserialized SLEs by key so that frequently accessed
 *  state entries are not repeatedly deserialized from the SHAMap.  This is
 *  the type that callers such as the transaction engine and RPC handlers
 *  typically hold, not a raw `Ledger`.
 *
 *  @see CachedView
 */
using CachedLedger = CachedView<Ledger>;

}  // namespace xrpl
