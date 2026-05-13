/** @file
 *  Defines the foundational read-only ledger view interface.
 *
 *  `ReadView` is the base of the entire ledger view hierarchy. Every concrete
 *  ledger representation — finalized `Ledger`, in-progress `OpenView`, apply-time
 *  `Sandbox`, or payment-path `PaymentSandbox` — exposes its state through this
 *  interface. Code that only reads ledger data can operate on any view type without
 *  knowing the concrete implementation.
 *
 *  `DigestAwareReadView` extends `ReadView` with per-entry cryptographic digests,
 *  used by `CachedView` for efficient cache invalidation and by `makeRulesGivenLedger`
 *  to detect amendment changes between ledger closes.
 */

#pragma once

#include <xrpl/basics/chrono.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/ledger/detail/ReadViewFwdRange.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>

#include <cstdint>
#include <optional>
#include <unordered_set>

namespace xrpl {

//------------------------------------------------------------------------------

/** Pure abstract read-only interface to a ledger.
 *
 *  Exposes two conceptually distinct maps: the **state map** (SLEs keyed by
 *  `uint256`) and the **transaction map** (committed transactions with metadata).
 *  Concrete implementations include `Ledger` (finalized), `OpenView` (in-progress),
 *  `Sandbox` (discardable apply-time copy), and `PaymentSandbox` (payment engine).
 *
 *  @note Copy and move constructors explicitly re-initialize `sles` and `txs`
 *      with `*this`. Both members store a raw pointer to their owning view; a
 *      default memberwise copy would leave them pointing at the source object.
 *      Assignment operators are deleted for the same reason.
 */
class ReadView
{
public:
    /** Pair of transaction and its associated metadata object.
     *
     *  The metadata `STObject` is empty for open ledgers, since metadata is
     *  only finalized at ledger close time.
     */
    using tx_type = std::pair<std::shared_ptr<STTx const>, std::shared_ptr<STObject const>>;

    /** Raw key type for state-map and transaction-map lookups. */
    using key_type = uint256;

    /** Shared ownership handle to a non-modifiable state entry. */
    using mapped_type = std::shared_ptr<SLE const>;

    /** STL-compatible forward range over the ledger state map.
     *
     *  Iterates all SLEs present in this view. Backed by type-erased
     *  `ReadViewFwdIter` so the same interface works across SHAMap-backed,
     *  delta-list, and sandbox views. `upperBound` enables sub-range scans
     *  without a full traversal.
     *
     *  @note Visiting every state entry can be expensive as the ledger grows.
     */
    struct SlesType : detail::ReadViewFwdRange<std::shared_ptr<SLE const>>
    {
        explicit SlesType(ReadView const& view);
        [[nodiscard]] Iterator
        begin() const;
        [[nodiscard]] Iterator
        end() const;
        /** Returns an iterator to the first SLE whose key is strictly greater than @p key. */
        [[nodiscard]] Iterator
        upperBound(key_type const& key) const;
    };

    /** STL-compatible forward range over the ledger transaction map.
     *
     *  Iterates all `tx_type` pairs (transaction + metadata) present in
     *  this view. For open ledgers the metadata member of each pair is empty.
     */
    struct TxsType : detail::ReadViewFwdRange<tx_type>
    {
        explicit TxsType(ReadView const& view);
        /** Returns `true` when the transaction map contains no entries. */
        [[nodiscard]] bool
        empty() const;
        [[nodiscard]] Iterator
        begin() const;
        [[nodiscard]] Iterator
        end() const;
    };

    virtual ~ReadView() = default;

    ReadView&
    operator=(ReadView&& other) = delete;
    ReadView&
    operator=(ReadView const& other) = delete;

    /** Constructs the view and binds `sles` and `txs` to `*this`. */
    ReadView() : sles(*this), txs(*this)
    {
    }

    /** Copy-constructs the view, re-binding `sles` and `txs` to `*this`.
     *
     *  @note The `sles` and `txs` members store a pointer to their owning
     *      view. They are explicitly re-initialized here to point at the new
     *      object, not at `other`.
     */
    ReadView(ReadView const& other) : sles(*this), txs(*this)
    {
    }

