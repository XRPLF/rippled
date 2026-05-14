/** @file
 *  Implements the XRPL ledger directory data structure.
 *
 *  A directory is a circular doubly-linked list of `ltDIR_NODE` pages used to
 *  associate sets of ledger object keys with an index.  Two flavors exist:
 *  owner directories (all objects owned by one account) and book directories
 *  (all open offers at one price point).  This file provides `ApplyView`'s
 *  `dirAdd`, `dirRemove`, `emptyDirDelete`, and `dirDelete` implementations,
 *  plus the low-level helpers in `namespace xrpl::directory`.
 */
#include <xrpl/ledger/ApplyView.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STVector256.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>

namespace xrpl {

namespace directory {

/** Bootstrap a brand-new single-entry directory.
 *
 *  Allocates the root `ltDIR_NODE` SLE at `directory`, stamps it with
 *  `sfRootIndex`, invokes `describe` so callers can inject type-specific
 *  fields (e.g. `sfOwner` for owner directories, `sfTakerPays`/`sfTakerGets`
 *  for book directories), pushes `key` as the sole entry, and inserts the SLE
 *  into `view`.
 *
 *  @param view      The mutable ledger view to insert into.
 *  @param directory Keylet of the root page (page 0) to create.
 *  @param key       The first entry to store in the new directory.
 *  @param describe  Callback that stamps type-specific fields onto every new
 *      page SLE; called once here for the root page.
 *  @return Always returns 0, the page number of the root page.
 */
std::uint64_t
createRoot(
    ApplyView& view,
    Keylet const& directory,
    uint256 const& key,
    std::function<void(std::shared_ptr<SLE> const&)> const& describe)
{
    auto newRoot = std::make_shared<SLE>(directory);
    newRoot->setFieldH256(sfRootIndex, directory.key);
    describe(newRoot);

    STVector256 v;
    v.pushBack(key);
    newRoot->setFieldV256(sfIndexes, v);

    view.insert(newRoot);
    return std::uint64_t{0};
}

/** Locate the last page of a directory in O(1) time.
 *
 *  The root's `sfIndexPrevious` always points to the tail page, so no
 *  traversal is needed regardless of chain length.  If `sfIndexPrevious` is
 *  non-zero but the referenced page cannot be found, the directory is
 *  structurally corrupt and `LogicError` terminates the process.
 *
 *  @param view      The mutable ledger view to read from.
 *  @param directory Keylet of the directory root (used to build page keylets).
 *  @param start     The root page SLE (page 0).
 *  @return A tuple of `(pageNumber, pageSLE, sfIndexes)` for the last page.
 *  @throw std::logic_error if `sfIndexPrevious` is non-zero but the page is
 *      absent — indicates corrupted ledger state, not a recoverable error.
 */
auto
findPreviousPage(ApplyView& view, Keylet const& directory, SLE::ref start)
{
    std::uint64_t const page = start->getFieldU64(sfIndexPrevious);

    auto node = start;

    if (page != 0u)
    {
        node = view.peek(keylet::page(directory, page));
        if (!node)
        {
            Throw<std::logic_error>(
                "Directory chain: root back-pointer broken.");  // LCOV_EXCL_LINE
        }
    }

    auto indexes = node->getFieldV256(sfIndexes);
    return std::make_tuple(page, node, indexes);
}

/** Insert `key` into an existing directory page and write it back.
 *
 *  Two insertion strategies are controlled by `preserveOrder`:
 *  - **`true`** (book directories / `dirAppend`): appends at the tail,
 *    maintaining insertion order.  Duplicates call `LogicError`.
 *  - **`false`** (owner directories / `dirInsert`): sorts the page first
 *    (a legacy concession — older code may not have maintained sort order),
 *    then binary-searches for the insertion point.  Duplicates call
 *    `LogicError`.
 *
 *  @param view          The mutable ledger view.
 *  @param node          The page SLE to modify (obtained via `peek`).
 *  @param page          The page number; returned unchanged.
 *  @param preserveOrder If `true`, append; if `false`, sort then insert.
 *  @param indexes       The current `sfIndexes` vector (modified in place).
 *  @param key           The key to insert.
 *  @return The page number `page` (unchanged).
 *  @throw std::logic_error if `key` is already present — double-insertion is
 *      a programming error, not a protocol error.
 */
std::uint64_t
insertKey(
    ApplyView& view,
    SLE::ref node,
    std::uint64_t page,
    bool preserveOrder,
    STVector256& indexes,
    uint256 const& key)
{
    if (preserveOrder)
    {
        if (std::ranges::find(indexes, key) != indexes.end())
            Throw<std::logic_error>("dirInsert: double insertion");  // LCOV_EXCL_LINE

        indexes.pushBack(key);
    }
    else
    {
        // We can't be sure if this page is already sorted because it may be a
        // legacy page we haven't yet touched. Take the time to sort it.
        std::ranges::sort(indexes);

        auto pos = std::ranges::lower_bound(indexes, key);

        if (pos != indexes.end() && key == *pos)
            Throw<std::logic_error>("dirInsert: double insertion");  // LCOV_EXCL_LINE

        indexes.insert(pos, key);
    }

    node->setFieldV256(sfIndexes, indexes);
    view.update(node);
    return page;
}

/** Append a new trailing page to a full directory and insert `key` into it.
 *
 *  Increments `page` to produce the new page number, links the new page at
 *  the tail (updating both the former-last page's `sfIndexNext` and the root's
 *  `sfIndexPrevious`), then inserts `key` as the sole entry.
 *
 *  Page-counter overflow is detected via deliberate `uint64_t` modulo
 *  wraparound (defined behavior for unsigned integers per
 *  [basic.fundamental] ¶2).  Two `static_assert` guards confirm at compile
 *  time that the type is unsigned and that `max + 1 == 0`.  When the counter
 *  wraps to zero, or when the pre-`fixDirectoryLimit` ceiling
 *  (`kDIR_NODE_MAX_PAGES`) is reached, `std::nullopt` is returned — the
 *  caller surfaces `tecDIR_FULL` to the transaction.
 *
 *  `nextPage` is always passed as 0 (appends only); the commented-out block
 *  for setting `sfIndexNext` is reserved for a hypothetical future
 *  insert-in-middle operation.
 *
 *  @param view      The mutable ledger view.
 *  @param page      The current last page number; incremented to get the new
 *      page number.
 *  @param node      The current last page SLE (its `sfIndexNext` is updated).
 *  @param nextPage  Reserved; must be 0.
 *  @param next      The root page SLE (its `sfIndexPrevious` is updated).
 *  @param key       The key to store in the new page.
 *  @param directory Keylet of the directory root.
 *  @param describe  Callback to stamp type-specific fields on the new page.
 *  @return The new page number on success, or `std::nullopt` if the directory
 *      has reached its maximum page capacity.
 */
std::optional<std::uint64_t>
insertPage(
    ApplyView& view,
    std::uint64_t page,
    SLE::pointer node,
    std::uint64_t nextPage,
    SLE::ref next,
    uint256 const& key,
    Keylet const& directory,
    std::function<void(std::shared_ptr<SLE> const&)> const& describe)
{
    // We rely on modulo arithmetic of unsigned integers (guaranteed in
    // [basic.fundamental] paragraph 2) to detect page representation overflow.
    // For signed integers this would be UB, hence static_assert here.
    static_assert(std::is_unsigned_v<decltype(page)>);
    // Defensive check against breaking changes in compiler.
    static_assert([]<typename T>(std::type_identity<T>) constexpr -> T {
        T tmp = std::numeric_limits<T>::max();
        return ++tmp;
    }(std::type_identity<decltype(page)>{}) == 0);
    ++page;
    // Check whether we're out of pages.
    if (page == 0)
        return std::nullopt;
    if (!view.rules().enabled(fixDirectoryLimit) && page >= kDIR_NODE_MAX_PAGES)  // Old pages limit
        return std::nullopt;

    // We are about to create a new node; we'll link it to
    // the chain first:
    node->setFieldU64(sfIndexNext, page);
    view.update(node);

    next->setFieldU64(sfIndexPrevious, page);
    view.update(next);

    // Insert the new key:
    STVector256 indexes;
    indexes.pushBack(key);

    node = std::make_shared<SLE>(keylet::page(directory, page));
    node->setFieldH256(sfRootIndex, directory.key);
    node->setFieldV256(sfIndexes, indexes);

    // Save some space by not specifying the value 0 since it's the default.
    if (page != 1)
        node->setFieldU64(sfIndexPrevious, page - 1);
    XRPL_ASSERT_PARTS(!nextPage, "xrpl::directory::insertPage", "nextPage has default value");
    /* Reserved for future use when directory pages may be inserted in
     * between two other pages instead of only at the end of the chain.
    if (nextPage)
        node->setFieldU64(sfIndexNext, nextPage);
    */
    describe(node);
    view.insert(node);

    return page;
}

}  // namespace directory

/** Single entry point for all directory insertions.
 *
 *  Dispatches to one of three helpers depending on the current state of the
 *  directory:
 *  1. No root present → `createRoot()` (first insertion ever).
 *  2. Last page has room → `insertKey()` (fast path, no new SLE).
 *  3. Last page is full → `insertPage()` (allocates a new trailing page).
 *
 *  @param preserveOrder `true` for book directories (append order); `false`
 *      for owner directories (sorted order).
 *  @param directory Keylet of the directory root.
 *  @param key       The `uint256` entry to insert.
 *  @param describe  Callback to stamp type-specific fields on any new page.
 *  @return The page number into which `key` was inserted, or `std::nullopt`
 *      if the directory has reached its maximum page capacity.
 */
std::optional<std::uint64_t>
ApplyView::dirAdd(
    bool preserveOrder,
    Keylet const& directory,
    uint256 const& key,
    std::function<void(std::shared_ptr<SLE> const&)> const& describe)
{
    auto root = peek(directory);

    if (!root)
    {
        // No root, make it.
        return directory::createRoot(*this, directory, key, describe);
    }

    auto [page, node, indexes] = directory::findPreviousPage(*this, directory, root);

    // If there's space, we use it:
    if (indexes.size() < kDIR_NODE_MAX_ENTRIES)
    {
        return directory::insertKey(*this, node, page, preserveOrder, indexes, key);
    }

    return directory::insertPage(*this, page, node, 0, root, key, directory, describe);
}

/** Delete a directory root that is already empty, handling one legacy edge case.
 *
 *  Validates that `directory` refers to an `ltDIR_NODE` whose `sfRootIndex`
 *  matches its own key (i.e. it really is a root, not an interior page).
 *  Returns `false` without erasing if the root's `sfIndexes` is non-empty.
 *
 *  **Legacy cleanup:** older code occasionally left the last page empty rather
 *  than deleting it.  If the root's `sfIndexNext` and `sfIndexPrevious` both
 *  point to the same non-root page, and that page is empty, this function
 *  unlinks and erases it before proceeding to erase the root.
 *
 *  Structural invariants are asserted with `LogicError` (not recoverable):
 *  a forward link without a matching reverse link, or vice versa, indicates
 *  corrupted ledger state.
 *
 *  @param directory Keylet of the root page (page 0) of the directory.
 *  @return `true` if the directory was found and successfully erased;
 *      `false` if not found or still contains entries.
 *  @note Only call this with the root keylet.  Passing an interior page
 *      keylet triggers `UNREACHABLE` and returns `false`.
 */
bool
ApplyView::emptyDirDelete(Keylet const& directory)
{
    auto node = peek(directory);

    if (!node)
        return false;

    // Verify that the passed directory node is the directory root.
    if (directory.type != ltDIR_NODE || node->getFieldH256(sfRootIndex) != directory.key)
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::ApplyView::emptyDirDelete : invalid node type");
        return false;
        // LCOV_EXCL_STOP
    }

