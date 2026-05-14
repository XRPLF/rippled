#pragma once

#include <xrpl/ledger/RawView.h>
#include <xrpl/ledger/ReadView.h>

#include <boost/container/pmr/monotonic_buffer_resource.hpp>
#include <boost/container/pmr/polymorphic_allocator.hpp>

#include <map>
#include <utility>

namespace xrpl::detail {

/** In-memory write buffer that accumulates SLE mutations before flushing them
 *  to a backing `RawView`.
 *
 *  Every mutable ledger view (`OpenView`, and indirectly `ApplyStateTable`)
 *  embeds a `RawStateTable` as its delta accumulator. The three mutation
 *  methods — `erase`, `insert`, and `replace` — apply a state-machine
 *  collapse so the map stays minimal: insert-then-erase cancels out entirely;
 *  erase-then-insert upgrades to replace; and illegal sequences (double-erase,
 *  double-insert) throw `std::logic_error`. `read`, `exists`, and `succ`
 *  overlay the pending delta transparently onto the supplied base `ReadView`,
 *  so callers always see a coherent merged state. Once a transaction succeeds,
 *  `apply()` flushes the buffer to the target `RawView` in a single pass.
 *
 *  The `items_` map uses a `boost::container::pmr::monotonic_buffer_resource`
 *  with a 256 KB initial arena for O(1) amortised allocation during the burst
 *  of mutations that constitute a single transaction round. Because the
 *  resource cannot be shared or assigned, copy construction allocates a fresh
 *  resource and deep-copies the map; move construction transfers the
 *  `unique_ptr` directly. Both assignment operators are deleted.
 *
 *  XRP fee destruction is tracked separately in `dropsDestroyed_` and
 *  replayed as a single `rawDestroyXRP` call during `apply()`.
 *
 *  @note This class is an internal implementation detail of `OpenView`.
 *      Transaction logic should not interact with it directly; use the
 *      `RawView` interface instead.
 *  @see OpenView, RawView
 */
class RawStateTable
{
public:
    using key_type = ReadView::key_type;

    /** Initial arena size for the PMR monotonic buffer resource.
     *
     *  Inherited from the legacy `qalloc` scheme this replaced. The 256 KB
     *  budget covers the typical per-transaction working set without triggering
     *  heap growth for the common case.
     */
    static constexpr size_t kINITIAL_BUFFER_SIZE = kilobytes(256);

    /** Construct an empty table with a fresh 256 KB monotonic arena. */
    RawStateTable()
        : monotonic_resource_{std::make_unique<boost::container::pmr::monotonic_buffer_resource>(
              kINITIAL_BUFFER_SIZE)}
        , items_{monotonic_resource_.get()} {};

    /** Copy-construct by allocating a fresh monotonic arena and copying items.
     *
     *  The SLE `shared_ptr` values in `items_` are shared with the source —
     *  not deep-copied — which is safe because SLEs are immutable once
     *  published. `dropsDestroyed_` is copied verbatim.
     *
     *  @param rhs The source table to copy.
     */
    RawStateTable(RawStateTable const& rhs)
        : monotonic_resource_{std::make_unique<boost::container::pmr::monotonic_buffer_resource>(
              kINITIAL_BUFFER_SIZE)}
        , items_{rhs.items_, monotonic_resource_.get()}
        , dropsDestroyed_{rhs.dropsDestroyed_} {};

    /** Move-construct by transferring the monotonic resource and items map.
     *
     *  After the move, the source table is left in a valid but empty state.
     *  The `unique_ptr` transfer preserves the stable address that `items_`'
     *  `polymorphic_allocator` holds.
     */
    RawStateTable(RawStateTable&&) = default;

    RawStateTable&
    operator=(RawStateTable&&) = delete;
    RawStateTable&
    operator=(RawStateTable const&) = delete;

