#pragma once

#include <xrpl/ledger/RawView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/detail/RawStateTable.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/XRPAmount.h>

#include <boost/container/pmr/monotonic_buffer_resource.hpp>
#include <boost/container/pmr/polymorphic_allocator.hpp>

#include <functional>
#include <utility>

namespace xrpl {

/** Tag type for constructing an open-ledger view.
 *
 *  Pass `kOPEN_LEDGER` to the `OpenView` constructor to build a fresh open
 *  ledger on top of a base. The header sequence is incremented, `parentHash`
 *  and `parentCloseTime` are derived from the base, and `validated`/`accepted`
 *  flags are cleared. Rules are supplied explicitly by the caller.
 *
 *  @see kOPEN_LEDGER
 */
inline constexpr struct OpenLedgerT
{
    explicit constexpr OpenLedgerT() = default;
} kOPEN_LEDGER{};

/** Tag type for constructing a batch-mode view.
 *
 *  Pass `kBATCH_VIEW` to the `OpenView` constructor when building a child view
 *  during batch transaction processing. The child wraps an existing `OpenView`
 *  and captures its current transaction count as `baseTxCount_`, so that
 *  `txCount()` ordinals remain globally unique and monotonically increasing
 *  within the enclosing ledger regardless of how many sub-views are stacked.
 *
 *  @see kBATCH_VIEW
 */
inline constexpr struct BatchViewT
{
    explicit constexpr BatchViewT() = default;
} kBATCH_VIEW{};

//------------------------------------------------------------------------------

/** Mutable ledger view used during transaction processing.
 *
 *  Implements the delta-accumulation pattern: holds an immutable base
 *  `ReadView` (typically the most recent closed ledger) and records all SLE
 *  mutations and inserted transactions as a pending diff on top of it.
 *  Nothing is written through to the base until `apply()` is called, making
 *  it safe to discard changes on failure.
 *
 *  State-object mutations are buffered in `items_` (`RawStateTable`). All
 *  `ReadView` queries merge the base and the pending diff transparently, so
 *  the apparent ledger state is always consistent. Transaction records are
 *  kept in `txs_` (a PMR `std::map`); open ledgers omit metadata while
 *  closed representations include it.
 *
 *  Both maps are backed by a 256 KB `monotonic_buffer_resource` for O(1)
 *  amortised allocation with no per-element heap overhead. The resource is
 *  a `unique_ptr` so move-construction maintains stable addressing for the
 *  maps' `polymorphic_allocator` raw pointers.
 *
 *  @note Move assignment and copy assignment are deleted; only move
 *      construction and copy construction are available.
 *  @note Callers holding `ReadView const*` see a coherent read-only snapshot
 *      that merges base state and pending modifications without needing to
 *      know whether the ledger is settled.
 */
class OpenView final : public ReadView, public TxsRawView
{
private:
    // Initial size for the monotonic_buffer_resource used for allocations
    // The size was chosen from the old `qalloc` code (which this replaces).
    // It is unclear how the size initially chosen in qalloc.
    static constexpr size_t kINITIAL_BUFFER_SIZE = kilobytes(256);

    class TxsIterImpl;

    struct TxData
    {
        std::shared_ptr<Serializer const> txn;
        std::shared_ptr<Serializer const> meta;

        // Constructor needed for emplacement in std::map
        TxData(
            std::shared_ptr<Serializer const> const& txn,
            std::shared_ptr<Serializer const> const& meta)
            : txn(txn), meta(meta)
        {
        }
    };

    // List of tx, key order
    // Use boost::pmr functionality instead of std::pmr
    // functions b/c clang does not support pmr yet (as-of 9/2020)
    using txs_map = std::map<
        key_type,
        TxData,
        std::less<key_type>,
        boost::container::pmr::polymorphic_allocator<std::pair<key_type const, TxData>>>;

    // monotonic_resource_ must outlive `items_`. Make a pointer so it may be
    // easily moved.
    std::unique_ptr<boost::container::pmr::monotonic_buffer_resource> monotonic_resource_;
    txs_map txs_;
    Rules rules_;
    LedgerHeader header_;
    ReadView const* base_;
    detail::RawStateTable items_;
    std::shared_ptr<void const> hold_;

    /// In batch mode, the number of transactions already executed.
    std::size_t baseTxCount_ = 0;

    bool open_ = true;

public:
    OpenView() = delete;
    OpenView&
    operator=(OpenView&&) = delete;
    OpenView&
    operator=(OpenView const&) = delete;