    // The directory still contains entries and so it cannot be removed
    if (!node->getFieldV256(sfIndexes).empty())
        return false;

    std::uint64_t constexpr kROOT_PAGE = 0;
    auto prevPage = node->getFieldU64(sfIndexPrevious);
    auto nextPage = node->getFieldU64(sfIndexNext);

    if (nextPage == kROOT_PAGE && prevPage != kROOT_PAGE)
        Throw<std::logic_error>("Directory chain: fwd link broken");  // LCOV_EXCL_LINE

    if (prevPage == kROOT_PAGE && nextPage != kROOT_PAGE)
        Throw<std::logic_error>("Directory chain: rev link broken");  // LCOV_EXCL_LINE

    // Older versions of the code would, in some cases, allow the last
    // page to be empty. Remove such pages:
    if (nextPage == prevPage && nextPage != kROOT_PAGE)
    {
        auto last = peek(keylet::page(directory, nextPage));

        if (!last)
            Throw<std::logic_error>("Directory chain: fwd link broken.");  // LCOV_EXCL_LINE

        if (!last->getFieldV256(sfIndexes).empty())
            return false;

        // Update the first page's linked list and
        // mark it as updated.
        node->setFieldU64(sfIndexNext, kROOT_PAGE);
        node->setFieldU64(sfIndexPrevious, kROOT_PAGE);
        update(node);

        // And erase the empty last page:
        erase(last);

        // Make sure our local values reflect the
        // updated information:
        nextPage = kROOT_PAGE;
        prevPage = kROOT_PAGE;
    }