    /** Move-constructs the view, re-binding `sles` and `txs` to `*this`.
     *
     *  @note Same aliasing concern as the copy constructor; `sles` and `txs`
     *      are explicitly re-initialized to point at the new object.
     */
    ReadView(ReadView&& other) : sles(*this), txs(*this)
    {
    }

    /** Returns the immutable header fields for this ledger.
     *
     *  All non-virtual convenience accessors (`seq()`, `parentCloseTime()`)
     *  delegate here, keeping the virtual dispatch surface minimal.
     */
    [[nodiscard]] virtual LedgerHeader const&
    header() const = 0;

    /** Returns `true` if this view reflects an open (not yet closed) ledger. */
    [[nodiscard]] virtual bool
    open() const = 0;

    /** Returns the close time of the previous (parent) ledger. */
    [[nodiscard]] NetClock::time_point
    parentCloseTime() const
    {
        return header().parentCloseTime;
    }

    /** Returns the sequence number of this ledger. */
    [[nodiscard]] LedgerIndex
    seq() const
    {
        return header().seq;
    }

    /** Returns the fee schedule in effect for this ledger. */
    [[nodiscard]] virtual Fees const&
    fees() const = 0;

    /** Returns the amendment rules active for this ledger. */
    [[nodiscard]] virtual Rules const&
    rules() const = 0;

    /** Returns `true` if a state entry matching the keylet is present.
     *
     *  The `Keylet` bundles a raw `uint256` key with its `LedgerEntryType`,
     *  allowing implementations to reject type mismatches without deserializing
     *  the entry. This makes `exists` more efficient than calling `read` when
     *  only presence is needed.
     *
     *  @param k The keylet (key + expected entry type) to probe.
     *  @return `true` if an SLE with the given key and type exists.
     */
    [[nodiscard]] virtual bool
    exists(Keylet const& k) const = 0;

    /** Returns the smallest state-map key strictly greater than @p key.
     *
     *  Enables ordered range scans of the SHAMap without deserializing entries.
     *  If @p last is set, the search is bounded to the open interval
     *  `(key, last)` — any candidate key outside that range causes
     *  `std::nullopt` to be returned instead.
     *
     *  @param key  The key to search above.
     *  @param last Optional exclusive upper bound for the result.
     *  @return The next key, or `std::nullopt` if none exists within bounds.
     */
    [[nodiscard]] virtual std::optional<key_type>
    succ(key_type const& key, std::optional<key_type> const& last = std::nullopt) const = 0;

    /** Returns a read-only handle to the state entry identified by @p k.
     *
     *  Gives the caller shared ownership of a non-modifiable SLE. The `const`
     *  qualifier reflects this caller's view; the underlying object may be
     *  mutated through `ApplyView` in another code path.
     *
     *  @param k The keylet (key + expected entry type) to look up.
     *  @return Shared pointer to the SLE, or `nullptr` if the key is absent
     *      or the ledger entry type does not match the keylet.
     */
    [[nodiscard]] virtual std::shared_ptr<SLE const>
    read(Keylet const& k) const = 0;

    /** Adjusts an IOU balance to exclude assets acquired during the current payment.
     *
     *  The payment engine executes paths in reverse (destination-first), which
     *  means an account may be credited before it has redeemed the corresponding
     *  asset. Accounts must not spend assets acquired within the same payment.
     *  `PaymentSandbox` overrides this hook to subtract deferred credits recorded
     *  in its `DeferredCredits` table. The default implementation returns
     *  @p amount unchanged, making the hook zero-cost for non-payment views.
     *
     *  @param account The account whose balance is being queried.
     *  @param issuer  The IOU issuer.
     *  @param amount  The raw IOU balance (must hold `Issue`).
     *  @return The effective spendable balance after deducting deferred credits.
     */
    [[nodiscard]] virtual STAmount
    balanceHookIOU(AccountID const& account, AccountID const& issuer, STAmount const& amount) const
    {
        XRPL_ASSERT(amount.holds<Issue>(), "balanceHookIOU: amount is for Issue");

        return amount;
    }

