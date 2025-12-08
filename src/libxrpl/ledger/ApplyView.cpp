#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/Protocol.h>

#include <limits>
#include <type_traits>

namespace ripple {

namespace directory {

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
    v.push_back(key);
    newRoot->setFieldV256(sfIndexes, v);

    view.insert(newRoot);
    return std::uint64_t{0};
}

auto
findPreviousPage(ApplyView& view, Keylet const& directory, SLE::ref start)
{
    std::uint64_t page = start->getFieldU64(sfIndexPrevious);

    auto node = start;

    if (page)
    {
        node = view.peek(keylet::page(directory, page));
        if (!node)
        {  // LCOV_EXCL_START
            LogicError("Directory chain: root back-pointer broken.");
            // LCOV_EXCL_STOP
        }
    }

    auto indexes = node->getFieldV256(sfIndexes);
    return std::make_tuple(page, node, indexes);
}

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
        if (std::find(indexes.begin(), indexes.end(), key) != indexes.end())
            LogicError("dirInsert: double insertion");  // LCOV_EXCL_LINE

        indexes.push_back(key);
    }
    else
    {
        // We can't be sure if this page is already sorted because
        // it may be a legacy page we haven't yet touched. Take
        // the time to sort it.
        std::sort(indexes.begin(), indexes.end());

        auto pos = std::lower_bound(indexes.begin(), indexes.end(), key);

        if (pos != indexes.end() && key == *pos)
            LogicError("dirInsert: double insertion");  // LCOV_EXCL_LINE

        indexes.insert(pos, key);
    }

    node->setFieldV256(sfIndexes, indexes);
    view.update(node);
    return page;
}

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
    if (!view.rules().enabled(fixDirectoryLimit) &&
        page >= dirNodeMaxPages)  // Old pages limit
        return std::nullopt;

    // We are about to create a new node; we'll link it to
    // the chain first:
    node->setFieldU64(sfIndexNext, page);
    view.update(node);

    next->setFieldU64(sfIndexPrevious, page);
    view.update(next);

    // Insert the new key:
    STVector256 indexes;
    indexes.push_back(key);

    node = std::make_shared<SLE>(keylet::page(directory, page));
    node->setFieldH256(sfRootIndex, directory.key);
    node->setFieldV256(sfIndexes, indexes);

    // Save some space by not specifying the value 0 since
    // it's the default.
    if (page != 1)
        node->setFieldU64(sfIndexPrevious, page - 1);
    XRPL_ASSERT_PARTS(
        !nextPage,
        "ripple::directory::insertPage",
        "nextPage has default value");
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

    auto [page, node, indexes] =
        directory::findPreviousPage(*this, directory, root);

    // If there's space, we use it:
    if (indexes.size() < dirNodeMaxEntries)
    {
        return directory::insertKey(
            *this, node, page, preserveOrder, indexes, key);
    }

    return directory::insertPage(
        *this, page, node, 0, root, key, directory, describe);
}

bool
ApplyView::emptyDirDelete(Keylet const& directory)
{
    auto node = peek(directory);

    if (!node)
        return false;

    // Verify that the passed directory node is the directory root.
    if (directory.type != ltDIR_NODE ||
        node->getFieldH256(sfRootIndex) != directory.key)
    {
        // LCOV_EXCL_START
        UNREACHABLE("ripple::ApplyView::emptyDirDelete : invalid node type");
        return false;
        // LCOV_EXCL_STOP
    }

    // The directory still contains entries and so it cannot be removed
    if (!node->getFieldV256(sfIndexes).empty())
        return false;

    std::uint64_t constexpr rootPage = 0;
    auto prevPage = node->getFieldU64(sfIndexPrevious);
    auto nextPage = node->getFieldU64(sfIndexNext);

    if (nextPage == rootPage && prevPage != rootPage)
        LogicError("Directory chain: fwd link broken");  // LCOV_EXCL_LINE

    if (prevPage == rootPage && nextPage != rootPage)
        LogicError("Directory chain: rev link broken");  // LCOV_EXCL_LINE

    // Older versions of the code would, in some cases, allow the last
    // page to be empty. Remove such pages:
    if (nextPage == prevPage && nextPage != rootPage)
    {
        auto last = peek(keylet::page(directory, nextPage));

        if (!last)
        {  // LCOV_EXCL_START
            LogicError("Directory chain: fwd link broken.");
            // LCOV_EXCL_STOP
        }

        if (!last->getFieldV256(sfIndexes).empty())
            return false;

        // Update the first page's linked list and
        // mark it as updated.
        node->setFieldU64(sfIndexNext, rootPage);
        node->setFieldU64(sfIndexPrevious, rootPage);
        update(node);

        // And erase the empty last page:
        erase(last);

        // Make sure our local values reflect the
        // updated information:
        nextPage = rootPage;
        prevPage = rootPage;
    }

    // If there are no other pages, erase the root:
    if (nextPage == rootPage && prevPage == rootPage)
        erase(node);

    return true;
}

