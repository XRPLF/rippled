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

void
ApplyView::creditHook (
    AccountID const& from,
    AccountID const& to,
    STAmount const& amount,
    STAmount const& preCreditBalance)
{
}

// Called when the owner count changes
// This is required to support PaymentSandbox
void
ApplyView::adjustOwnerCountHook (
    AccountID const& account,
    std::uint32_t cur,
    std::uint32_t next)
{

}

std::pair<bool, std::uint64_t>
ApplyView::dirInsert (
    Keylet const& directory,
    uint256 const& key,
    bool strictOrder,
    std::function<void (std::shared_ptr<SLE> const&)> describe)
{
    assert(keylet::page(directory, 0).key == directory.key);

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

        return { true, 0 };
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
        // If we're not maintaining strict order, then
        // sort entries lexicographically.
        if (!strictOrder)
        {
            auto v = indexes.value();

            // We can't use std::lower_bound here because
            // existing pages may not be sorted
            auto pos = std::find_if(v.begin(), v.end(),
                [&key](uint256 const& item)
                {
                    return key < item;
                });

            if (pos != v.end() && key == *pos)
                LogicError ("dirInsert: double insertion");

            v.insert (pos, key);
            indexes = v;
        }
        else
            indexes.push_back(key);

        node->setFieldV256 (sfIndexes, indexes);
        update(node);
        return { true, page };
    }

    // Check whether we're out of pages.
    if (++page == 0)
        return { false, 0 };

    // Insert the new key:
    indexes.clear();
    indexes.push_back (key);

    // We are about to create a new node; we'll link it to
    // the chain first:

    node->setFieldU64 (sfIndexNext, page);
    update(node);

    root->setFieldU64 (sfIndexPrevious, page);
    update(root);

    node = std::make_shared<SLE>(keylet::page(directory, page));
    node->setFieldH256 (sfRootIndex, directory.key);
    node->setFieldV256 (sfIndexes, indexes);

    // Save some space by not specifying the value 0 since
    // it's the default.
    if (page != 1)
        node->setFieldU64 (sfIndexPrevious, page - 1);
    describe (node);
    insert (node);

    return { true, page };
}

std::pair<bool, std::uint64_t>
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
    SLE::pointer node = peek(keylet::page(directory, currPage));

    if (!node)
        return false;

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
    if (currPage == 0)
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

    // If we're not keeping the root, then check to see if
    // it's left empty. If so, delete it as well.
    if (!keepRoot && nextPage == 0 && prevPage == 0)
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
