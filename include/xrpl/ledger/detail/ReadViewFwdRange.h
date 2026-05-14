/** @file
 *  Type-erased forward-iterator infrastructure for `ReadView` traversal.
 *
 *  Defines `ReadViewFwdIter` (the abstract iterator interface) and
 *  `ReadViewFwdRange` (the STL-compatible range wrapper) that together let
 *  any `ReadView` subclass expose its state and transaction maps through a
 *  single, stable iterator type. Callers interact indirectly via
 *  `ReadView::sles` and `ReadView::txs`; this header is internal plumbing.
 */

#pragma once

#include <cstddef>
#include <memory>
#include <optional>

namespace xrpl {

class ReadView;

namespace detail {

/** Abstract base defining the four primitive operations of a type-erased forward iterator.
 *
 *  Each concrete `ReadView` implementation provides a private subclass of
 *  this template and hands heap-allocated instances to `ReadViewFwdRange::Iterator`
 *  via the factory methods `slesBegin()`, `slesEnd()`, `slesUpperBound()`,
 *  `txsBegin()`, and `txsEnd()` on `ReadView`. Callers never interact with
 *  this class directly.
 *
 *  @tparam ValueType The element type yielded by the iterator —
 *      `std::shared_ptr<SLE const>` for state-map iteration or
 *      `ReadView::tx_type` for transaction-map iteration.
 */
template <class ValueType>
class ReadViewFwdIter
{
public:
    using base_type = ReadViewFwdIter;

    using value_type = ValueType;

    ReadViewFwdIter() = default;
    ReadViewFwdIter(ReadViewFwdIter const&) = default;
    ReadViewFwdIter&
    operator=(ReadViewFwdIter const&) = default;

    virtual ~ReadViewFwdIter() = default;

    /** Returns a heap-allocated deep copy of this iterator.
     *
     *  Provides value-semantics copy for the owning `unique_ptr` wrapper.
     *  Each concrete subclass must return a new instance of itself in the
     *  same position.
     *
     *  @return A `unique_ptr` to a fresh copy of this iterator instance.
     */
    [[nodiscard]] virtual std::unique_ptr<ReadViewFwdIter>
    copy() const = 0;

    /** Returns `true` if this iterator denotes the same position as @p impl.
     *
     *  Both iterators must be over the same underlying view; mixing iterators
     *  from different views produces undefined behavior.
     *
     *  @param impl The other iterator to compare against.
     *  @return `true` when both iterators point to the same element (or both
     *      are end sentinels).
     */
    [[nodiscard]] virtual bool
    equal(ReadViewFwdIter const& impl) const = 0;

    /** Advances this iterator to the next element in the sequence. */
    virtual void
    increment() = 0;

    /** Returns the element at the current iterator position.
     *
     *  @return The current `ValueType` value. The result is cached by the
     *      wrapping `Iterator` so repeated dereferences are inexpensive.
     *  @throw May throw if the underlying view operation fails.
     */
    [[nodiscard]] virtual value_type
    dereference() const = 0;
};

/** STL-compatible forward range backed by a type-erased iterator.
 *
 *  Wraps a `ReadViewFwdIter<ValueType>` behind a regular value-type iterator
 *  so that callers can write range-for loops over any `ReadView` subclass
 *  without knowing the concrete iterator type. Virtual dispatch is hidden
 *  inside the `impl_` pointer; the public `Iterator` API is fully inlined.
 *
 *  `ReadView::SlesType` and `ReadView::TxsType` inherit from this template;
 *  application code should use those types rather than instantiating
 *  `ReadViewFwdRange` directly.
 *
 *  @tparam ValueType The element type — must be noexcept-move-constructible
 *      so that `Iterator` move operations are noexcept.
 */
template <class ValueType>
class ReadViewFwdRange
{
public:
    using iter_base = ReadViewFwdIter<ValueType>;

    static_assert(
        std::is_nothrow_move_constructible<ValueType>{},
        "ReadViewFwdRange move and move assign constructors should be "
        "noexcept");

    /** STL forward iterator over a `ReadViewFwdRange`.
     *
     *  Value-type wrapper around a heap-allocated `iter_base`. Copy uses
     *  `iter_base::copy()` for a polymorphic deep clone; move transfers
     *  ownership of the `unique_ptr` without allocation and is `noexcept`.
     *  Dereference results are cached in `cache_` and cleared on advance,
     *  amortizing the cost of repeated `*it` or `it->` calls in tight loops.
     *
     *  @note Comparing iterators from different views triggers an
     *      `XRPL_ASSERT` in debug builds. The `view_` pointer is carried
     *      solely for this cross-view sanity check.
     */
    class Iterator
    {
    public:
        using value_type = ValueType;

        using pointer = value_type const*;

        using reference = value_type const&;