    OpenView(OpenView&&) = default;

    /** Construct a copy of this view with a fresh PMR arena.
     *
     *  The modification state table (`items_`) and transaction map (`txs_`)
     *  are copied into a newly allocated 256 KB monotonic buffer. `shared_ptr`
     *  members (SLEs, `hold_`) are shared with the source — they are not
     *  deep-copied — which is safe because SLEs are immutable once published.
     */
    OpenView(OpenView const&);

    /** Construct a fresh open ledger view on top of a closed base.
     *
     *  The header is derived from `base`: sequence is incremented by one,
     *  `parentCloseTime` is set to the base close time, `parentHash` is set
     *  to the base hash, and `validated`/`accepted` flags are cleared.
     *  The transaction list starts empty.
     *
     *  @param base The most recent closed ledger; must outlive this view
     *      unless `hold` is provided.
     *  @param rules Rules governing this open ledger; may differ from what
     *      the base recorded.
     *  @param hold Optional shared pointer keeping `base`'s backing object
     *      alive for the lifetime of this view.
     */
    OpenView(
        OpenLedgerT,
        ReadView const* base,
        Rules rules,
        std::shared_ptr<void const> hold = nullptr);

    /** Convenience overload that keeps the base alive via shared ownership.
     *
     *  Equivalent to the three-argument `OpenLedgerT` constructor, but takes
     *  a `shared_ptr` so the caller need not manage lifetime separately.
     *
     *  @param rules Rules governing this open ledger.
     *  @param base Shared pointer to the closed base ledger.
     */
    OpenView(OpenLedgerT, Rules const& rules, std::shared_ptr<ReadView const> const& base)
        : OpenView(kOPEN_LEDGER, &*base, rules, base)
    {
    }

    /** Construct a batch child view on top of an existing open ledger.
     *
     *  Wraps `base` as a read-through fallback and snapshots its current
     *  `txCount()` into `baseTxCount_`. This ensures that `txCount()` on this
     *  child continues from where the parent left off, preserving monotonically
     *  increasing apply-ordinals in transaction metadata.
     *
     *  @param base The parent `OpenView` to wrap; must outlive this child.
     */
    OpenView(BatchViewT, OpenView& base) : OpenView(std::addressof(base))
    {
        baseTxCount_ = base.txCount();
    }

    /** Construct a view representing a last-closed ledger.
     *
     *  Copies the `LedgerHeader` and `Rules` directly from `base`, and
     *  inherits its `open_` flag — so if the base was a closed ledger, this
     *  view will also report itself as closed. The transaction list starts
     *  empty.
     *
     *  @param base The source ledger; must outlive this view unless `hold`
     *      is provided.
     *  @param hold Optional shared pointer keeping `base`'s backing object
     *      alive for the lifetime of this view.
     */
    OpenView(ReadView const* base, std::shared_ptr<void const> hold = nullptr);

    /** Returns true if this view represents an open (not yet closed) ledger. */
    bool
    open() const override
    {
        return open_;
    }

    /** Return the total number of transactions applied since ledger construction.
     *
     *  Computed as `baseTxCount_ + txs_.size()`. In batch mode `baseTxCount_`
     *  captures the parent view's count at the time this child was constructed,
     *  so ordinals are globally unique and monotonically increasing even when
     *  child views are committed incrementally.
     *
     *  @return Number of transactions, used as the apply ordinal in metadata.
     */
    std::size_t
    txCount() const;

    /** Commit all accumulated changes to the target view.
     *
     *  Replays every buffered SLE mutation (`items_`) into `to` via
     *  `RawStateTable::apply`, then iterates `txs_` and calls
     *  `to.rawTxInsert()` for each transaction. The typical call site is
     *  `ApplyViewImpl::apply()`, which applies a per-transaction sandbox into
     *  the enclosing `OpenView`; later the `OpenView` itself is applied into
     *  the final ledger object.
     *
     *  @param to The target view that receives all mutations and transactions.
     */
    void
    apply(TxsRawView& to) const;

    // ReadView

    /** @return The current ledger header (sequence, hashes, close times). */
    LedgerHeader const&
    header() const override;

    /** @return The fee schedule inherited from the base ledger. */
    Fees const&
    fees() const override;

    /** @return The amendment rules supplied at construction or inherited from base. */
    Rules const&
    rules() const override;

