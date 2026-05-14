#pragma once

#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Indexes.h>

namespace xrpl {

/** Read-only range adaptor for a paged ledger directory (`ltDIR_NODE`).
 *
 *  A ledger directory is a linked list of `DirectoryNode` SLEs, each holding
 *  a `STVector256` (`sfIndexes`) of 256-bit keys pointing to other ledger
 *  objects. `Dir` wraps that structure in a C++ forward-iterable range,
 *  hiding page-chasing and SLE loading behind `begin()`/`end()`.
 *
 *  Construction reads the root page eagerly but loads no entry SLEs;
 *  per-entry loading is deferred to `operator*()`. The class is used
 *  with NFTokenOffer directories (`keylet::nft_buys()`, `keylet::nft_sells()`)
 *  and in unit tests with owner directories (`keylet::ownerDir()`).
 *
 *  @note Callers that only need per-page counts (not per-entry SLEs) should
 *      use `nextPage()` as the loop increment and `pageSize()` for counting,
 *      which avoids the per-entry `ReadView::read()` calls entirely.
 */
class Dir
{
private:
    ReadView const* view_ = nullptr;
    Keylet root_;
    std::shared_ptr<SLE const> sle_;
    STVector256 const* indexes_ = nullptr;

public:
    class ConstIterator;

    /** `shared_ptr<SLE const>`, matching `ReadView::read()`'s return type. */
    using value_type = std::shared_ptr<SLE const>;

    /** Construct a range over the directory rooted at `key` in `view`.
     *
     *  Reads the root `DirectoryNode` SLE immediately and caches its
     *  `sfIndexes`. If the root page is absent the range is empty.
     *
     *  @param view The ledger view to read from; must outlive this object.
     *  @param key  Keylet of the directory root page.
     */
    Dir(ReadView const&, Keylet const&);

    /** Return an iterator to the first entry of the directory.
     *
     *  If the root page is missing or its `sfIndexes` is empty, the returned
     *  iterator compares equal to `end()`.
     *
     *  @return A `ConstIterator` positioned at the first directory entry,
     *      or `end()` if the directory is empty.
     */
    [[nodiscard]] ConstIterator
    begin() const;

    /** Return a past-the-end sentinel iterator.
     *
     *  The sentinel has `page_.key == root_.key` and `index_ == beast::zero`.
     *  An iterator reaches this state when `nextPage()` finds `sfIndexNext == 0`
     *  on the last `DirectoryNode` page.
     *
     *  @return A past-the-end `ConstIterator`.
     */
    [[nodiscard]] ConstIterator
    end() const;
};

/** Forward iterator over entries in a paged ledger directory.
 *
 *  Each dereference lazily loads the ledger object pointed to by the current
 *  directory entry key via `ReadView::read(keylet::child(index_))`. The result
 *  is cached in `cache_` and cleared on every advance, including page
 *  transitions.
 *
 *  Equality compares `page_.key` and `index_`. Two iterators are equal when
 *  both fields match; comparing iterators from different views or roots is
 *  undefined (asserted in debug builds).
 *
 *  @note Advancing an iterator that is already at `end()` is undefined.
 *      Always guard with `it != dir.end()` before incrementing.
 */
class Dir::ConstIterator
{
public:
    using value_type = Dir::value_type;
    using pointer = value_type const*;
    using reference = value_type const&;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;

    /** Return true if both iterators point to the same directory entry.
     *
     *  Returns `false` if either view pointer is null. Asserts in debug builds
     *  that both iterators share the same view and root keylet.
     *
     *  @param other The iterator to compare against.
     *  @return `true` if `page_.key` and `index_` match in both iterators.
     */
    bool
    operator==(ConstIterator const& other) const;

    /** Return true if the iterators do not point to the same directory entry.
     *
     *  @param other The iterator to compare against.
     *  @return `!(*this == other)`.
     */
    bool
    operator!=(ConstIterator const& other) const
    {
        return !(*this == other);
    }

    /** Load and return the ledger object for the current directory entry.
     *
     *  The result is cached after the first call and reused on subsequent
     *  dereferences of the same position. The cache is cleared on every
     *  advance (including page transitions).
     *
     *  @return `shared_ptr<SLE const>` to the referenced ledger object,
     *      or `nullptr` if the object is not present in the view.
     */
    reference
    operator*() const;

    /** Return a pointer to the current entry's `shared_ptr<SLE const>`.
     *
     *  @return Pointer to the cached SLE shared pointer.
     */
    pointer
    operator->() const
    {
        return &**this;
    }

    /** Advance to the next directory entry, crossing page boundaries as needed.
     *
     *  When the end of the current page's `sfIndexes` is reached, calls
     *  `nextPage()` to load the subsequent `DirectoryNode`. If no next page
     *  exists the iterator converges to the `end()` sentinel.
     *
     *  @return Reference to this iterator after advancement.
     */
    ConstIterator&
    operator++();

    /** Post-increment: return a copy of this iterator, then advance.
     *
     *  @return Copy of the iterator before advancement.
     */
    ConstIterator
    operator++(int);

    /** Jump directly to the first entry of the next `DirectoryNode` page.
     *
     *  Reads `sfIndexNext` from the current page SLE. If the value is zero
     *  (last page), the iterator is set to the `end()` sentinel. Otherwise,
     *  loads `keylet::page(root_, sfIndexNext)` and positions the iterator
     *  at the beginning of that page's `sfIndexes`.
     *
     *  This method is public so callers can skip an entire page without
     *  loading individual entries — useful when only the per-page count is
     *  needed (see `pageSize()`).
     *
     *  @return Reference to this iterator, now positioned at the start of the
     *      next page, or at `end()` if the directory is exhausted.
     */
    ConstIterator&
    nextPage();

    /** Return the number of entries on the current page.
     *
     *  Reports `sfIndexes.size()` for the currently loaded `DirectoryNode`
     *  without reading any entry SLEs. Combined with `nextPage()` as a loop
     *  increment, this enables O(pages) offer-count checks instead of
     *  O(entries).
     *
     *  @return Number of `uint256` entries in the current page's `sfIndexes`.
     */
    std::size_t
    pageSize();

    /** Return the keylet of the currently loaded `DirectoryNode` page.
     *
     *  @return `Keylet` identifying the current page SLE.
     */
    Keylet const&
    page() const
    {
        return page_;
    }

    /** Return the `uint256` key of the current directory entry.
     *
     *  Equal to `beast::zero` when the iterator is at `end()`.
     *
     *  @return The current entry's 256-bit ledger object key.
     */
    uint256
    index() const
    {
        return index_;
    }

private:
    friend class Dir;

    ConstIterator(ReadView const& view, Keylet const& root, Keylet const& page)
        : view_(&view), root_(root), page_(page)
    {
    }

    ReadView const* view_ = nullptr;
    Keylet root_;
    Keylet page_;
    uint256 index_;
    std::optional<value_type> mutable cache_;
    std::shared_ptr<SLE const> sle_;
    STVector256 const* indexes_ = nullptr;
    std::vector<uint256>::const_iterator it_;
};

}  // namespace xrpl