        using difference_type = std::ptrdiff_t;

        using iterator_category = std::forward_iterator_tag;

        /** Constructs a singular (default) iterator.
         *
         *  A default-constructed iterator is not dereferenceable and must
         *  not be incremented. It compares equal only to other
         *  default-constructed iterators.
         */
        Iterator() = default;

        /** Copy-constructs an independent iterator at the same position.
         *
         *  Calls `iter_base::copy()` to deep-clone the polymorphic
         *  implementation, producing a new iterator that advances
         *  independently of @p other.
         *
         *  @param other The iterator to clone.
         */
        Iterator(Iterator const& other);

        /** Move-constructs an iterator, transferring ownership of the impl.
         *
         *  @param other The iterator to move from; left in a valid but
         *      singular state.
         */
        Iterator(Iterator&& other) noexcept;

        /** Constructs an iterator from a raw view pointer and a polymorphic impl.
         *
         *  Used exclusively by `ReadView`'s factory methods (`slesBegin()`,
         *  `slesEnd()`, etc.). Not intended for direct use by callers.
         *
         *  @param view  The owning view; stored only for cross-view assertion.
         *  @param impl  The heap-allocated concrete iterator; ownership is
         *      transferred to this object.
         */
        explicit Iterator(ReadView const* view, std::unique_ptr<iter_base> impl);

        /** Copy-assigns from another iterator at the same position.
         *
         *  Deep-clones via `iter_base::copy()`.
         *
         *  @param other The iterator to copy.
         *  @return `*this`.
         */
        Iterator&
        operator=(Iterator const& other);

        /** Move-assigns from another iterator.
         *
         *  @param other The iterator to move from; left in a valid but
         *      singular state.
         *  @return `*this`.
         */
        Iterator&
        operator=(Iterator&& other) noexcept;

        /** Returns `true` if both iterators denote the same position.
         *
         *  Delegates to `iter_base::equal()`. Two null `impl_` pointers also
         *  compare equal (both are end sentinels / default-constructed).
         *
         *  @param other The iterator to compare against.
         *  @return `true` when both iterators are at the same element.
         *  @note Asserts in debug builds that both iterators belong to the
         *      same view. Comparing iterators from different views is
         *      undefined behaviour.
         */
        bool
        operator==(Iterator const& other) const;

        /** Returns `true` if the iterators denote different positions.
         *
         *  @param other The iterator to compare against.
         *  @return `true` when the iterators are not at the same element.
         */
        bool
        operator!=(Iterator const& other) const;

        /** Returns a reference to the current element.
         *
         *  The result is cached after the first call; subsequent calls before
         *  the next `operator++` return the cached value at no extra cost.
         *
         *  @return A `const` reference to the current `ValueType`.
         *  @throw May throw if the underlying `iter_base::dereference()` call fails.
         */
        // Can throw
        reference
        operator*() const;

        /** Returns a pointer to the current element.
         *
         *  Delegates to `operator*()` so caching and exception behaviour are
         *  identical to that of the dereference operator.
         *
         *  @return A `const` pointer to the current `ValueType`.
         *  @throw May throw if the underlying `iter_base::dereference()` call fails.
         */
        // Can throw
        pointer
        operator->() const;

        /** Advances the iterator and clears the dereference cache.
         *
         *  @return `*this` after advancing to the next element.
         */
        Iterator&
        operator++();

        /** Returns a copy of the current iterator, then advances.
         *
         *  @return An iterator to the element before the advance.
         */
        Iterator
        operator++(int);

    private:
        /** Owning view; compared in `operator==` to catch cross-view misuse. */
        ReadView const* view_ = nullptr;
        /** Heap-allocated polymorphic iterator; null for the end sentinel. */
        std::unique_ptr<iter_base> impl_{};
        /** One-slot dereference cache; cleared on each advance. */
        std::optional<value_type> mutable cache_;
    };

    static_assert(std::is_nothrow_move_constructible<Iterator>{}, "");
    static_assert(std::is_nothrow_move_assignable<Iterator>{}, "");

    using const_iterator = Iterator;

    using value_type = ValueType;

    ReadViewFwdRange() = delete;
    ReadViewFwdRange(ReadViewFwdRange const&) = default;
    ReadViewFwdRange&
    operator=(ReadViewFwdRange const&) = default;

    /** Constructs a range bound to @p view.
     *
     *  The range stores a raw pointer to the view. The view must outlive
     *  the range and any iterators derived from it.
     *
     *  @param view The `ReadView` whose factory methods supply iterators.
     */
    explicit ReadViewFwdRange(ReadView const& view) : view_(&view)
    {
    }

protected:
    /** The view whose factory methods supply concrete `iter_base` instances. */
    ReadView const* view_;
};

}  // namespace detail
}  // namespace xrpl
