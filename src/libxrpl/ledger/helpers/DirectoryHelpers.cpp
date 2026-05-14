/** @file
 *  Low-level traversal utilities for ledger directory nodes (`ltDIR_NODE`).
 *
 *  Directory nodes are linked-list structures that associate a root keylet
 *  with a paged sequence of ledger-object keys (`sfIndexes`).  This file
 *  provides the concrete implementations of the traversal helpers declared
 *  in `DirectoryHelpers.h`: the deprecated step-iterator wrappers
 *  (`dirFirst`/`dirNext`/`cdirFirst`/`cdirNext`), the exhaustive and
 *  cursor-paginated higher-order walkers (`forEachItem`,
 *  `forEachItemAfter`), the emptiness predicate (`dirIsEmpty`), and the
 *  new-page initialisation factory (`describeOwnerDir`).
 *
 *  New code should prefer the `Dir` range adaptor over the step-iterator
 *  API.
 */

#include <xrpl/ledger/helpers/DirectoryHelpers.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <cstdint>
#include <functional>
#include <memory>

namespace xrpl {

// --- Deprecated step-iterator wrappers ---
// dirFirst/dirNext (mutable) and cdirFirst/cdirNext (read-only) are thin
// forwarding shells.  The shared template implementations
// detail::internalDirFirst and detail::internalDirNext select between
// view.peek() and view.read() at compile time based on whether the SLE
// pointer is const, preserving type safety without code duplication.

bool
dirFirst(
    ApplyView& view,
    uint256 const& root,
    std::shared_ptr<SLE>& page,
    unsigned int& index,
    uint256& entry)
{
    return detail::internalDirFirst(view, root, page, index, entry);
}

bool
dirNext(
    ApplyView& view,
    uint256 const& root,
    std::shared_ptr<SLE>& page,
    unsigned int& index,
    uint256& entry)
{
    return detail::internalDirNext(view, root, page, index, entry);
}

bool
cdirFirst(
    ReadView const& view,
    uint256 const& root,
    std::shared_ptr<SLE const>& page,
    unsigned int& index,
    uint256& entry)
{
    return detail::internalDirFirst(view, root, page, index, entry);
}

bool
cdirNext(
    ReadView const& view,
    uint256 const& root,
    std::shared_ptr<SLE const>& page,
    unsigned int& index,
    uint256& entry)
{
    return detail::internalDirNext(view, root, page, index, entry);
}

/** Exhaustively walks every page of a directory, invoking @p f for every
 *  child SLE in `sfIndexes` order.
 *
 *  Iteration terminates when `sfIndexNext` is zero (end of chain) or when
 *  a page SLE is missing — missing pages are treated as end-of-directory
 *  rather than an error.  There is no early-exit mechanism; the callback's
 *  return type is `void`.
 *
 *  A two-tier guard enforces the `ltDIR_NODE` precondition: an
 *  `XRPL_ASSERT` fires in instrumented builds while an explicit `if` guard
 *  silently returns in release builds, preventing a crash on stale data.
 *
 *  @param view The read-only ledger view to query.
 *  @param root Keylet of the directory's root (anchor) page; must have
 *      type `ltDIR_NODE`.
 *  @param f Callback invoked with each child SLE (may be `nullptr` if the
 *      child key is not present in the view).
 */
void
forEachItem(
    ReadView const& view,
    Keylet const& root,
    std::function<void(std::shared_ptr<SLE const> const&)> const& f)
{
    XRPL_ASSERT(root.type == ltDIR_NODE, "xrpl::forEachItem : valid root type");

    if (root.type != ltDIR_NODE)
        return;

    auto pos = root;

    while (true)
    {
        auto sle = view.read(pos);
        if (!sle)
            return;
        for (auto const& key : sle->getFieldV256(sfIndexes))
            f(view.read(keylet::child(key)));
        auto const next = sle->getFieldU64(sfIndexNext);
        if (next == 0u)
            return;
        pos = keylet::page(root, next);
    }
}

/** Cursor-paginated directory walk used by RPC handlers such as
 *  `account_offers`, `account_lines`, and `account_channels`.
 *
 *  When @p after is non-zero the function first tries the @p hint page
 *  to locate the cursor without a full linear scan (the common case for
 *  well-behaved clients that persist the hint from the previous response).
 *  If the hint is stale or wrong the search falls back to a linear scan
 *  from the root page until @p after is found.
 *
 *  The callback @p f receives each child SLE that comes after the cursor
 *  and returns `bool`: `true` to continue iteration, `false` to stop early
 *  regardless of @p limit.  The @p limit counter is decremented on each
 *  `true`-returning call; when it reaches one the walk stops — callers
 *  conventionally request `limit + 1` items and detect a non-empty next
 *  page by checking whether exactly `limit + 1` items were delivered.
 *
 *  When @p after is zero the function starts from the root page and always
 *  returns `true` (modulo missing SLEs), so the return value is only
 *  meaningful as a cursor-validity signal in the paginated case.
 *
 *  @param view The read-only ledger view to query.
 *  @param root Keylet of the directory's root page; must have type
 *      `ltDIR_NODE`.
 *  @param after Cursor key: iteration delivers only items that follow this
 *      key in directory order.  Pass `uint256()` (zero) to start from the
 *      beginning.
 *  @param hint Page number expected to contain @p after, used as a fast-
 *      path optimisation; ignored when @p after is zero.
 *  @param limit Maximum number of callback invocations before stopping;
 *      the walk stops when the callback returns `true` and this count
 *      reaches one.
 *  @param f Callback invoked for each qualifying child SLE (may be
 *      `nullptr` if the child key is absent in the view).  Return `true`
 *      to continue, `false` to stop early.
 *  @return `true` if the @p after key was found (or @p after is zero);
 *      `false` if the cursor key was never located, which indicates a
 *      stale or invalid marker that callers should surface as an error.
 */
bool
forEachItemAfter(
    ReadView const& view,
    Keylet const& root,
    uint256 const& after,
    std::uint64_t const hint,
    unsigned int limit,
    std::function<bool(std::shared_ptr<SLE const> const&)> const& f)
{
    XRPL_ASSERT(root.type == ltDIR_NODE, "xrpl::forEachItemAfter : valid root type");

    if (root.type != ltDIR_NODE)
        return false;

    auto currentIndex = root;

    // If startAfter is not zero try jumping to that page using the hint
    if (after.isNonZero())
    {
        auto const hintIndex = keylet::page(root, hint);

        if (auto hintDir = view.read(hintIndex))
        {
            for (auto const& key : hintDir->getFieldV256(sfIndexes))
            {
                if (key == after)
                {
                    // We found the hint, we can start here
                    currentIndex = hintIndex;
                    break;
                }
            }
        }

        bool found = false;
        for (;;)
        {
            auto const ownerDir = view.read(currentIndex);
            if (!ownerDir)
                return found;
            for (auto const& key : ownerDir->getFieldV256(sfIndexes))
            {
                if (!found)
                {
                    if (key == after)
                        found = true;
                }
                else if (f(view.read(keylet::child(key))) && limit-- <= 1)
                {
                    return found;
                }
            }

            auto const uNodeNext = ownerDir->getFieldU64(sfIndexNext);
            if (uNodeNext == 0)
                return found;
            currentIndex = keylet::page(root, uNodeNext);
        }
    }
    else
    {
        for (;;)
        {
            auto const ownerDir = view.read(currentIndex);
            if (!ownerDir)
                return true;
            for (auto const& key : ownerDir->getFieldV256(sfIndexes))
            {
                if (f(view.read(keylet::child(key))) && limit-- <= 1)
                    return true;
            }
            auto const uNodeNext = ownerDir->getFieldU64(sfIndexNext);
            if (uNodeNext == 0)
                return true;
            currentIndex = keylet::page(root, uNodeNext);
        }
    }
}

/** Returns `true` only when the directory contains no entries.
 *
 *  An empty `sfIndexes` array on the root page is necessary but not
 *  sufficient: the root is an anchor page and may legitimately have an
 *  empty index while `sfIndexNext` still points to a populated subsequent
 *  page.  Both conditions — empty `sfIndexes` *and* `sfIndexNext == 0` —
 *  must hold before declaring the directory empty.
 *
 *  A missing root SLE is treated as an empty directory.
 *
 *  @param view The read-only ledger view to query.
 *  @param k Keylet of the directory's root page.
 *  @return `true` if the directory has no entries; `false` otherwise.
 */
bool
dirIsEmpty(ReadView const& view, Keylet const& k)
{
    auto const sleNode = view.read(k);
    if (!sleNode)
        return true;
    if (!sleNode->getFieldV256(sfIndexes).empty())
        return false;
    // The root page may be empty while subsequent pages are not — check
    // sfIndexNext before concluding the directory is truly empty.
    return sleNode->getFieldU64(sfIndexNext) == 0;
}

/** Returns a callback that stamps a new directory page with @p account as
 *  its owner.
 *
 *  The callback is passed directly to `ApplyView::dirInsert`; whenever
 *  `dirInsert` allocates a new overflow page it calls this function to set
 *  `sfOwner` on the new `ltDIR_NODE` SLE.  This keeps the owning account
 *  ID out of the generic insertion logic while making the caller's intent
 *  explicit at the `dirInsert` call site.
 *
 *  @param account The `AccountID` to record on each new page.
 *  @return A `void(SLE::ref)` callable suitable for use as the `describe`
 *      argument to `ApplyView::dirInsert`.
 */
std::function<void(SLE::ref)>
describeOwnerDir(AccountID const& account)
{
    return [account](std::shared_ptr<SLE> const& sle) { (*sle)[sfOwner] = account; };
}

}  // namespace xrpl