bool
ApplyView::dirRemove(
    Keylet const& directory,
    std::uint64_t page,
    uint256 const& key,
    bool keepRoot)
{
    auto node = peek(keylet::page(directory, page));

    if (!node)
        return false;

    std::uint64_t constexpr rootPage = 0;

    {
        auto entries = node->getFieldV256(sfIndexes);

        auto it = std::find(entries.begin(), entries.end(), key);

        if (entries.end() == it)
            return false;

        // We always preserve the relative order when we remove.
        entries.erase(it);

        node->setFieldV256(sfIndexes, entries);
        update(node);

        if (!entries.empty())
            return true;
    }

    // The current page is now empty; check if it can be
    // deleted, and, if so, whether the entire directory
    // can now be removed.
    auto prevPage = node->getFieldU64(sfIndexPrevious);
    auto nextPage = node->getFieldU64(sfIndexNext);

    // The first page is the directory's root node and is
    // treated specially: it can never be deleted even if
    // it is empty, unless we plan on removing the entire
    // directory.
    if (page == rootPage)
    {
        if (nextPage == page && prevPage != page)
        {  // LCOV_EXCL_START
            LogicError("Directory chain: fwd link broken");
            // LCOV_EXCL_STOP
        }

        if (prevPage == page && nextPage != page)
        {  // LCOV_EXCL_START
            LogicError("Directory chain: rev link broken");
            // LCOV_EXCL_STOP
        }

        // Older versions of the code would, in some cases,
        // allow the last page to be empty. Remove such
        // pages if we stumble on them:
        if (nextPage == prevPage && nextPage != page)
        {
            auto last = peek(keylet::page(directory, nextPage));
            if (!last)
            {  // LCOV_EXCL_START
                LogicError("Directory chain: fwd link broken.");
                // LCOV_EXCL_STOP
            }

            if (last->getFieldV256(sfIndexes).empty())
            {
                // Update the first page's linked list and
                // mark it as updated.
                node->setFieldU64(sfIndexNext, page);
                node->setFieldU64(sfIndexPrevious, page);
                update(node);

                // And erase the empty last page:
                erase(last);

                // Make sure our local values reflect the
                // updated information:
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
        LogicError("Directory chain: fwd link broken");  // LCOV_EXCL_LINE

    if (prevPage == page)
        LogicError("Directory chain: rev link broken");  // LCOV_EXCL_LINE

    // This node isn't the root, so it can either be in the
    // middle of the list, or at the end. Unlink it first
    // and then check if that leaves the list with only a
    // root:
    auto prev = peek(keylet::page(directory, prevPage));
    if (!prev)
        LogicError("Directory chain: fwd link broken.");  // LCOV_EXCL_LINE
    // Fix previous to point to its new next.
    prev->setFieldU64(sfIndexNext, nextPage);
    update(prev);

    auto next = peek(keylet::page(directory, nextPage));
    if (!next)
        LogicError("Directory chain: rev link broken.");  // LCOV_EXCL_LINE
    // Fix next to point to its new previous.
    next->setFieldU64(sfIndexPrevious, prevPage);
    update(next);

    // The page is no longer linked. Delete it.
    erase(node);

    // Check whether the next page is the last page and, if
    // so, whether it's empty. If it is, delete it.
    if (nextPage != rootPage && next->getFieldU64(sfIndexNext) == rootPage &&
        next->getFieldV256(sfIndexes).empty())
    {
        // Since next doesn't point to the root, it
        // can't be pointing to prev.
        erase(next);

        // The previous page is now the last page:
        prev->setFieldU64(sfIndexNext, rootPage);
        update(prev);

        // And the root points to the last page:
        auto root = peek(keylet::page(directory, rootPage));
        if (!root)
        {  // LCOV_EXCL_START
            LogicError("Directory chain: root link broken.");
            // LCOV_EXCL_STOP
        }
        root->setFieldU64(sfIndexPrevious, prevPage);
        update(root);

        nextPage = rootPage;
    }

    // If we're not keeping the root, then check to see if
    // it's left empty. If so, delete it as well.
    if (!keepRoot && nextPage == rootPage && prevPage == rootPage)
    {
        if (prev->getFieldV256(sfIndexes).empty())
            erase(prev);
    }

    return true;
}

bool
ApplyView::dirDelete(
    Keylet const& directory,
    std::function<void(uint256 const&)> const& callback)
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

}  // namespace ripple
