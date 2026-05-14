/** @file
 *  Traversal utilities for ledger directory nodes (`ltDIR_NODE`).
 *
 *  A directory is a linked list of pages (`SLE` of type `ltDIR_NODE`),
 *  where each page holds an `sfIndexes` field (`STVector256`) of child
 *  ledger-entry keys and an `sfIndexNext` field that chains to the next
 *  page.  Owner directories track every object an account holds; order-
 *  book directories track standing offers at a given quality.
 *
 *  This header provides:
 *  - A const-aware template core (`detail::internalDirFirst` /
 *    `detail::internalDirNext`) that unifies the read and write traversal
 *    paths at compile time.
 *  - A deprecated step-iterator API (`cdirFirst`, `cdirNext`, `dirFirst`,
 *    `dirNext`) used only where cursor patching during deletion is required.
 *  - Higher-level callback iterators (`forEachItem`, `forEachItemAfter`)
 *    for exhaustive and paginated walks.
 *  - `dirIsEmpty` and `describeOwnerDir` utility helpers.
 */
#pragma once

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <functional>
#include <memory>
#include <type_traits>

namespace xrpl {

namespace detail {

/** Advance a directory cursor to the next entry, crossing page boundaries.
 *
 *  When the cursor has consumed all entries in the current page, the function
 *  follows `sfIndexNext` to load the next page and tail-calls itself to yield
 *  the first entry of that page in a single logical step.  If `sfIndexNext` is
 *  zero the directory is exhausted: `entry` is zeroed and `false` is returned.
 *
 *  The `if constexpr` branch selects `view.read()` when `N` is `SLE const`
 *  (read-only traversal via `ReadView`) and `view.peek()` when `N` is `SLE`
 *  (mutable traversal via `ApplyView`), keeping both paths in one template.
 *
 *  @tparam V   A view type derived from `ReadView`.
 *  @tparam N   Either `SLE` (mutable) or `SLE const` (read-only).
 *  @param view The ledger view to query pages from.
 *  @param root The 256-bit key of the directory's root (anchor) page.
 *  @param page In/out: the current page SLE; updated when a page boundary
 *      is crossed.
 *  @param index In/out: the zero-based cursor within `page->sfIndexes`;
 *      incremented to point past the entry that was just returned.
 *  @param entry Out: the key of the current entry on success; zeroed on
 *      end-of-directory.
 *  @return `true` if an entry was produced; `false` if the directory is
 *      exhausted.
 *  @note An `XRPL_ASSERT` fires in instrumented builds if `index` exceeds
 *      the page's entry count, indicating a corrupted cursor.
 */
template <
    class V,
    class N,
    class = std::enable_if_t<
        std::is_same_v<std::remove_cv_t<N>, SLE> && std::is_base_of_v<ReadView, V>>>
bool
internalDirNext(
    V& view,
    uint256 const& root,
    std::shared_ptr<N>& page,
    unsigned int& index,
    uint256& entry)
{
    auto const& svIndexes = page->getFieldV256(sfIndexes);
    XRPL_ASSERT(index <= svIndexes.size(), "xrpl::detail::internalDirNext : index inside range");

    if (index >= svIndexes.size())
    {
        auto const next = page->getFieldU64(sfIndexNext);

        if (!next)
        {
            entry.zero();
            return false;
        }

        if constexpr (std::is_const_v<N>)
        {
            page = view.read(keylet::page(root, next));
        }
        else
        {
            page = view.peek(keylet::page(root, next));
        }

        XRPL_ASSERT(page, "xrpl::detail::internalDirNext : non-null root");

        if (!page)
            return false;

        index = 0;

        return internalDirNext(view, root, page, index, entry);
    }

    entry = svIndexes[index++];
    return true;
}

/** Initialise a directory cursor at the first entry of the root page.
 *
 *  Loads the root page via `view.read()` (when `N` is `SLE const`) or
 *  `view.peek()` (when `N` is `SLE`), resets the index to zero, then
 *  delegates to `internalDirNext` to yield the first entry.
 *
 *  @tparam V   A view type derived from `ReadView`.
 *  @tparam N   Either `SLE` (mutable) or `SLE const` (read-only).
 *  @param view The ledger view to query pages from.
 *  @param root The 256-bit key of the directory's root (anchor) page.
 *  @param page Out: set to the root page SLE on success; unchanged if the
 *      root page is absent.
 *  @param index Out: set to zero before delegating to `internalDirNext`.
 *  @param entry Out: the key of the first entry on success.
 *  @return `true` if the directory has at least one entry; `false` if the
 *      root page is absent or the directory is empty.
 */
template <
    class V,
    class N,
    class = std::enable_if_t<
        std::is_same_v<std::remove_cv_t<N>, SLE> && std::is_base_of_v<ReadView, V>>>
bool
internalDirFirst(
    V& view,
    uint256 const& root,
    std::shared_ptr<N>& page,
    unsigned int& index,
    uint256& entry)
{
    if constexpr (std::is_const_v<N>)
    {
        page = view.read(keylet::page(root));
    }
    else
    {
        page = view.peek(keylet::page(root));
    }

    if (!page)
        return false;

    index = 0;

    return internalDirNext(view, root, page, index, entry);
}

}  // namespace detail

/** @{ */
/** Returns the first entry in the directory, advancing the index

    @deprecated These are legacy function that are considered deprecated
                and will soon be replaced with an iterator-based model
                that is easier to use. You should not use them in new code.

    @param view The view against which to operate
    @param root The root (i.e. first page) of the directory to iterate
    @param page The current page
    @param index The index inside the current page
    @param entry The entry at the current index

    @return true if the directory isn't empty; false otherwise
 */
bool
cdirFirst(
    ReadView const& view,
    uint256 const& root,
    std::shared_ptr<SLE const>& page,
    unsigned int& index,
    uint256& entry);

/** Returns the first entry in the directory, advancing the index.
 *
 *  Mutable overload of `cdirFirst` for use with `ApplyView`. Yields a
 *  `shared_ptr<SLE>` obtained via `view.peek()`, allowing the caller to
 *  modify the page SLE if required.
 *
 *  @deprecated Prefer the `Dir` range adaptor or `forEachItem` for new
 *      code. Use this overload only when cursor patching during deletion
 *      is required (see `cleanupOnAccountDelete` in `View.cpp`).
 *
 *  @param view The mutable view against which to operate.
 *  @param root The 256-bit key of the directory's root page.
 *  @param page Out: set to the root page SLE obtained via `peek()`.
 *  @param index Out: set to the cursor position within `page->sfIndexes`.
 *  @param entry Out: the key of the first directory entry.
 *  @return `true` if the directory has at least one entry; `false`
 *      otherwise.
 */
bool
dirFirst(
    ApplyView& view,
    uint256 const& root,
    std::shared_ptr<SLE>& page,
    unsigned int& index,
    uint256& entry);
/** @} */

/** @{ */
/** Returns the next entry in the directory, advancing the index

    @deprecated These are legacy function that are considered deprecated
                and will soon be replaced with an iterator-based model
                that is easier to use. You should not use them in new code.

    @param view The view against which to operate
    @param root The root (i.e. first page) of the directory to iterate
    @param page The current page
    @param index The index inside the current page
    @param entry The entry at the current index

    @return true if the directory isn't empty; false otherwise
 */
bool
cdirNext(
    ReadView const& view,
    uint256 const& root,
    std::shared_ptr<SLE const>& page,
    unsigned int& index,
    uint256& entry);

/** Advances the mutable directory cursor to the next entry.
 *
 *  Mutable overload of `cdirNext` for use with `ApplyView`. Page
 *  transitions are handled transparently: when `index` reaches the end
 *  of the current page, `sfIndexNext` is followed and the cursor is reset
 *  to the first entry of the new page.
 *
 *  @deprecated Prefer the `Dir` range adaptor or `forEachItem` for new
 *      code. The primary use case for this function is cursor patching
 *      during deletion: `cleanupOnAccountDelete` (in `View.cpp`) decrements
 *      `index` after each deletion so the cursor stays aligned as entries
 *      shift — a technique that relies on the cursor being externally
 *      accessible.
 *
 *  @param view The mutable view against which to operate.
 *  @param root The 256-bit key of the directory's root page.
 *  @param page In/out: the current page SLE; updated on page boundary
 *      crossing.
 *  @param index In/out: the cursor position within `page->sfIndexes`;
 *      incremented past the returned entry.
 *  @param entry Out: the key of the current entry on success; zeroed when
 *      the directory is exhausted.
 *  @return `true` if an entry was produced; `false` if the directory is
 *      exhausted.
 */
bool
dirNext(
    ApplyView& view,
    uint256 const& root,
    std::shared_ptr<SLE>& page,
    unsigned int& index,
    uint256& entry);
/** @} */

/** Exhaustively walk every entry in a directory, invoking a callback for each.
 *
 *  Iterates all pages of the directory in `sfIndexNext` chain order, calling
 *  `f` with the materialised child SLE for every key in `sfIndexes`.  The
 *  child SLE is obtained via `view.read(keylet::child(key))` and may be
 *  `nullptr` if the referenced entry is absent from the view; the callback
 *  must handle that case.  Iteration terminates when `sfIndexNext` is zero or
 *  a page SLE is missing; there is no early-exit mechanism.
 *
 *  @param view The read-only ledger view to query.
 *  @param root Keylet of the directory's root page; must have type
 *      `ltDIR_NODE`.
 *  @param f Callback invoked with each child SLE (possibly `nullptr`).
 *  @note An `XRPL_ASSERT` fires in instrumented builds if `root.type` is
 *      not `ltDIR_NODE`; in release builds the function returns silently.
 */
void
forEachItem(
    ReadView const& view,
    Keylet const& root,
    std::function<void(std::shared_ptr<SLE const> const&)> const& f);

/** Paginated directory walk, delivering items that follow a cursor key.
 *
 *  Supports cursor-based pagination as used by RPC handlers such as
 *  `account_offers`, `account_lines`, and `account_channels`.  When
 *  `after` is non-zero the function first attempts to jump to the `hint`
 *  page (the page the client last saw) to avoid re-scanning all prior
 *  pages; if the hint does not contain `after`, it falls back to a linear
 *  scan from the root.  Once the cursor is located, subsequent entries are
 *  delivered to `f` until `limit` is reached or the directory is exhausted.
 *
 *  The callback `f` returns `bool`: `true` to continue (and decrement the
 *  limit counter), `false` to stop immediately regardless of the remaining
 *  limit.  Callers conventionally request `limit + 1` items and infer a
 *  non-empty next page when exactly `limit + 1` items are delivered.
 *
 *  @param view The read-only ledger view to query.
 *  @param root Keylet of the directory's root page; must have type
 *      `ltDIR_NODE`.
 *  @param after Cursor key: only entries that follow this key in directory
 *      order are delivered.  Pass `uint256()` (zero) to start from the
 *      beginning, in which case the function always returns `true`.
 *  @param hint Page number expected to contain `after`; used as a fast-
 *      path optimisation.  Ignored when `after` is zero or when the hint
 *      page does not actually contain `after`.
 *  @param limit Maximum number of `true`-returning callback invocations
 *      before the walk stops.
 *  @param f Callback invoked for each qualifying child SLE (possibly
 *      `nullptr` if the key is absent).  Return `true` to continue
 *      iteration; `false` to stop early.
 *  @return `true` if `after` was found (or `after` is zero); `false` if
 *      the cursor key was never located, indicating a stale or invalid
 *      marker that callers should surface as a pagination error.
 */
bool
forEachItemAfter(
    ReadView const& view,
    Keylet const& root,
    uint256 const& after,
    std::uint64_t const hint,
    unsigned int limit,
    std::function<bool(std::shared_ptr<SLE const> const&)> const& f);

/** Exhaustively walk every entry in an account's owner directory.
 *
 *  Convenience overload that resolves `id` to `keylet::ownerDir(id)` and
 *  forwards to `forEachItem(view, Keylet, f)`.
 *
 *  @param view The read-only ledger view to query.
 *  @param id The account whose owner directory should be iterated.
 *  @param f Callback invoked with each child SLE (possibly `nullptr`).
 */
inline void
forEachItem(
    ReadView const& view,
    AccountID const& id,
    std::function<void(std::shared_ptr<SLE const> const&)> const& f)
{
    forEachItem(view, keylet::ownerDir(id), f);
}

/** Paginated walk of an account's owner directory after a cursor key.
 *
 *  Convenience overload that resolves `id` to `keylet::ownerDir(id)` and
 *  forwards to `forEachItemAfter(view, Keylet, after, hint, limit, f)`.
 *
 *  @param view The read-only ledger view to query.
 *  @param id The account whose owner directory should be iterated.
 *  @param after Cursor key; pass `uint256()` (zero) to start from the
 *      beginning.
 *  @param hint Page number expected to contain `after`.
 *  @param limit Maximum number of `true`-returning callback invocations.
 *  @param f Callback invoked for each qualifying child SLE.  Return `true`
 *      to continue; `false` to stop early.
 *  @return `true` if `after` was found (or is zero); `false` if the cursor
 *      was never located.
 */
inline bool
forEachItemAfter(
    ReadView const& view,
    AccountID const& id,
    uint256 const& after,
    std::uint64_t const hint,
    unsigned int limit,
    std::function<bool(std::shared_ptr<SLE const> const&)> const& f)
{
    return forEachItemAfter(view, keylet::ownerDir(id), after, hint, limit, f);
}

/** Returns `true` if the directory contains no entries.
 *
 *  An empty `sfIndexes` array on the root page is necessary but not
 *  sufficient: the root is an anchor page and may have an empty index
 *  while `sfIndexNext` still points to a populated subsequent page.  Both
 *  conditions — empty `sfIndexes` *and* `sfIndexNext == 0` — must hold
 *  before declaring the directory empty.  A missing root SLE is also
 *  treated as empty.
 *
 *  @param view The read-only ledger view to query.
 *  @param k Keylet of the directory's root page.
 *  @return `true` if the directory has no entries or does not exist;
 *      `false` otherwise.
 */
[[nodiscard]] bool
dirIsEmpty(ReadView const& view, Keylet const& k);

/** Returns a callback that stamps a new directory page with its owner account.
 *
 *  The returned `std::function<void(SLE::ref)>` sets `sfOwner = account` on
 *  the newly allocated `ltDIR_NODE` SLE.  It is passed as the `describe`
 *  argument to `ApplyView::dirInsert` throughout the codebase (e.g.,
 *  `RippleStateHelpers.cpp`, `PaymentChannelCreate.cpp`) and is invoked only
 *  when `dirInsert` actually allocates a fresh overflow page, keeping the
 *  owning account ID out of the generic insertion logic.
 *
 *  @param account The `AccountID` to record as `sfOwner` on each new page.
 *  @return A callable suitable for the `describe` parameter of
 *      `ApplyView::dirInsert`.
 */
[[nodiscard]] std::function<void(SLE::ref)>
describeOwnerDir(AccountID const& account);

}  // namespace xrpl