    // If there are no other pages, erase the root:
    if (nextPage == kROOT_PAGE && prevPage == kROOT_PAGE)
        erase(node);

    return true;
}

/** Remove one key from a directory, cleaning up empty pages as needed.
 *
 *  Peeks the page identified by `page`, finds `key` in `sfIndexes`, erases it
 *  (preserving the relative order of remaining keys), and writes the page
 *  back.  If the page is still non-empty, returns immediately.
 *
 *  **Empty-page cleanup** follows two distinct paths:
 *  - **Root page (page 0):** never deleted when `keepRoot` is `true`; erased
 *    only when `keepRoot` is `false` and no other pages remain.  A legacy
 *    empty trailing page (`sfIndexNext == sfIndexPrevious != 0`) is also
 *    reaped here.
 *  - **Non-root page:** unlinked from both its predecessor and successor,
 *    then erased.  An additional check reaps the new last page if it happens
 *    to be empty (another legacy artifact).  If the chain has now collapsed
 *    to only an empty root and `keepRoot` is `false`, the root is erased too.
 *
 *  Any structural inconsistency (missing neighbor page, self-referential link
 *  on a non-root node, asymmetric forward/reverse links) calls `LogicError`
 *  and terminates — these paths are `LCOV_EXCL_*`-marked because they are
 *  only reachable via corrupted ledger state.
 *
 *  @param directory Keylet of the directory root.
 *  @param page      Page number where `key` resides (stored by the caller
 *      alongside the owning ledger entry at insertion time).
 *  @param key       The `uint256` entry to remove.
 *  @param keepRoot  If `true`, retain the root page even when it becomes
 *      empty (used while the owning account still exists).
 *  @return `true` if `key` was found and removed; `false` if the page or key
 *      was not found.
 */
