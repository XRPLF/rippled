/** @file
 *  Implements `RawStateTable`, the write-buffer that accumulates SLE
 *  mutations (erase/insert/replace) and defers flushing them to a backing
 *  `RawView` until `apply()` is called.
 *
 *  This file also defines the private nested `SlesIterImpl` class, which
 *  provides a sorted merged iteration over the base ledger's SLE range and
 *  the pending delta, making pending inserts and erases transparently visible
 *  to callers who iterate the view as if it were already committed.
 */
#include <xrpl/ledger/detail/RawStateTable.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/RawView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/XRPAmount.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace xrpl::detail {

/** Sorted merged iterator over a base ledger's SLE range and the pending
 *  `RawStateTable` delta.
 *
 *  Implements the `ReadView::SlesType::iter_base` virtual interface so that
 *  callers iterating `sles` on an `OpenView` see a consistent merged view:
 *  pending inserts appear in their sorted position, pending erases are hidden,
 *  and pending replaces shadow the base entry at the same key.
 *
 *  Two parallel cursor pairs are maintained:
 *  - `iter0_` / `sle0_`: current position in the base view's sorted SLE range.
 *  - `iter1_` / `sle1_`: current position in `items_` (sorted, `std::map`
 *      order). `sle1_` is null when `iter1_` points to an `Action::Erase`
 *      entry that has already been consumed by `skip()`.
 *
 *  On a key tie, the local entry (from `items_`) always shadows the base.
 *  Both iterators advance simultaneously so the base copy is consumed.
 *
 *  @note `equal()` uses `dynamic_cast` to guard against cross-type comparison.
 *      Comparing iterators from different `ReadView` instances fires an
 *      `XRPL_ASSERT` in debug builds.
 */
class RawStateTable::SlesIterImpl : public ReadView::SlesType::iter_base
{
private:
    std::shared_ptr<SLE const> sle0_;
    ReadView::SlesType::Iterator iter0_;
    ReadView::SlesType::Iterator end0_;
    std::shared_ptr<SLE const> sle1_;
    items_t::const_iterator iter1_;
    items_t::const_iterator end1_;

public:
    SlesIterImpl(SlesIterImpl const&) = default;

    /** Construct the merged iterator at the given start positions.
     *
     *  Immediately calls `skip()` after initialising `sle1_` so that the
     *  iterator starts at the first non-erased position, even if the very
     *  first `items_` entry is an `Action::Erase`.
     *
     *  @param iter1  Start of the `items_` range to merge.
     *  @param end1   End sentinel for the `items_` range.
     *  @param iter0  Start of the base view's SLE iterator range.
     *  @param end0   End sentinel for the base view's SLE iterator range.
     */
    SlesIterImpl(
        items_t::const_iterator iter1,
        items_t::const_iterator end1,
        ReadView::SlesType::Iterator iter0,
        ReadView::SlesType::Iterator end0)
        : iter0_(std::move(iter0)), end0_(std::move(end0)), iter1_(iter1), end1_(end1)
    {
        if (iter0_ != end0_)
            sle0_ = *iter0_;
        if (iter1_ != end1)
        {
            sle1_ = iter1_->second.sle;
            skip();
        }
    }

    /** Return a heap-allocated copy of this iterator (value semantics).
     *
     *  @return A `unique_ptr` to an independent copy of the current position.
     */
    std::unique_ptr<base_type>
    copy() const override
    {
        return std::make_unique<SlesIterImpl>(*this);
    }

    /** Test iterator equality.
     *
     *  Uses `dynamic_cast` to reject cross-type comparisons safely.
     *  Both `iter0_` and `iter1_` must agree for the iterators to be equal.
     *
     *  @param impl The other iterator to compare against.
     *  @return `true` if both iterators point to the same merged position.
     *  @note Asserts in debug builds that both end sentinels match, which
     *      would be violated by comparing iterators from different views.
     */
    bool
    equal(base_type const& impl) const override
    {
        if (auto const p = dynamic_cast<SlesIterImpl const*>(&impl))
        {
            XRPL_ASSERT(
                end1_ == p->end1_ && end0_ == p->end0_,
                "xrpl::detail::RawStateTable::equal : matching end "
                "iterators");
            return iter1_ == p->iter1_ && iter0_ == p->iter0_;
        }

        return false;
    }