    /** Check whether a ledger entry exists, merging base state and pending diff.
     *
     *  @param k Keylet identifying the entry.
     *  @return `true` if the entry exists in the merged view.
     */
    bool
    exists(Keylet const& k) const override;

    /** Return the smallest key strictly greater than `key` in the merged view.
     *
     *  @param key The lower bound (exclusive) to search from.
     *  @param last Optional upper bound (inclusive); search is bounded to
     *      `[key+1, last]` when provided.
     *  @return The next key, or `std::nullopt` if none exists in range.
     */
    std::optional<key_type>
    succ(key_type const& key, std::optional<key_type> const& last = std::nullopt) const override;

    /** Read a ledger entry from the merged view (base + pending diff).
     *
     *  @param k Keylet identifying the entry.
     *  @return Shared pointer to the immutable SLE, or `nullptr` if absent.
     */
    std::shared_ptr<SLE const>
    read(Keylet const& k) const override;

    /** @return Iterator to the first SLE in the merged state map. */
    std::unique_ptr<SlesType::iter_base>
    slesBegin() const override;

    /** @return Past-the-end iterator for the merged state map. */
    std::unique_ptr<SlesType::iter_base>
    slesEnd() const override;

    /** @return Iterator to the first SLE whose key is > `key` in the merged map.
     *
     *  @param key The exclusive lower bound.
     */
    std::unique_ptr<SlesType::iter_base>
    slesUpperBound(uint256 const& key) const override;

    /** @return Iterator to the first transaction in this view's tx map.
     *
     *  @note For open ledgers the iterator will not deserialize metadata;
     *      for closed-ledger views it will.
     */
    std::unique_ptr<TxsType::iter_base>
    txsBegin() const override;

    /** @return Past-the-end iterator for this view's tx map. */
    std::unique_ptr<TxsType::iter_base>
    txsEnd() const override;

    /** Check whether a transaction is present in this view's tx map.
     *
     *  @param key The transaction ID.
     *  @return `true` if the transaction was inserted into this view.
     */
    bool
    txExists(key_type const& key) const override;

    /** Read a transaction from this view, falling back to the base.
     *
     *  @param key The transaction ID.
     *  @return Pair of `(STTx, optional metadata STObject)`; both pointers are
     *      null if the transaction is not found in this view or the base.
     */
    tx_type
    txRead(key_type const& key) const override;

    // RawView

    /** Buffer a deletion of an existing state item.
     *
     *  Delegates to `RawStateTable::erase`. The entry will be removed from
     *  the merged view immediately and will not appear in subsequent reads.
     *
     *  @param sle The SLE to erase; its key is extracted from the object.
     */
    void
    rawErase(std::shared_ptr<SLE> const& sle) override;

    /** Buffer an insertion of a new state item.
     *
     *  Delegates to `RawStateTable::insert`. The key must not already exist
     *  in the merged view.
     *
     *  @param sle The new SLE to insert; its key is extracted from the object.
     */
    void
    rawInsert(std::shared_ptr<SLE> const& sle) override;

    /** Buffer a replacement of an existing state item.
     *
     *  Delegates to `RawStateTable::replace`. The key must already exist in
     *  the merged view.
     *
     *  @param sle The replacement SLE; its key is extracted from the object.
     */
    void
    rawReplace(std::shared_ptr<SLE> const& sle) override;

    /** Record destruction of XRP (burned as transaction fees).
     *
     *  Delegates to `RawStateTable::destroyXRP`. The destroyed amount
     *  accumulates in the state table and is flushed to the target on `apply()`.
     *
     *  @param fee The amount of XRP to destroy.
     */
    void
    rawDestroyXRP(XRPAmount const& fee) override;

    // TxsRawView

    /** Record a transaction in this view's transaction map.
     *
     *  For open ledgers `metaData` is typically `nullptr`; for closed-ledger
     *  representations it carries the serialized `TxMeta`.
     *
     *  @param key The transaction ID (must be unique within this view).
     *  @param txn Serialized transaction blob.
     *  @param metaData Serialized transaction metadata, or `nullptr` for open
     *      ledger entries.
     *  @throws std::logic_error if `key` is already present in this view's
     *      tx map. Duplicate transaction IDs are a hard invariant violation.
     */
    void
    rawTxInsert(
        key_type const& key,
        std::shared_ptr<Serializer const> const& txn,
        std::shared_ptr<Serializer const> const& metaData) override;
};

}  // namespace xrpl
