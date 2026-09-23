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
#include <exception>
#include <mutex>
#include <sstream>
#include <stack>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace xrpl {

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
    boost::intrusive_ptr<SHAMapItem const> const& otherMapItem,
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
            auto inner = safeDowncast<SHAMapInnerNode*>(node);
            for (auto i = 0u; i < SHAMapInnerNode::kBranchFactor; ++i)
            {
                if (!inner->isEmptyBranch(i))
                    nodeStack.push({descendThrow(inner, i)});
            }
        }
        else
        {
            // This is a leaf node, process its item
            auto item = safeDowncast<SHAMapLeafNode*>(node)->peekItem();

            if (emptyBranch || (item->key() != otherMapItem->key()))
            {
                // unmatched
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
                // non-matching items with same tag
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
                // exact match
                emptyBranch = true;
            }
        }
    }

    if (!emptyBranch)
    {
        // otherMapItem was unmatched, must add
        if (isFirstMap)
        {  // this is first map, so other item is from second
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
    // compare two hash trees, add up to maxCount differences to the difference
    // table return value: true=complete table of differences given, false=too
    // many differences throws on corrupt tables or missing nodes CAUTION:
    // otherMap is not locked and must be immutable

    XRPL_ASSERT(
        isValid() && otherMap.isValid(), "xrpl::SHAMap::compare : valid state and valid input");

    if (getHash() == otherMap.getHash())
        return true;

    using StackEntry = std::pair<SHAMapTreeNode*, SHAMapTreeNode*>;
    std::stack<StackEntry, std::vector<StackEntry>> nodeStack;  // track nodes we've pushed

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
            // two leaves
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
            for (auto i = 0u; i < SHAMapInnerNode::kBranchFactor; ++i)
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

        for (auto i = 0u; i < SHAMapInnerNode::kBranchFactor; ++i)
        {
            if (!node->isEmptyBranch(i))
            {
                SHAMapTreeNodePtr const nextNode = descendNoStore(*node, i);

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
        return true;

    // Only the nodes this call records count towards the result, so remember what the
    // caller already had.
    auto const initialMissing = missingNodes.size();

    using StackEntry = intr_ptr::SharedPtr<SHAMapInnerNode>;
    std::array<SHAMapTreeNodePtr, SHAMapInnerNode::kBranchFactor> topChildren;
    {
        // This loop runs before the workers start, so it needs no lock.
        auto const& innerRoot = intr_ptr::staticPointerCast<SHAMapInnerNode>(root_);
        for (auto i = 0u; i < SHAMapInnerNode::kBranchFactor; ++i)
        {
            if (innerRoot->isEmptyBranch(i))
                continue;

            topChildren[i] = descendNoStore(*innerRoot, i);
            if (!topChildren[i])
            {
                // A root child that cannot be read hides its whole subtree. Record it here,
                // because the loop below skips a null child without visiting it.
                missingNodes.emplace_back(type_, innerRoot->getChildHash(i));
                if (--maxMissing <= 0)
                    return false;
            }
        }
    }
    std::vector<std::thread> workers;
    workers.reserve(SHAMapInnerNode::kBranchFactor);
    std::vector<std::string> exceptions;
    exceptions.reserve(SHAMapInnerNode::kBranchFactor);

    std::array<std::stack<StackEntry, std::vector<StackEntry>>, SHAMapInnerNode::kBranchFactor>
        nodeStacks;

    // This mutex is used inside the worker threads to protect `missingNodes`
    // and `maxMissing` from race conditions
    std::mutex m;

    for (auto rootChildIndex = 0u; rootChildIndex < SHAMapInnerNode::kBranchFactor;
         ++rootChildIndex)
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

                        for (auto i = 0u; i < SHAMapInnerNode::kBranchFactor; ++i)
                        {
                            if (node->isEmptyBranch(i))
                                continue;
                            SHAMapTreeNodePtr const nextNode = descendNoStore(*node, i);

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

                                // Another worker may have spent the budget while this one
                                // was walking, so test it before adding to the list and not
                                // only after. Testing only after lets every worker still
                                // running add one more node than the caller asked for.
                                if (maxMissing <= 0)
                                    return;

                                missingNodes.emplace_back(type_, node->getChildHash(i));
                                if (--maxMissing <= 0)
                                    return;
                            }
                        }
                    }
                }
                catch (std::exception const& e)
                {
                    // LCOV_EXCL_START
                    // A worker must not let an exception leave its thread, so record it
                    // and let the join below report it.
                    std::scoped_lock const l(m);
                    exceptions.emplace_back(e.what());
                    // LCOV_EXCL_STOP
                }
            },
            std::move(nodeStacks[rootChildIndex]));
    }

    for (std::thread& worker : workers)
        worker.join();

    std::scoped_lock const l(m);
    if (!exceptions.empty())
    {
        // LCOV_EXCL_START
        std::stringstream ss;
        ss << "Exception(s) in ledger load: ";
        for (auto const& e : exceptions)
            ss << e << ", ";
        JLOG(journal_.error()) << ss.str();
        return false;
        // LCOV_EXCL_STOP
    }

    // A node the workers could not read is recorded in `missingNodes` rather than thrown,
    // so the result has to consult it too.
    auto const found = missingNodes.size() - initialMissing;
    if (found != 0)
    {
        JLOG(journal_.error()) << "Missing node(s) in ledger load: " << found
                               << ", first: " << missingNodes[initialMissing].what();
        return false;
    }

    return true;
}

}  // namespace xrpl
