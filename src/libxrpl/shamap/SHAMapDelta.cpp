#include <xrpl/basics/IntrusivePointer.h>    // IWYU pragma: keep
#include <xrpl/basics/IntrusivePointer.ipp>  // IWYU pragma: keep
#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapInnerNode.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapMissingNode.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <boost/smart_ptr/intrusive_ptr.hpp>

#include <array>
#include <mutex>
#include <sstream>
#include <stack>
#include <thread>
#include <utility>
#include <vector>

namespace xrpl {

/** @file
 *  Implements SHAMap comparison (delta) and completeness-check (walkMap)
 *  operations. These are logically separate from the core map mechanics
 *  (insert, fetch, hash) and answer two questions: how do two trees differ,
 *  and what nodes are missing from this tree?
 *
 *  The delta algorithm short-circuits at matching subtree hashes, giving
 *  O(d) complexity in the number of differences rather than O(n) in total
 *  items. Missing-node detection uses iterative DFS (sequential) or
 *  depth-1-partitioned parallel traversal.
 */

/** Walk a subtree that is paired with an empty branch or single item from
 *  the other map, collecting all differences into `differences`.
 *
 *  This is the asymmetric-case helper for `compare`. One side of the
 *  traversal has a full subtree rooted at `node`; the other side has either
 *  nothing (`otherMapItem == nullptr`) or a single leaf item.
 *
 *  For each leaf found in the subtree three outcomes are possible:
 *  - No counterpart (`emptyBranch` is true or keys differ): the leaf is
 *      recorded as unmatched (one half of the `DeltaItem` pair is null).
 *  - Same key, different payload: recorded as a modification; `emptyBranch`
 *      is set to suppress a trailing entry for `otherMapItem`.
 *  - Exact match (same key and payload): silently consumed; `emptyBranch`
 *      is set.
 *
 *  After the walk, if `otherMapItem` was never matched it is inserted as its
 *  own unmatched entry.
 *
 *  `isFirstMap` determines which half of the `DeltaItem` pair receives `this`
 *  map's item versus the other map's item, so the semantic ordering
 *  (first-map version, second-map version) is preserved regardless of which
 *  direction the asymmetry runs.
 *
 *  @param node          Root of the subtree to walk. Must not be null.
 *  @param otherMapItem  The single item from the opposite side, or null if
 *      that side has no branch here.
 *  @param isFirstMap    `true` if `this` map is the first operand of the
 *      enclosing `compare` call; controls `DeltaItem` pair ordering.
 *  @param differences   Accumulator; entries are appended.
 *  @param maxCount      Shared budget counter; decremented per insertion.
 *      Passed by reference so the budget is shared across all delegations
 *      from `compare`.
 *  @return `true` if the walk completed within budget; `false` if `maxCount`
 *      was exhausted (diff is truncated).
 *  @throws SHAMapMissingNode if a referenced node cannot be fetched.
 */
bool
SHAMap::walkBranch(
    SHAMapTreeNode* node,
    boost::intrusive_ptr<SHAMapItem const> const& otherMapItem,
    bool isFirstMap,
    Delta& differences,
    int& maxCount) const
{
    std::stack<SHAMapTreeNode*, std::vector<SHAMapTreeNode*>> nodeStack;
    nodeStack.push(node);

    bool emptyBranch = !otherMapItem;

    while (!nodeStack.empty())
    {
        node = nodeStack.top();
        nodeStack.pop();

        if (node->isInner())
        {
            auto inner = safeDowncast<SHAMapInnerNode*>(node);
            for (int i = 0; i < 16; ++i)
            {
                if (!inner->isEmptyBranch(i))
                    nodeStack.push({descendThrow(inner, i)});
            }
        }
        else
        {
            auto item = safeDowncast<SHAMapLeafNode*>(node)->peekItem();

            if (emptyBranch || (item->key() != otherMapItem->key()))
            {
                if (isFirstMap)
                {
                    differences.insert(std::make_pair(item->key(), DeltaRef(item, nullptr)));
                }
                else
                {
                    differences.insert(std::make_pair(item->key(), DeltaRef(nullptr, item)));
                }

                if (--maxCount <= 0)
                    return false;
            }
            else if (item->slice() != otherMapItem->slice())
            {
                if (isFirstMap)
                {
                    differences.insert(std::make_pair(item->key(), DeltaRef(item, otherMapItem)));
                }
                else
                {
                    differences.insert(std::make_pair(item->key(), DeltaRef(otherMapItem, item)));
                }

                if (--maxCount <= 0)
                    return false;

                emptyBranch = true;
            }
            else
            {
                emptyBranch = true;
            }
        }
    }

    if (!emptyBranch)
    {
        // otherMapItem's key did not appear anywhere in the subtree — record it as unmatched
        if (isFirstMap)
        {
            differences.insert(
                std::make_pair(otherMapItem->key(), DeltaRef(nullptr, otherMapItem)));
        }
        else
        {
            differences.insert(
                std::make_pair(otherMapItem->key(), DeltaRef(otherMapItem, nullptr)));
        }

        if (--maxCount <= 0)
            return false;
    }

    return true;
}

bool
SHAMap::compare(SHAMap const& otherMap, Delta& differences, int maxCount) const
{
    XRPL_ASSERT(
        isValid() && otherMap.isValid(), "xrpl::SHAMap::compare : valid state and valid input");

    if (getHash() == otherMap.getHash())
        return true;

    using StackEntry = std::pair<SHAMapTreeNode*, SHAMapTreeNode*>;
    std::stack<StackEntry, std::vector<StackEntry>> nodeStack;

    nodeStack.emplace(root_.get(), otherMap.root_.get());
    while (!nodeStack.empty())
    {
        auto [ourNode, otherNode] = nodeStack.top();
        nodeStack.pop();

        if ((ourNode == nullptr) || (otherNode == nullptr))
        {
            // LCOV_EXCL_START
            UNREACHABLE("xrpl::SHAMap::compare : missing a node");
            Throw<SHAMapMissingNode>(type_, uint256());
            // LCOV_EXCL_STOP
        }

        if (ourNode->isLeaf() && otherNode->isLeaf())
        {
            auto ours = safeDowncast<SHAMapLeafNode*>(ourNode);
            auto other = safeDowncast<SHAMapLeafNode*>(otherNode);
            if (ours->peekItem()->key() == other->peekItem()->key())
            {
                if (ours->peekItem()->slice() != other->peekItem()->slice())
                {
                    differences.insert(
                        std::make_pair(
                            ours->peekItem()->key(),
                            DeltaRef(ours->peekItem(), other->peekItem())));
                    if (--maxCount <= 0)
                        return false;
                }
            }
            else
            {
                differences.insert(
                    std::make_pair(ours->peekItem()->key(), DeltaRef(ours->peekItem(), nullptr)));
                if (--maxCount <= 0)
                    return false;

                differences.insert(
                    std::make_pair(other->peekItem()->key(), DeltaRef(nullptr, other->peekItem())));
                if (--maxCount <= 0)
                    return false;
            }
        }
        else if (ourNode->isInner() && otherNode->isLeaf())
        {
            auto ours = safeDowncast<SHAMapInnerNode*>(ourNode);
            auto other = safeDowncast<SHAMapLeafNode*>(otherNode);
            if (!walkBranch(ours, other->peekItem(), true, differences, maxCount))
                return false;
        }
        else if (ourNode->isLeaf() && otherNode->isInner())
        {
            auto ours = safeDowncast<SHAMapLeafNode*>(ourNode);
            auto other = safeDowncast<SHAMapInnerNode*>(otherNode);
            if (!otherMap.walkBranch(other, ours->peekItem(), false, differences, maxCount))
                return false;
        }
        else if (ourNode->isInner() && otherNode->isInner())
        {
            auto ours = safeDowncast<SHAMapInnerNode*>(ourNode);
            auto other = safeDowncast<SHAMapInnerNode*>(otherNode);
            for (int i = 0; i < 16; ++i)
            {
                if (ours->getChildHash(i) != other->getChildHash(i))
                {
                    if (other->isEmptyBranch(i))
                    {
                        // We have a branch, the other tree does not
                        SHAMapTreeNode* iNode = descendThrow(ours, i);
                        if (!walkBranch(iNode, nullptr, true, differences, maxCount))
                            return false;
                    }
                    else if (ours->isEmptyBranch(i))
                    {
                        // The other tree has a branch, we do not
                        SHAMapTreeNode* iNode = otherMap.descendThrow(other, i);
                        if (!otherMap.walkBranch(iNode, nullptr, false, differences, maxCount))
                            return false;
                    }
                    else
                    {  // The two trees have different non-empty branches
                        nodeStack.emplace(descendThrow(ours, i), otherMap.descendThrow(other, i));
                    }
                }
            }
        }
        else
        {
            // LCOV_EXCL_START
            UNREACHABLE("xrpl::SHAMap::compare : invalid node");
            // LCOV_EXCL_STOP
        }
    }

    return true;
}

void
SHAMap::walkMap(std::vector<SHAMapMissingNode>& missingNodes, int maxMissing) const
{
    if (!root_->isInner())  // root_ is only node, and we have it
        return;

    using StackEntry = intr_ptr::SharedPtr<SHAMapInnerNode>;
    std::stack<StackEntry, std::vector<StackEntry>> nodeStack;

    nodeStack.push(intr_ptr::staticPointerCast<SHAMapInnerNode>(root_));

    while (!nodeStack.empty())
    {
        intr_ptr::SharedPtr<SHAMapInnerNode> const node = std::move(nodeStack.top());
        nodeStack.pop();

        for (int i = 0; i < 16; ++i)
        {
            if (!node->isEmptyBranch(i))
            {
                intr_ptr::SharedPtr<SHAMapTreeNode> const nextNode = descendNoStore(*node, i);

                if (nextNode)
                {
                    if (nextNode->isInner())
                        nodeStack.push(intr_ptr::staticPointerCast<SHAMapInnerNode>(nextNode));
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

bool
SHAMap::walkMapParallel(std::vector<SHAMapMissingNode>& missingNodes, int maxMissing) const
{
    if (!root_->isInner())  // root_ is only node, and we have it
        return false;

    using StackEntry = intr_ptr::SharedPtr<SHAMapInnerNode>;
    std::array<intr_ptr::SharedPtr<SHAMapTreeNode>, 16> topChildren;
    {
        auto const& innerRoot = intr_ptr::staticPointerCast<SHAMapInnerNode>(root_);
        for (int i = 0; i < 16; ++i)
        {
            if (!innerRoot->isEmptyBranch(i))
                topChildren[i] = descendNoStore(*innerRoot, i);
        }
    }
    std::vector<std::thread> workers;
    workers.reserve(16);
    std::vector<SHAMapMissingNode> exceptions;
    exceptions.reserve(16);

    std::array<std::stack<StackEntry, std::vector<StackEntry>>, 16> nodeStacks;

    // This mutex is used inside the worker threads to protect `missingNodes`
    // and `maxMissing` from race conditions
    std::mutex m;

    for (int rootChildIndex = 0; rootChildIndex < 16; ++rootChildIndex)
    {
        auto const& child = topChildren[rootChildIndex];
        if (!child || !child->isInner())
            continue;

        nodeStacks[rootChildIndex].push(intr_ptr::staticPointerCast<SHAMapInnerNode>(child));

        JLOG(journal_.debug()) << "starting worker " << rootChildIndex;
        workers.emplace_back(
            [&m, &missingNodes, &maxMissing, &exceptions, this](
                std::stack<StackEntry, std::vector<StackEntry>> nodeStack) {
                try
                {
                    while (!nodeStack.empty())
                    {
                        intr_ptr::SharedPtr<SHAMapInnerNode> const node =
                            std::move(nodeStack.top());
                        XRPL_ASSERT(node, "xrpl::SHAMap::walkMapParallel : non-null node");
                        nodeStack.pop();

                        for (int i = 0; i < 16; ++i)
                        {
                            if (node->isEmptyBranch(i))
                                continue;
                            intr_ptr::SharedPtr<SHAMapTreeNode> const nextNode =
                                descendNoStore(*node, i);

                            if (nextNode)
                            {
                                if (nextNode->isInner())
                                {
                                    nodeStack.push(
                                        intr_ptr::staticPointerCast<SHAMapInnerNode>(nextNode));
                                }
                            }
                            else
                            {
                                std::scoped_lock const l{m};
                                missingNodes.emplace_back(type_, node->getChildHash(i));
                                if (--maxMissing <= 0)
                                    return;
                            }
                        }
                    }
                }
                catch (SHAMapMissingNode const& e)
                {
                    std::scoped_lock const l(m);
                    exceptions.push_back(e);
                }
            },
            std::move(nodeStacks[rootChildIndex]));
    }

    for (std::thread& worker : workers)
        worker.join();

    std::scoped_lock const l(m);
    if (exceptions.empty())
        return true;
    std::stringstream ss;
    ss << "Exception(s) in ledger load: ";
    for (auto const& e : exceptions)
        ss << e.what() << ", ";
    JLOG(journal_.error()) << ss.str();
    return false;
}

}  // namespace xrpl