    /** Advance to the next SLE in merged sorted order.
     *
     *  Determines which of `sle0_` and `sle1_` has the smaller key and
     *  advances that cursor (or both on a key tie, since the local entry
     *  shadows the base). Calls `skip()` afterward to consume any
     *  immediately following `Action::Erase` entries.
     *
     *  @note Asserts that at least one of `sle0_` / `sle1_` is non-null,
     *      which would be violated by incrementing past end.
     */
    void
    increment() override
    {
        XRPL_ASSERT(
            sle1_ || sle0_,
            "xrpl::detail::RawStateTable::increment : either SLE is "
            "non-null");

        if (sle1_ && !sle0_)
        {
            inc1();
            return;
        }

        if (sle0_ && !sle1_)
        {
            inc0();
            return;
        }

        if (sle1_->key() == sle0_->key())
        {
            inc1();
            inc0();
        }
        else if (sle1_->key() < sle0_->key())
        {
            inc1();
        }
        else
        {
            inc0();
        }
        skip();
    }

    /** Return the SLE at the current merged position.
     *
     *  Returns the local entry when its key is less than or equal to the
     *  base entry's key (local always wins on a tie), otherwise the base
     *  entry. Pending inserts and replaces are returned directly; base
     *  entries shadowed by an insert or replace are never exposed.
     *
     *  @return The `SLE const` at the current position.
     */
    value_type
    dereference() const override
    {
        if (!sle1_)
        {
            return sle0_;
        }
        if (!sle0_)
        {
            return sle1_;
        }
        if (sle1_->key() <= sle0_->key())
            return sle1_;
        return sle0_;
    }

private:
    /** Advance the base iterator and refresh `sle0_`. */
    void
    inc0()
    {
        ++iter0_;
        if (iter0_ == end0_)
        {
            sle0_ = nullptr;
        }
        else
        {
            sle0_ = *iter0_;
        }
    }

    /** Advance the local iterator and refresh `sle1_`. */
    void
    inc1()
    {
        ++iter1_;
        if (iter1_ == end1_)
        {
            sle1_ = nullptr;
        }
        else
        {
            sle1_ = iter1_->second.sle;
        }
    }

    /** Consume `Action::Erase` entries that mask the current base entry.
     *
     *  When `iter1_` points to an erase and its key matches `sle0_->key()`,
     *  both iterators advance in tandem so the deleted SLE is invisible to
     *  callers. The loop terminates when the keys no longer match, either
     *  iterator is exhausted, or the local action is no longer `Erase`.
     */
    void
    skip()
    {
        while (iter1_ != end1_ && iter1_->second.action == Action::Erase &&
               sle0_->key() == sle1_->key())
        {
            inc1();
            inc0();
            if (!sle0_)
                return;
        }
    }
};

//------------------------------------------------------------------------------

/** Flush all buffered mutations to a backing `RawView`.
 *
 *  Replays `dropsDestroyed_` as a single `rawDestroyXRP` call, then
 *  iterates `items_` dispatching each pending action to the corresponding
 *  `rawErase`, `rawInsert`, or `rawReplace` method on `to`.
 *
 *  Precondition invariants (e.g., the key must exist for erase/replace,
 *  must not exist for insert) are enforced by the target `RawView`, not
 *  here. `RawStateTable` only enforces transition correctness within its
 *  own pending buffer.
 *
 *  @param to The `RawView` target that will absorb the buffered changes.
 */
void
RawStateTable::apply(RawView& to) const
{
    to.rawDestroyXRP(dropsDestroyed_);
    for (auto const& elem : items_)
    {
        auto const& item = elem.second;
        switch (item.action)
        {
            case Action::Erase:
                to.rawErase(item.sle);
                break;
            case Action::Insert:
                to.rawInsert(item.sle);
                break;
            case Action::Replace:
                to.rawReplace(item.sle);
                break;
        }
    }
}

/** Test whether an SLE exists, overlaying the pending delta onto `base`.
 *
 *  Checks `items_` first: a pending erase returns `false`; a pending insert
 *  or replace returns `true` only if the keylet type check passes. Falls
 *  through to `base.exists(k)` when the key has no pending action.
 *
 *  @param base The underlying read-only ledger state.
 *  @param k    The keylet whose key and type to check.
 *  @return `true` if the entry exists and its SLE type satisfies `k.check()`.
 */
