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

#include <ripple/basics/contract.h>
#include <ripple/shamap/SHAMap.h>

#include <array>
#include <stack>
#include <vector>

namespace ripple {

// This code is used to compare another node's transaction tree
// to our own. It returns a map containing all items that are different
// between two SHA maps. It is optimized not to descend down tree
// branches with the same branch hash. A limit can be passed so
// that we will abort early if a node sends a map to us that
// makes no sense at all. (And our sync algorithm will avoid
// synchronizing matching branches too.)

bool
SHAMap::walkBranch(
    SHAMapTreeNode* node,
    std::shared_ptr<SHAMapItem const> const& otherMapItem,
    bool isFirstMap,
    Delta& differences,
    int& maxCount) const
{
    // Walk a branch of a SHAMap that's matched by an empty branch or single
    // item in the other map
    std::stack<SHAMapTreeNode*, std::vector<SHAMapTreeNode*>> nodeStack;
    nodeStack.push(node);

    bool emptyBranch = !otherMapItem;

    while (!nodeStack.empty())
    {
        node = nodeStack.top();
        nodeStack.pop();

        if (node->isInner())
        {
            // This is an inner node, add all non-empty branches
            auto inner = static_cast<SHAMapInnerNode*>(node);
            for (int i = 0; i < 16; ++i)
                if (!inner->isEmptyBranch(i))
                    nodeStack.push({descendThrow(inner, i)});
        }
        else
        {
            // This is a leaf node, process its item
            auto item = static_cast<SHAMapLeafNode*>(node)->peekItem();

            if (emptyBranch || (item->key() != otherMapItem->key()))
            {
                // unmatched
                if (isFirstMap)
                    differences.insert(std::make_pair(
                        item->key(),
                        DeltaRef(item, std::shared_ptr<SHAMapItem const>())));
                else
                    differences.insert(std::make_pair(
                        item->key(),
                        DeltaRef(std::shared_ptr<SHAMapItem const>(), item)));

                if (--maxCount <= 0)
                    return false;
            }
            else if (item->slice() != otherMapItem->slice())
            {
                // non-matching items with same tag
                if (isFirstMap)
                    differences.insert(std::make_pair(
                        item->key(), DeltaRef(item, otherMapItem)));
                else
                    differences.insert(std::make_pair(
                        item->key(), DeltaRef(otherMapItem, item)));

                if (--maxCount <= 0)
                    return false;

                emptyBranch = true;
            }
            else
            {
                // exact match
                emptyBranch = true;
            }
        }
    }

    if (!emptyBranch)
    {
        // otherMapItem was unmatched, must add
        if (isFirstMap)  // this is first map, so other item is from second
            differences.insert(std::make_pair(
                otherMapItem->key(),
                DeltaRef(std::shared_ptr<SHAMapItem const>(), otherMapItem)));
        else
            differences.insert(std::make_pair(
                otherMapItem->key(),
                DeltaRef(otherMapItem, std::shared_ptr<SHAMapItem const>())));

        if (--maxCount <= 0)
            return false;
    }

    return true;
}

bool
SHAMap::compare(SHAMap const& otherMap, Delta& differences, int maxCount) const
{
    // compare two hash trees, add up to maxCount differences to the difference
    // table return value: true=complete table of differences given, false=too
    // many differences throws on corrupt tables or missing nodes CAUTION:
    // otherMap is not locked and must be immutable

    assert(isValid() && otherMap.isValid());

    if (getHash() == otherMap.getHash())
        return true;

    using StackEntry = std::pair<SHAMapTreeNode*, SHAMapTreeNode*>;
    std::stack<StackEntry, std::vector<StackEntry>>
        nodeStack;  // track nodes we've pushed

    nodeStack.push({root_.get(), otherMap.root_.get()});
    while (!nodeStack.empty())
    {
        auto [ourNode, otherNode] = nodeStack.top();
        nodeStack.pop();

        if (!ourNode || !otherNode)
        {
            assert(false);
            Throw<SHAMapMissingNode>(type_, uint256());
        }

        if (ourNode->isLeaf() && otherNode->isLeaf())
        {
            // two leaves
            auto ours = static_cast<SHAMapLeafNode*>(ourNode);
            auto other = static_cast<SHAMapLeafNode*>(otherNode);
            if (ours->peekItem()->key() == other->peekItem()->key())
            {
                if (ours->peekItem()->slice() != other->peekItem()->slice())
                {
                    differences.insert(std::make_pair(
                        ours->peekItem()->key(),
                        DeltaRef(ours->peekItem(), other->peekItem())));
                    if (--maxCount <= 0)
                        return false;
                }
            }
            else
            {
                differences.insert(std::make_pair(
                    ours->peekItem()->key(),
                    DeltaRef(
                        ours->peekItem(),
                        std::shared_ptr<SHAMapItem const>())));
                if (--maxCount <= 0)
                    return false;

                differences.insert(std::make_pair(
                    other->peekItem()->key(),
                    DeltaRef(
                        std::shared_ptr<SHAMapItem const>(),
                        other->peekItem())));
                if (--maxCount <= 0)
                    return false;
            }
        }
        else if (ourNode->isInner() && otherNode->isLeaf())
        {
            auto ours = static_cast<SHAMapInnerNode*>(ourNode);
            auto other = static_cast<SHAMapLeafNode*>(otherNode);
            if (!walkBranch(
                    ours, other->peekItem(), true, differences, maxCount))
                return false;
        }
        else if (ourNode->isLeaf() && otherNode->isInner())
        {
            auto ours = static_cast<SHAMapLeafNode*>(ourNode);
            auto other = static_cast<SHAMapInnerNode*>(otherNode);
            if (!otherMap.walkBranch(
                    other, ours->peekItem(), false, differences, maxCount))
                return false;
        }
        else if (ourNode->isInner() && otherNode->isInner())
        {
            auto ours = static_cast<SHAMapInnerNode*>(ourNode);
            auto other = static_cast<SHAMapInnerNode*>(otherNode);
            for (int i = 0; i < 16; ++i)
                if (ours->getChildHash(i) != other->getChildHash(i))
                {
                    if (other->isEmptyBranch(i))
                    {
                        // We have a branch, the other tree does not
                        SHAMapTreeNode* iNode = descendThrow(ours, i);
                        if (!walkBranch(
                                iNode,
                                std::shared_ptr<SHAMapItem const>(),
                                true,
                                differences,
                                maxCount))
                            return false;
                    }
                    else if (ours->isEmptyBranch(i))
                    {
                        // The other tree has a branch, we do not
                        SHAMapTreeNode* iNode = otherMap.descendThrow(other, i);
                        if (!otherMap.walkBranch(
                                iNode,
                                std::shared_ptr<SHAMapItem const>(),
                                false,
                                differences,
                                maxCount))
                            return false;
                    }
                    else  // The two trees have different non-empty branches
                        nodeStack.push(
                            {descendThrow(ours, i),
                             otherMap.descendThrow(other, i)});
                }
        }
        else
            assert(false);
    }

    return true;
}

void
SHAMap::walkMap(std::vector<SHAMapMissingNode>& missingNodes, int maxMissing)
    const
{
    if (!root_->isInner())  // root_ is only node, and we have it
        return;

    using StackEntry = std::shared_ptr<SHAMapInnerNode>;
    std::stack<StackEntry, std::vector<StackEntry>> nodeStack;

    nodeStack.push(std::static_pointer_cast<SHAMapInnerNode>(root_));

    while (!nodeStack.empty())
    {
        std::shared_ptr<SHAMapInnerNode> node = std::move(nodeStack.top());
        nodeStack.pop();

        for (int i = 0; i < 16; ++i)
        {
            if (!node->isEmptyBranch(i))
            {
                std::shared_ptr<SHAMapTreeNode> nextNode =
                    descendNoStore(node, i);

                if (nextNode)
                {
                    if (nextNode->isInner())
                        nodeStack.push(
                            std::static_pointer_cast<SHAMapInnerNode>(
                                nextNode));
                }
                else
                {
                    missingNodes.emplace_back(type_, node->getChildHash(i));
                    if (--maxMissing <= 0)
                        return;
                }
            }
        }
    }
}

void
SHAMap::walkMapParallel(
    std::vector<SHAMapMissingNode>& missingNodes,
    int maxMissing) const
{
    if (!root_->isInner())  // root_ is only node, and we have it
        return;

    using StackEntry = std::shared_ptr<SHAMapInnerNode>;
    std::array<std::shared_ptr<SHAMapTreeNode>, 16> topChildren;
    {
        auto const& innerRoot =
            std::static_pointer_cast<SHAMapInnerNode>(root_);
        for (int i = 0; i < 16; ++i)
        {
            if (!innerRoot->isEmptyBranch(i))
                topChildren[i] = descendNoStore(innerRoot, i);
        }
    }
    std::vector<std::thread> workers;
    workers.reserve(16);

    std::array<std::stack<StackEntry, std::vector<StackEntry>>, 16> nodeStacks;

    // This mutex is used inside the worker threads to protect `missingNodes`
    // and `maxMissing` from race conditions
    std::mutex m;

    for (int rootChildIndex = 0; rootChildIndex < 16; ++rootChildIndex)
    {
        auto const& child = topChildren[rootChildIndex];
        if (!child || !child->isInner())
            continue;

        nodeStacks[rootChildIndex].push(
            std::static_pointer_cast<SHAMapInnerNode>(child));

        JLOG(journal_.debug()) << "starting worker " << rootChildIndex;
        workers.push_back(std::thread(
            [&m, &missingNodes, &maxMissing, this](
                std::stack<StackEntry, std::vector<StackEntry>> nodeStack) {
                while (!nodeStack.empty())
                {
                    std::shared_ptr<SHAMapInnerNode> node =
                        std::move(nodeStack.top());
                    assert(node);
                    nodeStack.pop();

                    for (int i = 0; i < 16; ++i)
                    {
                        if (node->isEmptyBranch(i))
                            continue;
                        std::shared_ptr<SHAMapTreeNode> nextNode =
                            descendNoStore(node, i);

                        if (nextNode)
                        {
                            if (nextNode->isInner())
                                nodeStack.push(
                                    std::static_pointer_cast<SHAMapInnerNode>(
                                        nextNode));
                        }
                        else
                        {
                            std::lock_guard l{m};
                            missingNodes.emplace_back(
                                type_, node->getChildHash(i));
                            if (--maxMissing <= 0)
                                return;
                        }
                    }
                }
            },
            std::move(nodeStacks[rootChildIndex])));
    }

    for (std::thread& worker : workers)
        worker.join();
}

}  // namespace ripple
