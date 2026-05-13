#include <xrpl/ledger/Dir.h>

#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>

#include <cstddef>
#include <iterator>
#include <optional>

namespace xrpl {

using const_iterator = Dir::ConstIterator;

/** Construct the range by reading the root `DirectoryNode` SLE.
 *
 *  `indexes_` is set to point directly into the SLE's `sfIndexes` field
 *  data, which remains valid for the lifetime of `sle_`. If the root page
 *  does not exist, `sle_` and `indexes_` stay null and `begin()` returns an
 *  end-sentinel.
 */
Dir::Dir(ReadView const& view, Keylet const& key)
    : view_(&view), root_(key), sle_(view_->read(root_))
{
    if (sle_ != nullptr)
        indexes_ = &sle_->getFieldV256(sfIndexes);
}

/** Build an iterator positioned at the first directory entry.
 *
 *  Starts from an end-sentinel-shaped `ConstIterator` and promotes it only
 *  if the root SLE exists and `sfIndexes` is non-empty.  The root SLE's
 *  `shared_ptr` is copied into the iterator so the SLE stays alive.
 *
 *  An empty `sfIndexes` on a non-null root is valid (the directory has been
 *  allocated but all entries removed) and still yields `end()`.
 */
auto
Dir::begin() const -> ConstIterator
{
    auto it = ConstIterator(*view_, root_, root_);
    if (sle_ != nullptr)
    {
        it.sle_ = sle_;
        if (!indexes_->empty())
        {
            it.indexes_ = indexes_;
            it.it_ = std::begin(*indexes_);
            it.index_ = *it.it_;
        }
    }

    return it;
}

/** Return the past-the-end sentinel.
 *
 *  The sentinel encodes the end state structurally: `page_.key == root_.key`
 *  and `index_ == beast::zero`.  This matches the state that `nextPage()`
 *  converges to when `sfIndexNext` is 0 on the last directory page.
 */
auto
Dir::end() const -> ConstIterator
{
    return ConstIterator(*view_, root_, root_);
}

/** Compare two iterators for equality.
 *
 *  Two iterators are equal when they share the same `view_` pointer and
 *  `root_.key`, and their current `page_.key` and `index_` agree.
 *  The end-sentinel state (`page_.key == root_.key && index_ == beast::zero`)
 *  compares equal to any other iterator in that state, making
 *  range-based `for` loops terminate correctly.
 *
 *  @note Comparing iterators from different views or different directories is
 *      a programming error; `XRPL_ASSERT` fires in debug builds.
 */
bool
const_iterator::operator==(ConstIterator const& other) const
{
    if (view_ == nullptr || other.view_ == nullptr)
        return false;

    XRPL_ASSERT(
        view_ == other.view_ && root_.key == other.root_.key,
        "xrpl::Dir::ConstIterator::operator== : views and roots are matching");
    return page_.key == other.page_.key && index_ == other.index_;
}

/** Lazily dereference to the SLE pointed to by the current entry key.
 *
 *  The SLE is loaded from the view on first access and cached in `cache_`.
 *  Advancing the iterator clears the cache.  This avoids loading SLEs when
 *  the caller only needs the entry key (via `index()`) rather than the full
 *  ledger object.
 *
 *  @pre `index_ != beast::zero` — calling on an end-sentinel is undefined.
 */
const_iterator::reference
const_iterator::operator*() const
{
    XRPL_ASSERT(index_ != beast::kZERO, "xrpl::Dir::ConstIterator::operator* : nonzero index");
    if (!cache_)
        cache_ = view_->read(keylet::child(index_));
    return *cache_;
}

/** Advance to the next directory entry, crossing page boundaries if needed.
 *
 *  If incrementing `it_` within the current page's `sfIndexes` vector does
 *  not reach the end, `index_` is updated and the SLE cache is cleared.
 *  Otherwise `nextPage()` is called to follow the `sfIndexNext` link.
 *
 *  @pre `index_ != beast::zero` — incrementing an end-sentinel is undefined.
 */
const_iterator&
const_iterator::operator++()
{
    XRPL_ASSERT(index_ != beast::kZERO, "xrpl::Dir::ConstIterator::operator++ : nonzero index");
    if (++it_ != std::end(*indexes_))
    {
        index_ = *it_;
        cache_ = std::nullopt;
        return *this;
    }

    return nextPage();
}

/** Post-increment: advance and return a copy of the pre-advance state.
 *
 *  @pre `index_ != beast::zero` — post-incrementing an end-sentinel is
 *      undefined.
 */
const_iterator
const_iterator::operator++(int)
{
    XRPL_ASSERT(
        index_ != beast::kZERO, "xrpl::Dir::ConstIterator::operator++(int) : nonzero index");
    ConstIterator tmp(*this);
    ++(*this);
    return tmp;
}

/** Advance past the current page to the next linked `DirectoryNode`.
 *
 *  Reads `sfIndexNext` from the current page SLE:
 *  - **Zero** means this is the last page.  `page_.key` and `index_` are
 *    reset to the end-sentinel values (`root_.key` and `beast::zero`).
 *  - **Non-zero** constructs `keylet::page(root_, next)`, reads the SLE,
 *    updates `indexes_` and `it_`, and loads the first entry key.  An empty
 *    `sfIndexes` on a continuation page is treated as end-of-directory rather
 *    than a hard error, matching the empty-root-page handling in `begin()`.
 *
 *  @note A non-zero `sfIndexNext` that does not resolve to an existing SLE is
 *      a ledger integrity violation; `XRPL_ASSERT` fires in that case.
 *
 *  This method is also exposed publicly (declared in `Dir.h`) to let callers
 *  skip all remaining entries on the current page and jump directly to the
 *  start of the next one — useful when only per-page counts are needed.
 */
const_iterator&
const_iterator::nextPage()
{
    auto const next = sle_->getFieldU64(sfIndexNext);
    if (next == 0)
    {
        page_.key = root_.key;
        index_ = beast::kZERO;
    }
    else
    {
        page_ = keylet::page(root_, next);
        sle_ = view_->read(page_);
        XRPL_ASSERT(sle_, "xrpl::Dir::ConstIterator::nextPage : non-null SLE");
        indexes_ = &sle_->getFieldV256(sfIndexes);
        if (indexes_->empty())
        {
            index_ = beast::kZERO;
        }
        else
        {
            it_ = std::begin(*indexes_);
            index_ = *it_;
        }
    }
    cache_ = std::nullopt;
    return *this;
}

/** Return the number of entries on the current page.
 *
 *  Equivalent to the size of the current page's `sfIndexes` vector.  Used
 *  with `nextPage()` for page-at-a-time traversal that avoids per-entry SLE
 *  loads when only entry counts are required.
 */
std::size_t
const_iterator::pageSize()
{
    return indexes_->size();
}

}  // namespace xrpl