    /** Flush all buffered mutations to a backing `RawView`.
     *
     *  First calls `to.rawDestroyXRP(dropsDestroyed_)` to replay accumulated
     *  fee burns, then iterates `items_` and dispatches each pending action
     *  to the corresponding `rawErase`, `rawInsert`, or `rawReplace` method.
     *  The table is not cleared after apply; this object should be discarded
     *  or destroyed once flushed.
     *
     *  @param to The target `RawView` that receives all buffered mutations.
     */
    void
    apply(RawView& to) const;

    /** Test whether an SLE exists, overlaying the pending delta onto `base`.
     *
     *  Checks the pending buffer first: a pending erase returns `false`; a
     *  pending insert or replace returns `true` only if `k.check()` passes
     *  (type-tag validation). Falls through to `base.exists(k)` when the key
     *  has no pending action.
     *
     *  @param base The underlying read-only ledger state.
     *  @param k    The keylet specifying key and expected SLE type.
     *  @return `true` if the entry exists and its type satisfies `k.check()`.
     */
    [[nodiscard]] bool
    exists(ReadView const& base, Keylet const& k) const;

    /** Find the smallest key strictly greater than `key` in the merged state.
     *
     *  Runs two parallel searches: (1) walks `base.succ()` repeatedly,
     *  skipping any base key that has a pending `Action::Erase`; (2) scans
     *  `items_` forward from `key` for the first non-erase entry. Returns
     *  the lower of the two candidates. If `last` is given and the result is
     *  `>= last`, returns `std::nullopt` (half-open range semantics).
     *
     *  @param base The underlying read-only ledger state.
     *  @param key  Exclusive lower bound; the search begins strictly after this.
     *  @param last Optional exclusive upper bound; `std::nullopt` means unbounded.
     *  @return The next existing key, or `std::nullopt` if none is in range.
     */
    [[nodiscard]] std::optional<key_type>
    succ(ReadView const& base, key_type const& key, std::optional<key_type> const& last) const;

    /** Stage an SLE deletion, applying state-machine transition rules.
     *
     *  Transitions on the key's existing pending action:
     *  - None       → records `Action::Erase`.
     *  - `Insert`   → removes the entry entirely (net-zero; base is unaffected).
     *  - `Replace`  → downgrades to `Action::Erase`.
     *  - `Erase`    → `LogicError` (double-delete).
     *
     *  @param sle The ledger entry to stage for deletion; key is taken from the SLE.
     *  @throws std::logic_error if the key already has a pending erase.
     */
    void
    erase(std::shared_ptr<SLE> const& sle);

    /** Stage an SLE creation, applying state-machine transition rules.
     *
     *  Transitions on the key's existing pending action:
     *  - None      → records `Action::Insert`.
     *  - `Erase`   → upgrades to `Action::Replace` (delete-then-recreate in
     *      the same transaction batch).
     *  - `Insert`  → `LogicError` (duplicate insert).
     *  - `Replace` → `LogicError` (key already present in the delta).
     *
     *  @param sle The new ledger entry to stage; key is taken from the SLE.
     *  @throws std::logic_error if the key is already pending insert or replace.
     */
    void
    insert(std::shared_ptr<SLE> const& sle);

    /** Stage an SLE field update, applying state-machine transition rules.
     *
     *  Transitions on the key's existing pending action:
     *  - None      → records `Action::Replace`.
     *  - `Insert`  → updates the stored SLE pointer; preserves `Insert`
     *      because from the base's perspective the key is still being created.
     *  - `Replace` → updates the stored SLE pointer.
     *  - `Erase`   → `LogicError` (cannot replace a deleted key).
     *
     *  @param sle The updated ledger entry to stage; key is taken from the SLE.
     *  @throws std::logic_error if the key has a pending erase.
     */
    void
    replace(std::shared_ptr<SLE> const& sle);

