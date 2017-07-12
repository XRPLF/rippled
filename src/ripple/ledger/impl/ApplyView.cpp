//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <BeastConfig.h>
#include <ripple/ledger/ApplyView.h>
#include <ripple/basics/contract.h>
#include <ripple/protocol/Protocol.h>
#include <cassert>

namespace ripple {

boost::optional<std::uint64_t>
ApplyView::dirInsert (
    Keylet const& directory,
    uint256 const& key,
    bool strictOrder,
    std::function<void (std::shared_ptr<SLE> const&)> describe)
{
    auto root = peek(directory);

    if (! root)
    {
        // No root, make it.
        root = std::make_shared<SLE>(directory);
        root->setFieldH256 (sfRootIndex, directory.key);
        describe (root);

        STVector256 v;
        v.push_back (key);
        root->setFieldV256 (sfIndexes, v);

        insert (root);
        return std::uint64_t{0};
    }

    std::uint64_t page = root->getFieldU64(sfIndexPrevious);

    auto node = root;

    if (page)
    {
        node = peek (keylet::page(directory, page));
        if (!node)
            LogicError ("Directory chain: root back-pointer broken.");
    }

    auto indexes = node->getFieldV256(sfIndexes);

    // If there's space, we use it:
    if (indexes.size () < dirNodeMaxEntries)
    {
        if (!strictOrder)
        {
            // We can't be sure if this page is already sorted because
            // it may be a legacy page we haven't yet touched. Take
            // the time to sort it.
            std::sort (indexes.begin(), indexes.end());

            auto pos = std::lower_bound(indexes.begin(), indexes.end(), key);

            if (pos != indexes.end() && key == *pos)
                LogicError ("dirInsert: double insertion");

            indexes.insert (pos, key);
        }
        else
        {
            if (std::find(indexes.begin(), indexes.end(), key) != indexes.end())
                LogicError ("dirInsert: double insertion");

            indexes.push_back(key);
        }

        node->setFieldV256 (sfIndexes, indexes);
        update(node);

        return page;
    }

    // Check whether we're out of pages.
    if (++page >= dirNodeMaxPages)
        return boost::none;

    // We are about to create a new node; we'll link it to
    // the chain first:
    node->setFieldU64 (sfIndexNext, page);
    update(node);

    root->setFieldU64 (sfIndexPrevious, page);
    update(root);

    // Insert the new key:
    indexes.clear();
    indexes.push_back (key);

    node = std::make_shared<SLE>(keylet::page(directory, page));
    node->setFieldH256 (sfRootIndex, directory.key);
    node->setFieldV256 (sfIndexes, indexes);

    // Save some space by not specifying the value 0 since
    // it's the default.
    if (page != 1)
        node->setFieldU64 (sfIndexPrevious, page - 1);
    describe (node);
    insert (node);

    return page;
}

boost::optional<std::uint64_t>
ApplyView::dirInsert (
    Keylet const& directory,
    Keylet const& key,
    bool strictOrder,
    std::function<void (std::shared_ptr<SLE> const&)> describe)
{
    return dirInsert (directory, key.key, strictOrder, describe);
}

bool
ApplyView::dirRemove (
    Keylet const& directory,
    std::uint64_t currPage,
    uint256 const& key,
    bool keepRoot)
{
    auto node = peek(keylet::page(directory, currPage));

    if (!node)
        return false;

    std::uint64_t constexpr rootPage = 0;

    {
        auto entries = node->getFieldV256(sfIndexes);

        auto it = std::find(entries.begin(), entries.end(), key);

        if (entries.end () == it)
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
    if (currPage == rootPage)
    {
        if (nextPage == currPage && prevPage != currPage)
            LogicError ("Directory chain: fwd link broken");

        if (prevPage == currPage && nextPage != currPage)
            LogicError ("Directory chain: rev link broken");

        // Older versions of the code would, in some cases,
        // allow the last page to be empty. Remove such
        // pages if we stumble on them:
        if (nextPage == prevPage && nextPage != currPage)
        {
            auto last = peek(keylet::page(directory, nextPage));
            if (!last)
                LogicError ("Directory chain: fwd link broken.");

            if (last->getFieldV256 (sfIndexes).empty())
            {
                // Update the first page's linked list and
                // mark it as updated.
                node->setFieldU64 (sfIndexNext, currPage);
                node->setFieldU64 (sfIndexPrevious, currPage);
                update(node);

                // And erase the empty last page:
                erase(last);

                // Make sure our local values reflect the
                // updated information:
                nextPage = currPage;
                prevPage = currPage;
            }
        }

        if (keepRoot)
            return true;

        // If there's no other pages, erase the root:
        if (nextPage == currPage && prevPage == currPage)
            erase(node);

        return true;
    }

    // This can never happen for nodes other than the root:
    if (nextPage == currPage)
        LogicError ("Directory chain: fwd link broken");

    if (prevPage == currPage)
        LogicError ("Directory chain: rev link broken");

    // This node isn't the root, so it can either be in the
    // middle of the list, or at the end. Unlink it first
    // and then check if that leaves the list with only a
    // root:
    auto prev = peek(keylet::page(directory, prevPage));
    if (!prev)
        LogicError ("Directory chain: fwd link broken.");
    // Fix previous to point to its new next.
    prev->setFieldU64(sfIndexNext, nextPage);
    update (prev);

    auto next = peek(keylet::page(directory, nextPage));
    if (!next)
        LogicError ("Directory chain: rev link broken.");
    // Fix next to point to its new previous.
    next->setFieldU64(sfIndexPrevious, prevPage);
    update(next);

    // The page is no longer linked. Delete it.
    erase(node);

    // Check whether the next page is the last page and, if
    // so, whether it's empty. If it is, delete it.
    if (nextPage != rootPage &&
        next->getFieldU64 (sfIndexNext) == rootPage &&
        next->getFieldV256 (sfIndexes).empty())
    {
        // Since next doesn't point to the root, it
        // can't be pointing to prev.
        erase(next);

        // The previous page is now the last page:
        prev->setFieldU64(sfIndexNext, rootPage);
        update (prev);

        // And the root points to the the last page:
        auto root = peek(keylet::page(directory, rootPage));
        if (!root)
            LogicError ("Directory chain: root link broken.");
        root->setFieldU64(sfIndexPrevious, prevPage);
        update (root);

        nextPage = rootPage;
    }

    // If we're not keeping the root, then check to see if
    // it's left empty. If so, delete it as well.
    if (!keepRoot && nextPage == rootPage && prevPage == rootPage)
    {
        if (prev->getFieldV256 (sfIndexes).empty())
            erase(prev);
    }

    return true;
}

bool
ApplyView::dirRemove (
    Keylet const& directory,
    std::uint64_t currPage,
    Keylet const& key,
    bool keepRoot)
{
    return dirRemove (directory, currPage, key.key, keepRoot);
}

} // ripple
