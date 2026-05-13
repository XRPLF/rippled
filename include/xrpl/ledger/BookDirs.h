#pragma once

#include <xrpl/ledger/ReadView.h>

namespace xrpl {

/** A range-based view over all offers in a single order-book direction.
 *
 *  The XRPL DEX stores offers in a two-level ledger directory structure: a
 *  *book* groups offers for one currency pair in one direction, and within
 *  the book each quality level (encoded exchange rate) has its own directory
 *  page. `BookDirs` presents this multi-level structure as a flat sequence of
 *  `SLE` objects, letting callers iterate every offer with a standard
 *  range-for loop without reasoning about quality boundaries or directory
 *  pagination.
 *
 *  Construction eagerly locates the first quality directory via
 *  `ReadView::succ` and loads the first page with `cdirFirst`; subsequent
 *  advancement is handled lazily by `const_iterator::operator++`.
 *
 *  @note The `ReadView` passed at construction must outlive both the
 *      `BookDirs` object and any iterators derived from it. Iterators hold
 *      raw pointers to the view.
 *  @see Dir for single-directory iteration (e.g., NFTokenOffer pages).
 */
class BookDirs
{
private:
    ReadView const* view_ = nullptr;
    uint256 const root_;
    uint256 const next_quality_;
    uint256 const key_;
    std::shared_ptr<SLE const> sle_ = nullptr;
    unsigned int entry_ = 0;
    uint256 index_;

public:
    class const_iterator;  // NOLINT(readability-identifier-naming)
    using value_type = std::shared_ptr<SLE const>;

    /** Construct a `BookDirs` range over all offers in `book` as seen by `view`.
     *
     *  Finds the first quality directory in the book's key-space via
     *  `view.succ` and positions the internal state at the first offer.
     *  If the book is empty, `begin() == end()` immediately.
     *
     *  @param view The ledger view to read from; must remain valid for the
     *      lifetime of this object and all derived iterators.
     *  @param book The currency pair and direction defining the order book.
     */
    BookDirs(ReadView const&, Book const&);

    /** Return an iterator positioned at the first offer in the book.
     *
     *  If the book is empty the returned iterator compares equal to `end()`.
     */
    [[nodiscard]] const_iterator
    begin() const;

    /** Return the past-the-end sentinel iterator for this book. */
    [[nodiscard]] const_iterator
    end() const;
};

/** Forward iterator over offers in an order book, crossing quality boundaries.
 *
 *  Advances through all offers in a book by walking pages within each quality
 *  directory via `cdirNext`, then locating the next quality directory via
 *  `ReadView::succ` when a quality is exhausted. Dereference re-reads the
 *  current offer SLE from the view and caches it until `operator++` clears
 *  the cache.
 *
 *  **End-sentinel encoding:** the end iterator and an exhausted begin iterator
 *  share identical state — `entry_ == 0`, `cur_key_ == key_`, and
 *  `index_ == beast::zero`. `operator++` explicitly resets to this state when
 *  no further quality directory exists, which is how loop termination is
 *  detected.
 *
 *  @note Default-constructed iterators have a null `view_` and compare
 *      unequal to everything, including each other. They are valid only as
 *      placeholders; dereferencing them is undefined behaviour.
 *  @note Only `BookDirs` may construct iterators in valid, non-default states.
 */
class BookDirs::const_iterator  // NOLINT(readability-identifier-naming)
{
public:
    using value_type = BookDirs::value_type;
    using pointer = value_type const*;
    using reference = value_type const&;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;

    /** Construct a default (placeholder) iterator with a null view.
     *
     *  Required by the `ForwardIterator` concept. The resulting iterator
     *  compares unequal to all other iterators and must not be dereferenced
     *  or incremented.
     */
    const_iterator() = default;

    /** Return true if both iterators refer to the same offer position.
     *
     *  Equality is determined by comparing `entry_`, `cur_key_`, and
     *  `index_`. If either iterator has a null view, returns false.
     *
     *  @note Comparing iterators from different `BookDirs` instances
     *      (different views or roots) triggers an assertion in debug builds.
     */
    bool
    operator==(const_iterator const& other) const;

    /** Return true if the iterators do not refer to the same offer position. */
    bool
    operator!=(const_iterator const& other) const
    {
        return !(*this == other);
    }

    /** Return a reference to the current offer SLE.
     *
     *  Reads the offer SLE from the view on first access and caches the
     *  result; subsequent dereferences of the same position return the cached
     *  value. The cache is cleared by `operator++`.
     *
     *  @note Asserts that `index_` is non-zero; dereferencing the end
     *      iterator or a default-constructed iterator is undefined behaviour.
     */
    reference
    operator*() const;

    /** Return a pointer to the current offer SLE.
     *
     *  Equivalent to `&**this`. Safe to use with `->` because `operator*`
     *  stores the result in a `mutable` cache member whose lifetime matches
     *  the iterator.
     */
    pointer
    operator->() const
    {
        return &**this;
    }

    /** Advance to the next offer in the book and return this iterator.
     *
     *  First attempts to advance within the current quality directory via
     *  `cdirNext`. If that quality is exhausted, uses `ReadView::succ` to
     *  find the next quality directory and positions at its first offer via
     *  `cdirFirst`. If no further quality directory exists, resets to the
     *  end-sentinel state. Clears the dereference cache.
     *
     *  @note Asserts that the iterator is not already at the end position
     *      (i.e. `index_` must be non-zero) before advancing.
     */
    const_iterator&
    operator++();

    /** Post-increment: advance and return a copy of the pre-increment state. */
    const_iterator
    operator++(int);

private:
    friend class BookDirs;

    /** Construct a valid iterator anchored to `view`, `root`, and `dirKey`.
     *
     *  Only `BookDirs` calls this constructor. `dirKey` becomes both `key_`
     *  (the end-sentinel anchor) and the initial `cur_key_`. Additional
     *  fields (`next_quality_`, `sle_`, `entry_`, `index_`) are populated by
     *  `BookDirs::begin()` for the begin iterator; the end iterator leaves
     *  them at their zero-initialised defaults.
     *
     *  @param view  The ledger view; must outlive this iterator.
     *  @param root  The root key of the book's quality key-space; must be
     *      non-zero.
     *  @param dirKey The key of the first quality directory, or `beast::zero`
     *      if the book is empty.
     */
    const_iterator(ReadView const& view, uint256 const& root, uint256 const& dirKey)
        : view_(&view), root_(root), key_(dirKey), cur_key_(dirKey)
    {
    }

    ReadView const* view_ = nullptr;
    uint256 root_;
    uint256 next_quality_;
    uint256 key_;
    uint256 cur_key_;
    std::shared_ptr<SLE const> sle_;
    unsigned int entry_ = 0;
    uint256 index_;
    std::optional<value_type> mutable cache_;
};

}  // namespace xrpl