bool
ApplyView::dirRemove(Keylet const& directory, std::uint64_t page, uint256 const& key, bool keepRoot)
{
    auto node = peek(keylet::page(directory, page));

    if (!node)
        return false;

    std::uint64_t constexpr kROOT_PAGE = 0;

    {
        auto entries = node->getFieldV256(sfIndexes);

        auto it = std::ranges::find(entries, key);

        if (entries.end() == it)
            return false;

        // We always preserve the relative order when we remove.
        entries.erase(it);

        node->setFieldV256(sfIndexes, entries);
        update(node);

        if (!entries.empty())
            return true;
    }

    // The current page is now empty; check if it can be deleted, and, if so,
    // whether the entire directory can now be removed.
    auto prevPage = node->getFieldU64(sfIndexPrevious);
    auto nextPage = node->getFieldU64(sfIndexNext);

    // The first page is the directory's root node and is
    // treated specially: it can never be deleted even if
    // it is empty, unless we plan on removing the entire
    // directory.
    if (page == kROOT_PAGE)
    {
        if (nextPage == page && prevPage != page)
            Throw<std::logic_error>("Directory chain: fwd link broken");  // LCOV_EXCL_LINE

        if (prevPage == page && nextPage != page)
            Throw<std::logic_error>("Directory chain: rev link broken");  // LCOV_EXCL_LINE

        // Older versions of the code would, in some cases, allow the last page
        // to be empty. Remove such pages if we stumble on them:
        if (nextPage == prevPage && nextPage != page)
        {
            auto last = peek(keylet::page(directory, nextPage));
            if (!last)
                Throw<std::logic_error>("Directory chain: fwd link broken.");  // LCOV_EXCL_LINE

            if (last->getFieldV256(sfIndexes).empty())
            {
                // Update the first page's linked list and mark it as updated.
                node->setFieldU64(sfIndexNext, page);
                node->setFieldU64(sfIndexPrevious, page);
                update(node);

                // And erase the empty last page:
                erase(last);

                // Make sure our local values reflect the updated information:
                nextPage = page;
                prevPage = page;
            }
        }

        if (keepRoot)
            return true;

        // If there's no other pages, erase the root:
        if (nextPage == page && prevPage == page)
            erase(node);

        return true;
    }

    // This can never happen for nodes other than the root:
    if (nextPage == page)
        Throw<std::logic_error>("Directory chain: fwd link broken");  // LCOV_EXCL_LINE

    if (prevPage == page)
        Throw<std::logic_error>("Directory chain: rev link broken");  // LCOV_EXCL_LINE

    // This node isn't the root, so it can either be in the middle of the list,
    // or at the end. Unlink it first and then check if that leaves the list
    // with only a root:
    auto prev = peek(keylet::page(directory, prevPage));
    if (!prev)
        Throw<std::logic_error>("Directory chain: fwd link broken.");  // LCOV_EXCL_LINE
    // Fix previous to point to its new next.
    prev->setFieldU64(sfIndexNext, nextPage);
    update(prev);

    auto next = peek(keylet::page(directory, nextPage));
    if (!next)
        Throw<std::logic_error>("Directory chain: rev link broken.");  // LCOV_EXCL_LINE
    // Fix next to point to its new previous.
    next->setFieldU64(sfIndexPrevious, prevPage);
    update(next);

    // The page is no longer linked. Delete it.
    erase(node);

    // Check whether the next page is the last page and, if
    // so, whether it's empty. If it is, delete it.
    if (nextPage != kROOT_PAGE && next->getFieldU64(sfIndexNext) == kROOT_PAGE &&
        next->getFieldV256(sfIndexes).empty())
    {
        // Since next doesn't point to the root, it can't be pointing to prev.
        erase(next);

        // The previous page is now the last page:
        prev->setFieldU64(sfIndexNext, kROOT_PAGE);
        update(prev);

        // And the root points to the last page:
        auto root = peek(keylet::page(directory, kROOT_PAGE));
        if (!root)
            Throw<std::logic_error>("Directory chain: root link broken.");  // LCOV_EXCL_LINE

        root->setFieldU64(sfIndexPrevious, prevPage);
        update(root);

        nextPage = kROOT_PAGE;
    }

    // If we're not keeping the root, then check to see if
    // it's left empty. If so, delete it as well.
    if (!keepRoot && nextPage == kROOT_PAGE && prevPage == kROOT_PAGE)
    {
        if (prev->getFieldV256(sfIndexes).empty())
            erase(prev);
    }

    return true;
}

/** Bulk-destroy a directory, invoking `callback` for every stored key.
 *
 *  Walks the page chain via `sfIndexNext` starting at page 0, calling
 *  `callback` for each `uint256` entry on every page, then erasing each page.
 *  Used when an entire directory must be torn down — for example, during
 *  account deletion.
 *
 *  @param directory Keylet of the directory root.
 *  @param callback  Invoked once per stored key before its page is erased;
 *      allows callers to perform per-object cleanup (e.g. releasing reserves).
 *  @return `true` if the root page was found and all pages were erased;
 *      `false` if the root page was not present.
 */
bool
ApplyView::dirDelete(Keylet const& directory, std::function<void(uint256 const&)> const& callback)
{
    std::optional<std::uint64_t> pi;

    do
    {
        auto const page = peek(keylet::page(directory, pi.value_or(0)));

        if (!page)
            return false;

        for (auto const& item : page->getFieldV256(sfIndexes))
            callback(item);

        pi = (*page)[~sfIndexNext];

        erase(page);
    } while (pi);

    return true;
}

}  // namespace xrpl