bool
RawStateTable::exists(ReadView const& base, Keylet const& k) const
{
    XRPL_ASSERT(k.key.isNonZero(), "xrpl::detail::RawStateTable::exists : nonzero key");
    auto const iter = items_.find(k.key);
    if (iter == items_.end())
        return base.exists(k);
    auto const& item = iter->second;
    if (item.action == Action::Erase)
        return false;
    if (!k.check(*item.sle))
        return false;
    return true;
}

/** Find the smallest key strictly greater than `key`, overlaying the pending
 *  delta onto `base`.
 *
 *  Computes the result in two independent passes then returns the minimum:
 *  1. Walks `base.succ()` repeatedly, skipping any key that has a pending
 *     `Action::Erase` in `items_`, to find the smallest non-deleted base
 *     successor.
 *  2. Scans `items_` forward from `key` for the first non-erase entry.
 *
 *  The lower of the two candidates is the answer. If `last` is provided and
 *  the result is `>= last`, returns `std::nullopt` (half-open range semantics).
 *
 *  @param base The underlying read-only ledger state.
 *  @param key  The current key; the search begins strictly after this value.
 *  @param last Optional exclusive upper bound; `std::nullopt` means unbounded.
 *  @return The next existing key, or `std::nullopt` if none exists in range.
 */
auto
RawStateTable::succ(ReadView const& base, key_type const& key, std::optional<key_type> const& last)
    const -> std::optional<key_type>
{
    std::optional<key_type> next = key;
    items_t::const_iterator iter;
    do
    {
        next = base.succ(*next, last);
        if (!next)
            break;
        iter = items_.find(*next);
    } while (iter != items_.end() && iter->second.action == Action::Erase);
    for (iter = items_.upper_bound(key); iter != items_.end(); ++iter)
    {
        if (iter->second.action != Action::Erase)
        {
            if (!next || next > iter->first)
                next = iter->first;
            break;
        }
    }
    if (last && next >= last)
        return std::nullopt;
    return next;
}

/** Stage an SLE deletion, applying the pending-state transition rules.
 *
 *  State machine transitions on the key:
 *  - No prior action → records `Action::Erase`.
 *  - Prior `Insert`  → removes the entry entirely (net-zero; nothing to
 *      propagate to the base on `apply()`).
 *  - Prior `Replace` → downgrades to `Action::Erase`.
 *  - Prior `Erase`   → `LogicError` (double-delete).
 *
 *  The base-view invariant (key must exist) is enforced by the target
 *  `RawView` at `apply()` time, not here.
 *
 *  @param sle The ledger entry to stage for deletion.
 *  @throws std::logic_error if the key already has a pending erase.
 */
void
RawStateTable::erase(std::shared_ptr<SLE> const& sle)
{
    auto const result = items_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(sle->key()),
        std::forward_as_tuple(Action::Erase, sle));
    if (result.second)
        return;
    auto& item = result.first->second;
    switch (item.action)
    {
        case Action::Erase:
            Throw<std::logic_error>("RawStateTable::erase: already erased");
            break;
        case Action::Insert:
            items_.erase(result.first);
            break;
        case Action::Replace:
            item.action = Action::Erase;
            item.sle = sle;
            break;
    }
}

/** Stage an SLE creation, applying the pending-state transition rules.
 *
 *  State machine transitions on the key:
 *  - No prior action → records `Action::Insert`.
 *  - Prior `Erase`   → upgrades to `Action::Replace` (delete-then-recreate
 *      at the same key within one transaction batch).
 *  - Prior `Insert`  → `LogicError` (duplicate insert).
 *  - Prior `Replace` → `LogicError` (key already exists in the delta).
 *
 *  @param sle The new ledger entry to stage for insertion.
 *  @throws std::logic_error if the key is already pending insert or replace.
 */
void
RawStateTable::insert(std::shared_ptr<SLE> const& sle)
{
    auto const result = items_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(sle->key()),
        std::forward_as_tuple(Action::Insert, sle));
    if (result.second)
        return;
    auto& item = result.first->second;
    switch (item.action)
    {
        case Action::Erase:
            item.action = Action::Replace;
            item.sle = sle;
            break;
        case Action::Insert:
            Throw<std::logic_error>("RawStateTable::insert: already inserted");
            break;
        case Action::Replace:
            Throw<std::logic_error>("RawStateTable::insert: already exists");
            break;
    }
}

