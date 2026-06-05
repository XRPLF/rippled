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
    SLE::const_pointer& page,
    unsigned int& index,
    uint256& entry);

bool
dirFirst(
    ApplyView& view,
    uint256 const& root,
    SLE::pointer& page,
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
    SLE::const_pointer& page,
    unsigned int& index,
    uint256& entry);

bool
dirNext(
    ApplyView& view,
    uint256 const& root,
    SLE::pointer& page,
    unsigned int& index,
    uint256& entry);
/** @} */

/** Iterate all items in the given directory. */
void
forEachItem(ReadView const& view, Keylet const& root, std::function<void(SLE::const_ref)> const& f);

/** Iterate all items after an item in the given directory.
    @param after The key of the item to start after
    @param hint The directory page containing `after`
    @param limit The maximum number of items to return
    @return `false` if the iteration failed
*/
bool
forEachItemAfter(
    ReadView const& view,
    Keylet const& root,
    uint256 const& after,
    std::uint64_t const hint,
    unsigned int limit,
    std::function<bool(SLE::const_ref)> const& f);

/** Iterate all items in an account's owner directory. */
inline void
forEachItem(ReadView const& view, AccountID const& id, std::function<void(SLE::const_ref)> const& f)
{
    forEachItem(view, keylet::ownerDir(id), f);
}

/** Iterate all items after an item in an owner directory.
    @param after The key of the item to start after
    @param hint The directory page containing `after`
    @param limit The maximum number of items to return
    @return `false` if the iteration failed
*/
inline bool
forEachItemAfter(
    ReadView const& view,
    AccountID const& id,
    uint256 const& after,
    std::uint64_t const hint,
    unsigned int limit,
    std::function<bool(SLE::const_ref)> const& f)
{
    return forEachItemAfter(view, keylet::ownerDir(id), after, hint, limit, f);
}

/** Returns `true` if the directory is empty
    @param key The key of the directory
*/
[[nodiscard]] bool
dirIsEmpty(ReadView const& view, Keylet const& k);

/** Returns a function that sets the owner on a directory SLE */
[[nodiscard]] std::function<void(SLE::ref)>
describeOwnerDir(AccountID const& account);

}  // namespace xrpl