    /** Adjusts an MPT balance to exclude assets acquired during the current payment.
     *
     *  Mirrors `balanceHookIOU` for MPT-denominated amounts. `PaymentSandbox`
     *  overrides this hook; the default implementation wraps @p amount in an
     *  `STAmount` and returns it unchanged.
     *
     *  @param account The account whose balance is being queried.
     *  @param issue   The MPT issuance.
     *  @param amount  The raw MPT balance as a signed 64-bit integer.
     *  @return The effective spendable balance after deducting deferred credits.
     */
    [[nodiscard]] virtual STAmount
    balanceHookMPT(AccountID const& account, MPTIssue const& issue, std::int64_t amount) const
    {
        return STAmount{issue, amount};
    }

    /** Adjusts the available issuance capacity for an issuer selling their own MPT.
     *
     *  An issuer's sell-offer for their own MPT is limited by their remaining
     *  issuance capacity (i.e., `MaximumAmount - OutstandingAmount`), reduced
     *  by any MPT already committed to self-issued sell offers during this payment.
     *  `PaymentSandbox` overrides this hook to track that self-debit; the default
     *  returns @p amount unchanged. Used by `issuerFundsToSelfIssue()`.
     *
     *  @param issue  The MPT issuance.
     *  @param amount The raw available-issuance amount.
     *  @return The effective capacity after accounting for in-flight self-sold amounts.
     */
    [[nodiscard]] virtual STAmount
    balanceHookSelfIssueMPT(MPTIssue const& issue, std::int64_t amount) const
    {
        return STAmount{issue, amount};
    }

    /** Returns the effective owner count, adjusted for in-payment reserve changes.
     *
     *  A payment could temporarily free reserves by consuming offers in intermediate
     *  steps, making it appear that an account has fewer owner-count obligations.
     *  `PaymentSandbox` overrides this hook to return the maximum owner count seen
     *  so far during the payment, preventing reserve-bypass exploits. The default
     *  implementation returns @p count unchanged.
     *
     *  @param account The account being queried.
     *  @param count   The current owner count from ledger state.
     *  @return The high-water-mark owner count for reserve purposes.
     */
    [[nodiscard]] virtual std::uint32_t
    ownerCountHook(AccountID const& account, std::uint32_t count) const
    {
        return count;
    }

    /** Returns a heap-allocated iterator positioned at the start of the state map.
     *
     *  Called by `SlesType::begin()`; not intended for direct use by callers.
     */
    [[nodiscard]] virtual std::unique_ptr<SlesType::iter_base>
    slesBegin() const = 0;

    /** Returns a heap-allocated sentinel iterator for the state map.
     *
     *  Called by `SlesType::end()`; not intended for direct use by callers.
     */
    [[nodiscard]] virtual std::unique_ptr<SlesType::iter_base>
    slesEnd() const = 0;

    /** Returns a heap-allocated iterator to the first SLE whose key is strictly greater than @p key.
     *
     *  Called by `SlesType::upperBound()`; not intended for direct use by callers.
     */
    [[nodiscard]] virtual std::unique_ptr<SlesType::iter_base>
    slesUpperBound(key_type const& key) const = 0;

    /** Returns a heap-allocated iterator positioned at the start of the transaction map.
     *
     *  Called by `TxsType::begin()`; not intended for direct use by callers.
     */
    [[nodiscard]] virtual std::unique_ptr<TxsType::iter_base>
    txsBegin() const = 0;

    /** Returns a heap-allocated sentinel iterator for the transaction map.
     *
     *  Called by `TxsType::end()`; not intended for direct use by callers.
     */
    [[nodiscard]] virtual std::unique_ptr<TxsType::iter_base>
    txsEnd() const = 0;