/** Stage an SLE field update, applying the pending-state transition rules.
 *
 *  State machine transitions on the key:
 *  - No prior action → records `Action::Replace`.
 *  - Prior `Insert`  → updates the stored SLE pointer, preserving `Insert`
 *      (from the base view's perspective the key is still being created).
 *  - Prior `Replace` → updates the stored SLE pointer.
 *  - Prior `Erase`   → `LogicError` (cannot replace a deleted key).
 *
 *  @param sle The updated ledger entry to stage.
 *  @throws std::logic_error if the key has a pending erase.
 */
void
RawStateTable::replace(std::shared_ptr<SLE> const& sle)
{
    auto const result = items_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(sle->key()),
        std::forward_as_tuple(Action::Replace, sle));
    if (result.second)
        return;
    auto& item = result.first->second;
    switch (item.action)
    {
        case Action::Erase:
            Throw<std::logic_error>("RawStateTable::replace: was erased");
            break;
        case Action::Insert:
        case Action::Replace:
            item.sle = sle;
            break;
    }
}

/** Read an SLE, overlaying the pending delta onto `base`.
 *
 *  Checks `items_` first: a pending erase returns `nullptr`; a pending
 *  insert or replace returns the buffered SLE only if `k.check()` passes
 *  (guarding against type mismatches at the same key). Falls through to
 *  `base.read(k)` when the key has no pending action.
 *
 *  @param base The underlying read-only ledger state.
 *  @param k    The keylet specifying the key and expected SLE type.
 *  @return The SLE if it exists and the type matches, otherwise `nullptr`.
 */
std::shared_ptr<SLE const>
RawStateTable::read(ReadView const& base, Keylet const& k) const
{
    auto const iter = items_.find(k.key);
    if (iter == items_.end())
        return base.read(k);
    auto const& item = iter->second;
    if (item.action == Action::Erase)
        return nullptr;
    std::shared_ptr<SLE const> sle = item.sle;
    if (!k.check(*sle))
        return nullptr;
    return sle;
}

/** Accumulate XRP drops to destroy at `apply()` time.
 *
 *  Drops are not forwarded to the target `RawView` individually; they are
 *  summed here and replayed as a single `rawDestroyXRP` call in `apply()`,
 *  keeping fee-burn accounting atomic with the rest of the flush.
 *
 *  @param fee The quantity of XRP drops to add to the accumulated burn total.
 */
void
RawStateTable::destroyXRP(XRPAmount const& fee)
{
    dropsDestroyed_ += fee;
}

/** Return a begin iterator for the merged SLE range over `base` and the
 *  pending delta.
 *
 *  @param base The underlying read-only ledger state to merge with.
 *  @return A heap-allocated `iter_base` positioned at the first merged SLE.
 */
std::unique_ptr<ReadView::SlesType::iter_base>
RawStateTable::slesBegin(ReadView const& base) const
{
    return std::make_unique<SlesIterImpl>(
        items_.begin(), items_.end(), base.sles.begin(), base.sles.end());
}

/** Return an end sentinel for the merged SLE range over `base` and the
 *  pending delta.
 *
 *  @param base The underlying read-only ledger state to merge with.
 *  @return A heap-allocated `iter_base` positioned past the last merged SLE.
 */
std::unique_ptr<ReadView::SlesType::iter_base>
RawStateTable::slesEnd(ReadView const& base) const
{
    return std::make_unique<SlesIterImpl>(
        items_.end(), items_.end(), base.sles.end(), base.sles.end());
}

/** Return an iterator to the first merged SLE with key strictly greater than
 *  `key`.
 *
 *  @param base The underlying read-only ledger state to merge with.
 *  @param key  The exclusive lower bound for the search.
 *  @return A heap-allocated `iter_base` positioned at the first qualifying SLE.
 */
std::unique_ptr<ReadView::SlesType::iter_base>
RawStateTable::slesUpperBound(ReadView const& base, uint256 const& key) const
{
    return std::make_unique<SlesIterImpl>(
        items_.upper_bound(key), items_.end(), base.sles.upperBound(key), base.sles.end());
}

}  // namespace xrpl::detail