    /** Read an SLE, overlaying the pending delta onto `base`.
     *
     *  Checks the buffer first: a pending erase returns `nullptr`; a pending
     *  insert or replace returns the buffered SLE if `k.check()` passes
     *  (guards against type mismatches at the same key). Falls through to
     *  `base.read(k)` when the key has no pending action.
     *
     *  @param base The underlying read-only ledger state.
     *  @param k    The keylet specifying key and expected SLE type.
     *  @return The SLE if it exists and the type matches, otherwise `nullptr`.
     */
    [[nodiscard]] std::shared_ptr<SLE const>
    read(ReadView const& base, Keylet const& k) const;

    /** Accumulate XRP drops to destroy at `apply()` time.
     *
     *  Drops are not forwarded individually; they accumulate in
     *  `dropsDestroyed_` and are replayed as a single `rawDestroyXRP` call in
     *  `apply()`, keeping fee-burn accounting atomic with the rest of the flush.
     *
     *  @param fee The quantity of XRP drops to add to the accumulated burn total.
     */
    void
    destroyXRP(XRPAmount const& fee);

    /** Return a begin iterator for the merged SLE range over `base` and the delta.
     *
     *  The returned iterator implements the two-pointer merge defined by
     *  `SlesIterImpl`: pending inserts appear in sorted position, pending
     *  erases are hidden, and pending replaces shadow the base entry.
     *
     *  @param base The underlying read-only ledger state to merge with.
     *  @return A heap-allocated `iter_base` positioned at the first merged SLE.
     */
    [[nodiscard]] std::unique_ptr<ReadView::SlesType::iter_base>
    slesBegin(ReadView const& base) const;

    /** Return an end sentinel for the merged SLE range over `base` and the delta.
     *
     *  @param base The underlying read-only ledger state to merge with.
     *  @return A heap-allocated `iter_base` positioned past the last merged SLE.
     */
    [[nodiscard]] std::unique_ptr<ReadView::SlesType::iter_base>
    slesEnd(ReadView const& base) const;

    /** Return an iterator to the first merged SLE with key strictly greater
     *  than `key`.
     *
     *  @param base The underlying read-only ledger state to merge with.
     *  @param key  Exclusive lower bound for the search.
     *  @return A heap-allocated `iter_base` positioned at the first qualifying SLE.
     */
    [[nodiscard]] std::unique_ptr<ReadView::SlesType::iter_base>
    slesUpperBound(ReadView const& base, uint256 const& key) const;

private:
    /** Pending mutation kind for an entry in `items_`. */
    enum class Action {
        Erase,    /**< Entry is scheduled for deletion. */
        Insert,   /**< Entry is being created; does not yet exist in the base. */
        Replace,  /**< Entry exists in the base and has been modified. */
    };

    /** Private iterator class that merges base-view SLEs with the pending
     *  delta; defined in the `.cpp`. */
    class SlesIterImpl;

    /** Pairs a pending `Action` with the SLE it acts on.
     *
     *  Stored as the mapped value in `items_`. The SLE pointer is always
     *  non-null; for `Erase` it is the last version written before the
     *  deletion was staged (used by `RawView::rawErase`).
     */
    struct SleAction
    {
        Action action;
        std::shared_ptr<SLE> sle;

        // Constructor needed for emplacement in std::map
        SleAction(Action action, std::shared_ptr<SLE> const& sle) : action(action), sle(sle)
        {
        }
    };

    // Use boost::pmr functionality instead of the std::pmr
    // functions b/c clang does not support pmr yet (as-of 9/2020)
    using items_t = std::map<
        key_type,
        SleAction,
        std::less<key_type>,
        boost::container::pmr::polymorphic_allocator<std::pair<key_type const, SleAction>>>;

    // monotonic_resource_ must outlive `items_`. Make a pointer so it may be
    // easily moved.
    std::unique_ptr<boost::container::pmr::monotonic_buffer_resource> monotonic_resource_;

    /** Ordered map from ledger key to pending mutation; backed by the
     *  monotonic arena for O(1) amortised node allocation. */
    items_t items_;

    /** Accumulated XRP drops burned by fees; replayed as one `rawDestroyXRP`
     *  call during `apply()`. */
    XRPAmount dropsDestroyed_{0};
};

}  // namespace xrpl::detail