    /** Returns `true` if a transaction with the given key exists in the tx map.
     *
     *  A transaction is present if it is part of the base ledger or was
     *  inserted into this view's delta since the base.
     *
     *  @param key The transaction hash to probe.
     */
    [[nodiscard]] virtual bool
    txExists(key_type const& key) const = 0;

    /** Returns the transaction and its metadata for the given key.
     *
     *  For open ledgers the metadata `STObject` in the returned pair will be
     *  empty, since metadata is only finalized at close time.
     *
     *  @param key The transaction hash to look up.
     *  @return A `tx_type` pair where both pointers are `nullptr` if the key
     *      is not found in the transaction map.
     */
    [[nodiscard]] virtual tx_type
    txRead(key_type const& key) const = 0;

    //
    // Memberspaces
    //

    /** Iterable range over all state entries (SLEs) in this view.
     *
     *  @note Full traversal can be expensive on a large ledger. Use
     *      `upperBound` or `succ` for targeted sub-range scans.
     */
    SlesType sles;

    /** Iterable range over all transactions in this view. */
    TxsType txs;
};

//------------------------------------------------------------------------------

/** Extension of `ReadView` that provides per-entry cryptographic digests.
 *
 *  `Ledger` implements this interface cheaply by reading the hash directly
 *  from the SHAMap trie node without deserializing the leaf entry. Sandboxes
 *  and delta-views do not expose digests, which is why this capability is a
 *  separate subclass rather than part of `ReadView`.
 *
 *  Used by `CachedView` for two-level cache invalidation and by
 *  `makeRulesGivenLedger` to detect amendments changes across ledger closes.
 */
class DigestAwareReadView : public ReadView
{
public:
    using digest_type = uint256;

    DigestAwareReadView() = default;
    DigestAwareReadView(DigestAwareReadView const&) = default;

    /** Returns the cryptographic hash of the serialized state entry at @p key.
     *
     *  Implementations may return this without fully deserializing the entry.
     *
     *  @param key The raw state-map key to query.
     *  @return The entry's digest, or `std::nullopt` if no entry exists at that key.
     */
    [[nodiscard]] virtual std::optional<digest_type>
    digest(key_type const& key) const = 0;
};

//------------------------------------------------------------------------------

/** Constructs the active amendment `Rules` from a closed ledger, updating from existing rules.
 *
 *  Reads the `sfAmendments` field from the ledger's amendments object and passes
 *  its digest to the `Rules` constructor so that `Rules` can detect unchanged
 *  amendments between successive ledger closes without re-parsing. Requires a
 *  `DigestAwareReadView` because the optimization depends on querying the entry
 *  hash directly. Falls back to a default `Rules` object if the amendments object
 *  is absent.
 *
 *  @param ledger  The closed ledger to read amendments from.
 *  @param current The current rules object; its internal preset set is forwarded
 *      to the new `Rules` instance.
 *  @return A `Rules` object reflecting the amendments active in @p ledger.
 *  @see makeRulesGivenLedger(DigestAwareReadView const&, std::unordered_set<uint256, beast::Uhash<>> const&)
 */
Rules
makeRulesGivenLedger(DigestAwareReadView const& ledger, Rules const& current);

/** Constructs the active amendment `Rules` from a closed ledger using an explicit preset set.
 *
 *  Identical behavior to the `Rules const& current` overload but accepts
 *  the preset set directly. Used during initialization before a prior `Rules`
 *  object is available.
 *
 *  @param ledger   The closed ledger to read amendments from.
 *  @param presets  The set of always-enabled amendment flags to seed the rules object.
 *  @return A `Rules` object reflecting the amendments active in @p ledger.
 *  @see makeRulesGivenLedger(DigestAwareReadView const&, Rules const&)
 */
Rules
makeRulesGivenLedger(
    DigestAwareReadView const& ledger,
    std::unordered_set<uint256, beast::Uhash<>> const& presets);

}  // namespace xrpl

#include <xrpl/ledger/detail/ReadViewFwdRange.ipp>
