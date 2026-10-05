#include <xrpl/basics/Blob.h>
#include <xrpl/basics/IntrusivePointer.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/random.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapAddNode.h>
#include <xrpl/shamap/SHAMapInnerNode.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapLeafNode.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/shamap/SHAMapSyncFilter.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <boost/smart_ptr/intrusive_ptr.hpp>

#include <cstdint>
#include <exception>
#include <functional>
#include <iterator>
#include <mutex>
#include <optional>
#include <stack>
#include <tuple>
#include <utility>
#include <vector>

namespace xrpl {

namespace {

/**
 * Whether a depth is at or past the deepest an inner node may occupy.
 *
 * Nibbles run out at SHAMap::kLeafDepth, so only a leaf may sit there. True for
 * every deeper position too, which lies past the end of a key.
 *
 * @param depth The depth to judge.
 * @return Whether an inner node at that depth makes the map impossible.
 */
[[nodiscard]] bool
isLeafDepth(unsigned int depth)
{
    return depth >= SHAMap::kLeafDepth;
}

}  // namespace

void
SHAMap::visitLeaves(
    std::function<void(boost::intrusive_ptr<SHAMapItem const> const& item)> const& leafFunction)
    const
{
    visitNodes([&leafFunction](SHAMapTreeNode& node) {
        if (!node.isInner())
            leafFunction(safeDowncast<SHAMapLeafNode&>(node).peekItem());
        return true;
    });
}

void
SHAMap::visitNodes(std::function<bool(SHAMapTreeNode&)> const& function) const
{
    if (!root_)
        return;

    function(*root_);

    if (!root_->isInner())
        return;

    using StackEntry = std::pair<unsigned int, intr_ptr::SharedPtr<SHAMapInnerNode>>;
    std::stack<StackEntry, std::vector<StackEntry>> stack;

    auto node = intr_ptr::staticPointerCast<SHAMapInnerNode>(root_);
    auto pos = 0u;

    while (true)
    {
        while (pos < kBranchFactor)
        {
            if (!node->isEmptyBranch(pos))
            {
                SHAMapTreeNodePtr const child = descendNoStore(*node, pos);
                if (!function(*child))
                    return;

                if (child->isLeaf())
                {
                    ++pos;
                }
                else
                {
                    // If there are no more children, don't push this node
                    while ((pos != kBranchFactor - 1u) && (node->isEmptyBranch(pos + 1)))
                        ++pos;

                    if (pos != kBranchFactor - 1u)
                    {
                        // save next position to resume at
                        stack.emplace(pos + 1, std::move(node));
                    }

                    // descend to the child's first position
                    node = intr_ptr::staticPointerCast<SHAMapInnerNode>(child);
                    pos = 0;
                }
            }
            else
            {
                ++pos;  // move to next position
            }
        }

        if (stack.empty())
            break;

        std::tie(pos, node) = stack.top();
        stack.pop();
    }
}

void
SHAMap::visitDifferences(
    SHAMap const* map,
    std::function<bool(SHAMapTreeNode const&)> const& function) const
{
    // Visit every node in this SHAMap that is not present
    // in the specified SHAMap
    if (!root_)
        return;

    if (root_->getHash().isZero())
        return;

    if ((map != nullptr) && (root_->getHash() == map->root_->getHash()))
        return;

    if (root_->isLeaf())
    {
        auto leaf = intr_ptr::staticPointerCast<SHAMapLeafNode>(root_);
        if ((map == nullptr) || !map->hasLeafNode(leaf->peekItem()->key(), leaf->getHash()))
            function(*root_);
        return;
    }
    // contains unexplored non-matching inner node entries
    using StackEntry = std::pair<SHAMapInnerNode*, SHAMapNodeID>;
    std::stack<StackEntry, std::vector<StackEntry>> stack;

    stack.emplace(safeDowncast<SHAMapInnerNode*>(root_.get()), SHAMapNodeID{});

    while (!stack.empty())
    {
        auto const [node, nodeID] = stack.top();
        stack.pop();

        // 1) Add this node to the pack
        if (!function(*node))
            return;

        // Nibbles run out at kLeafDepth, so only a leaf belongs there. addKnownNode marks the map
        // invalid on meeting an inner node at that depth, and fetch-pack data is hash-verified
        // against a validated root, so reaching this means a defect or a corrupt store. The
        // node is still reported, since the wire form carries no depth and the recipient hooks
        // blobs in by hash. Its children are skipped, since getChildNodeID has no answer past
        // kLeafDepth.
        if (nodeID.getDepth() >= kLeafDepth)
        {
            // LCOV_EXCL_START
            UNREACHABLE("xrpl::SHAMap::visitDifferences : inner node at leaf depth");
            continue;
            // LCOV_EXCL_STOP
        }

        // 2) push non-matching child inner nodes
        for (auto i = 0u; i < kBranchFactor; ++i)
        {
            if (!node->isEmptyBranch(i))
            {
                auto const& childHash = node->getChildHash(i);
                auto const childID = nodeID.getChildNodeID(i);
                auto next = descendThrow(node, i);

                if (next->isInner())
                {
                    if ((map == nullptr) || !map->hasInnerNode(childID, childHash))
                        stack.emplace(safeDowncast<SHAMapInnerNode*>(next), childID);
                }
                else if ((map == nullptr) || !map->hasLeafNode(leafKey(*next), childHash))
                {
                    if (!function(*next))
                        return;
                }
            }
        }
    }
}

// Starting at the position referred to by the specfied
// StackEntry, process that node and its first resident
// children, descending the SHAMap until we complete the
// processing of a node.
void
SHAMap::gmnProcessNodes(MissingNodes& mn, MissingNodes::StackEntry& se)
{
    SHAMapInnerNode*& node = std::get<0>(se);
    SHAMapNodeID& nodeID = std::get<1>(se);
    auto& firstChild = std::get<2>(se);
    auto& currentChild = std::get<3>(se);
    bool& fullBelow = std::get<4>(se);

    while (currentChild < kBranchFactor)
    {
        auto const branch = (firstChild + currentChild++) % kBranchFactor;
        if (node->isEmptyBranch(branch))
            continue;

        auto const& childHash = node->getChildHash(branch);

        if (mn.missingHashes.contains(childHash))
        {
            // we already know this child node is missing
            fullBelow = false;
        }
        // The cache key carries the child's position beside its hash, so a hit answers for this
        // child at this position and no other. The depth test runs first, so a child at kLeafDepth
        // is judged by the arm below rather than answered from the cache: only a leaf sits there.
        else if (
            !backed_ || isLeafDepth(nodeID.getDepth() + 1) ||
            !f_.getFullBelowCache()->touchIfExists(childHash.asUInt256(), nodeID, branch))
        {
            bool pending = false;
            auto d = descendAsync(
                node,
                branch,
                mn.filter,
                pending,
                [node, nodeID, branch, &mn](SHAMapTreeNodePtr found, SHAMapHash const&) {
                    // a read completed asynchronously
                    std::unique_lock<std::mutex> const lock{mn.deferLock};
                    mn.finishedReads.emplace_back(node, nodeID, branch, std::move(found));
                    mn.deferCondVar.notify_one();
                });

            if (pending)
            {
                fullBelow = false;
                ++mn.deferred;
            }
            else if (d == nullptr)
            {
                // node is not in database

                fullBelow = false;  // for now, not known full below
                mn.missingHashes.insert(childHash);
                mn.missingNodes.emplace_back(nodeID.getChildNodeID(branch), childHash.asUInt256());

                if (--mn.max <= 0)
                    return;
            }
            else if (d->isInner() && isLeafDepth(nodeID.getDepth() + 1))
            {
                // Only a leaf belongs that deep (see isLeafDepth and SHAMap::addKnownNode). A node
                // resolved locally reaches the walk without passing through addKnownNode(), so the
                // walk reaches this verdict itself. Ordered ahead of the full-below test below,
                // which canonicalization shares across maps.
                JLOG(journal_.warn()) << "Inner node at branch " << branch << " below " << nodeID
                                      << " makes the map invalid";
                setInvalid();
                return;
            }
            // The node's own full-below flag is not read here. The node object is shared by hash
            // across maps and positions, so a flag another walk set says nothing about this
            // position, and the position-keyed cache above has already missed for it.
            else if (d->isInner())
            {
                mn.stack.push(se);

                // Switch to processing the child node
                node = safeDowncast<SHAMapInnerNode*>(d);
                nodeID = nodeID.getChildNodeID(branch);
                firstChild = randInt(255);
                currentChild = 0;
                fullBelow = true;
            }
        }
    }

    // We have finished processing an inner node
    // and thus (for now) all its children

    if (fullBelow)
    {  // No partial node encountered below this node
        // The node flag is written for the root alone, since the root position is the only one a
        // shared node object can vouch for; see SHAMapInnerNode::fullBelowGen_.
        if (nodeID.isRoot())
            node->setFullBelowGen(mn.generation);
        if (backed_)
        {
            // Keyed by position as well as hash, so the entry answers only for this position.
            f_.getFullBelowCache()->insert(node->getHash().asUInt256(), nodeID);
        }
    }

    node = nullptr;
}

// Wait for deferred reads to finish and
// process their results
void
SHAMap::gmnProcessDeferredReads(MissingNodes& mn)
{
    // Process all deferred reads
    int complete = 0;
    while (complete != mn.deferred)
    {
        MissingNodes::DeferredNode deferredNode;
        {
            std::unique_lock<std::mutex> lock{mn.deferLock};

            while (mn.finishedReads.size() <= complete)
                mn.deferCondVar.wait(lock);
            deferredNode = std::move(mn.finishedReads[complete++]);
        }

        auto parent = std::get<0>(deferredNode);
        auto const& parentID = std::get<1>(deferredNode);
        auto branch = std::get<2>(deferredNode);
        auto nodePtr = std::get<3>(deferredNode);
        auto const& nodeHash = parent->getChildHash(branch);

        if (nodePtr)
        {  // Got the node
            nodePtr = parent->canonicalizeChild(branch, std::move(nodePtr));

            // When we finish this stack, we need to restart
            // with the parent of this node
            mn.resumes[parent] = parentID;
        }
        else if ((mn.max > 0) && (mn.missingHashes.insert(nodeHash).second))
        {
            // getChildNodeID() is safe here: gmnProcessNodes refuses to descend into an inner node
            // at kLeafDepth, so a deferred parent sits at most one level above it.
            mn.missingNodes.emplace_back(parentID.getChildNodeID(branch), nodeHash.asUInt256());
            --mn.max;
        }
    }

    mn.finishedReads.clear();
    mn.finishedReads.reserve(mn.maxDefer);
    mn.deferred = 0;
}

std::vector<std::pair<SHAMapNodeID, UInt256>>
SHAMap::getMissingNodes(int max, SHAMapSyncFilter const* filter)
{
    XRPL_ASSERT(root_->getHash().isNonZero(), "xrpl::SHAMap::getMissingNodes : nonzero root hash");
    XRPL_ASSERT(max > 0, "xrpl::SHAMap::getMissingNodes : valid max input");

    if (!isValid())
    {
        // The root node's own hash, since getHash() unshares the tree on a zero hash.
        JLOG(journal_.warn()) << "getMissingNodes called on an invalid map, root hash "
                              << root_->getHash() << " seq " << ledgerSeq();
        return {};
    }

    MissingNodes mn(
        max,
        filter,
        512,  // number of async reads per pass
        f_.getFullBelowCache()->getGeneration());

    if (!root_->isInner() ||
        intr_ptr::staticPointerCast<SHAMapInnerNode>(root_)->isFullBelow(mn.generation))
    {
        clearSynching();
        return std::move(mn.missingNodes);
    }

    // Start at the root.
    // The firstChild value is selected randomly so if multiple threads
    // are traversing the map, each thread will start at a different
    // (randomly selected) inner node.  This increases the likelihood
    // that the two threads will produce different request sets (which is
    // more efficient than sending identical requests).
    MissingNodes::StackEntry pos{
        safeDowncast<SHAMapInnerNode*>(root_.get()), SHAMapNodeID(), randInt(255), 0, true};
    auto& node = std::get<0>(pos);
    auto& nextChild = std::get<3>(pos);
    auto& fullBelow = std::get<4>(pos);

    // Traverse the map without blocking
    do
    {
        while ((node != nullptr) && (mn.deferred <= mn.maxDefer))
        {
            gmnProcessNodes(mn, pos);

            // The walk just invalidated the map. The loop stops descending here but falls through
            // to the drain below, since every posted read must be drained while `mn` is alive.
            if (!isValid())
                break;

            if (mn.max <= 0)
                break;

            if ((node == nullptr) && !mn.stack.empty())
            {
                // Pick up where we left off with this node's parent
                bool const was = fullBelow;  // was full below

                pos = mn.stack.top();
                mn.stack.pop();
                if (nextChild == 0)
                {
                    // This is a node we are processing for the first time
                    fullBelow = true;
                }
                else
                {
                    // This is a node we are continuing to process
                    fullBelow = fullBelow && was;  // was and still is
                }
                XRPL_ASSERT(node, "xrpl::SHAMap::getMissingNodes : first non-null node");
            }
        }

        // We have either emptied the stack or
        // posted as many deferred reads as we can
        if (mn.deferred != 0)
            gmnProcessDeferredReads(mn);

        // Reads are drained, so the map can be abandoned. What was collected belongs to a tree
        // that cannot exist.
        if (!isValid())
            return {};

        if (mn.max <= 0)
            return std::move(mn.missingNodes);

        if (node == nullptr)
        {  // We weren't in the middle of processing a node

            if (mn.stack.empty() && !mn.resumes.empty())
            {
                // Recheck nodes we could not finish before. A node's own full-below flag is not
                // consulted: it is written for a root only, and a resumed node sits below one.
                for (auto const& [innerNode, nodeId] : mn.resumes)
                    mn.stack.emplace(innerNode, nodeId, randInt(255), 0, true);

                mn.resumes.clear();
            }

            if (!mn.stack.empty())
            {
                // Resume at the top of the stack
                pos = mn.stack.top();
                mn.stack.pop();
                XRPL_ASSERT(node, "xrpl::SHAMap::getMissingNodes : second non-null node");
            }
        }

        // node will only still be nullptr if
        // we finished the current node, the stack is empty
        // and we have no nodes to resume

    } while (node != nullptr);

    // addKnownNode() on another thread can write the verdict after the loop's own test, so the map
    // is judged once more before the result is returned.
    if (!isValid())
        return {};  // LCOV_EXCL_LINE: only that other thread reaches this, so no test does

    if (mn.missingNodes.empty())
        clearSynching();

    return std::move(mn.missingNodes);
}

bool
SHAMap::getNodeFat(
    SHAMapNodeID const& wanted,
    std::vector<SHAMapNodeData>& data,
    bool fatLeaves,
    std::uint32_t depth) const
{
    // Gets a node and some of its children
    // to a specified depth

    auto node = root_.get();
    SHAMapNodeID nodeID;

    while ((node != nullptr) && node->isInner() && (nodeID.getDepth() < wanted.getDepth()))
    {
        auto const branch = selectBranch(nodeID, wanted.getNodeID());
        auto inner = safeDowncast<SHAMapInnerNode*>(node);
        if (inner->isEmptyBranch(branch))
            return false;
        node = descendThrow(inner, branch);
        nodeID = nodeID.getChildNodeID(branch);
    }

    if (node == nullptr || wanted != nodeID)
    {
        JLOG(journal_.info()) << "peer requested node that is not in the map: " << wanted
                              << " but found " << nodeID;
        return false;
    }

    if (node->isInner() && safeDowncast<SHAMapInnerNode*>(node)->isEmpty())
    {
        JLOG(journal_.warn()) << "peer requests empty node";
        return false;
    }

    std::stack<std::tuple<SHAMapTreeNode*, SHAMapNodeID, std::uint32_t>> stack;
    stack.emplace(node, nodeID, depth);

    Serializer s(8192);

    while (!stack.empty())
    {
        std::tie(node, nodeID, depth) = stack.top();
        stack.pop();

        // Add this node to the reply
        s.erase();
        node->serializeForWire(s);
        data.emplace_back(nodeID, node->isLeaf(), s.getData());

        if (node->isInner())
        {
            // We descend inner nodes with only a single child
            // without decrementing the depth
            auto inner = safeDowncast<SHAMapInnerNode*>(node);
            auto const bc = inner->getBranchCount();

            if ((depth > 0) || (bc == 1))
            {
                // We need to process this node's children
                for (auto i = 0u; i < kBranchFactor; ++i)
                {
                    if (!inner->isEmptyBranch(i))
                    {
                        auto const childNode = descendThrow(inner, i);
                        auto const childID = nodeID.getChildNodeID(i);

                        if (childNode->isInner() && ((depth > 1) || (bc == 1)))
                        {
                            // If there's more than one child, reduce the depth
                            // If only one child, follow the chain
                            stack.emplace(childNode, childID, (bc > 1) ? (depth - 1) : depth);
                        }
                        else if (childNode->isInner() || fatLeaves)
                        {
                            // Just include this node
                            s.erase();
                            childNode->serializeForWire(s);
                            data.emplace_back(childID, childNode->isLeaf(), s.getData());
                        }
                    }
                }
            }
        }
    }

    return true;
}

void
SHAMap::serializeRoot(Serializer& s) const
{
    root_->serializeForWire(s);
}

SHAMapAddNode
SHAMap::addRootNode(
    SHAMapHash const& hash,
    SHAMapTreeNodePtr rootNode,
    SHAMapSyncFilter const* filter)
{
    XRPL_ASSERT(cowid_ >= 1, "xrpl::SHAMap::addRootNode : valid cowid");
    XRPL_ASSERT(rootNode, "xrpl::SHAMap::addRootNode : non-null root node");

    // A map syncs against one hash and installs a root once, so a root already held is a duplicate
    // only once it hashes to the hash asked for.
    if (root_->getHash().isNonZero())
    {
        JLOG(journal_.trace()) << "Got root node, already have one";

        if (root_->getHash() != hash)
        {
            JLOG(journal_.warn()) << "Root node offered under hash " << hash
                                  << ", but the map holds " << root_->getHash();
            return SHAMapAddNode::invalid();
        }

        return SHAMapAddNode::duplicate();
    }

    if (rootNode->getHash() != hash)
    {
        JLOG(journal_.warn()) << "Corrupt root node received: expected hash " << hash << ", got "
                              << rootNode->getHash();
        return SHAMapAddNode::invalid();
    }

    if (backed_)
        canonicalize(hash, rootNode);

    root_ = std::move(rootNode);

    if (root_->isLeaf())
        clearSynching();

    if (filter != nullptr)
    {
        Serializer s;
        root_->serializeWithPrefix(s);
        filter->gotNode(
            false, root_->getHash(), ledgerSeq(), std::move(s.modData()), root_->getType());
    }

    return SHAMapAddNode::useful();
}

SHAMapAddNode
SHAMap::addKnownNode(
    SHAMapNodeID const& nodeID,
    SHAMapTreeNodePtr treeNode,
    SHAMapSyncFilter const* filter)
{
    XRPL_ASSERT(!nodeID.isRoot(), "xrpl::SHAMap::addKnownNode : valid node");
    XRPL_ASSERT(treeNode, "xrpl::SHAMap::addKnownNode : non-null tree node");

    if (!isSynching())
    {
        JLOG(journal_.trace()) << "AddKnownNode while not synching";
        return SHAMapAddNode::duplicate();
    }

    SHAMapNodeID currNodeID;
    auto currNode = root_.get();

    // The descent reads no node's own full-below flag: a node object is shared by hash across
    // maps and positions, so that flag answers for the root position alone. The position-keyed
    // cache lookup in the loop is the memo that answers for a position below it.
    while (currNode->isInner() && (currNodeID.getDepth() < nodeID.getDepth()))
    {
        auto const branch = selectBranch(currNodeID, nodeID.getNodeID());
        auto inner = safeDowncast<SHAMapInnerNode*>(currNode);
        if (inner->isEmptyBranch(branch))
        {
            JLOG(journal_.warn()) << "Add known node " << nodeID << " for empty branch " << branch
                                  << " at " << currNodeID;
            return SHAMapAddNode::invalid();
        }

        auto childHash = inner->getChildHash(branch);

        // The cache key carries the child's position beside its hash, so a hit answers for this
        // child at this position and no other. The depth test runs first, so a node offered at
        // kLeafDepth reaches the badDepth test below rather than being answered as a duplicate.
        if (!isLeafDepth(currNodeID.getDepth() + 1) &&
            f_.getFullBelowCache()->touchIfExists(childHash.asUInt256(), currNodeID, branch))
        {
            return SHAMapAddNode::duplicate();
        }

        auto prevNode = inner;
        std::tie(currNode, currNodeID) = descend(inner, currNodeID, branch, filter);

        if (!isValid())
        {
            // descend condemned the map. `childHash` was read before that descent, so the
            // comparison below would read a stale value.
            JLOG(journal_.warn()) << "Node " << nodeID << " cannot be hooked into an invalid map";
            return SHAMapAddNode::invalid();
        }

        if (currNode != nullptr)
            continue;

        if (childHash != treeNode->getHash())
        {
            JLOG(journal_.warn()) << "Corrupt node " << nodeID << " received: expected hash "
                                  << childHash << ", got " << treeNode->getHash();
            return SHAMapAddNode::invalid();
        }

        // Every node from the root down hash-verified to get here, so the requested root hash
        // itself commits to a shape no tree can have. The verdict belongs to that hash.
        bool const badDepth = treeNode->isInner() && isLeafDepth(currNodeID.getDepth());
        SOMETIMES(badDepth, "xrpl::SHAMap::addKnownNode : map is invalid");
        if (badDepth)
        {
            condemn(*treeNode, treeNode->getHash(), currNodeID);
            return SHAMapAddNode::mapInvalidated();
        }

        // The data hashes to the child at currNodeID but is labeled nodeID, so it is not the node
        // asked for. Only the label is wrong, so the map stays sound.
        bool const badPosition = (currNodeID != nodeID);
        SOMETIMES(badPosition, "xrpl::SHAMap::addKnownNode : node ID does not match its position");
        if (badPosition)
        {
            JLOG(journal_.warn()) << "Unable to hook node " << nodeID << ", stuck at "
                                  << currNodeID;
            return SHAMapAddNode::invalid();
        }

        // A leaf's own key names its position, and the hash test above ties this leaf to this
        // parent, so the verdict belongs to the map. Below badPosition, since Invalid is terminal.
        if (!belongsAt(nodeID, *treeNode))
        {
            condemn(*treeNode, treeNode->getHash(), nodeID);
            return SHAMapAddNode::mapInvalidated();
        }

        if (backed_)
            canonicalize(childHash, treeNode);

        treeNode = prevNode->canonicalizeChild(branch, std::move(treeNode));

        if (filter != nullptr)
        {
            Serializer s;
            treeNode->serializeWithPrefix(s);
            filter->gotNode(
                false, childHash, ledgerSeq(), std::move(s.modData()), treeNode->getType());
        }

        return SHAMapAddNode::useful();
    }

    JLOG(journal_.trace()) << "got node, already had it (late)";
    return SHAMapAddNode::duplicate();
}

bool
SHAMap::deepCompare(SHAMap& other) const
{
    // Intended for debug/test only
    std::stack<std::pair<SHAMapTreeNode*, SHAMapTreeNode*>> stack;

    stack.emplace(root_.get(), other.root_.get());

    while (!stack.empty())
    {
        auto const [node, otherNode] = stack.top();
        stack.pop();

        if ((node == nullptr) || (otherNode == nullptr))
        {
            JLOG(journal_.info()) << "unable to fetch node";
            return false;
        }
        if (otherNode->getHash() != node->getHash())
        {
            JLOG(journal_.warn()) << "node hash mismatch";
            return false;
        }

        if (node->isLeaf())
        {
            if (!otherNode->isLeaf())
                return false;
            auto& nodePeek = safeDowncast<SHAMapLeafNode*>(node)->peekItem();
            auto& otherNodePeek = safeDowncast<SHAMapLeafNode*>(otherNode)->peekItem();
            if (nodePeek->key() != otherNodePeek->key())
                return false;
            if (nodePeek->slice() != otherNodePeek->slice())
                return false;
        }
        else if (node->isInner())
        {
            if (!otherNode->isInner())
                return false;
            auto nodeInner = safeDowncast<SHAMapInnerNode*>(node);
            auto otherInner = safeDowncast<SHAMapInnerNode*>(otherNode);
            for (auto i = 0u; i < kBranchFactor; ++i)
            {
                if (nodeInner->isEmptyBranch(i))
                {
                    if (!otherInner->isEmptyBranch(i))
                        return false;
                }
                else
                {
                    if (otherInner->isEmptyBranch(i))
                        return false;

                    auto next = descend(nodeInner, i);
                    auto otherNext = other.descend(otherInner, i);
                    if ((next == nullptr) || (otherNext == nullptr))
                    {
                        JLOG(journal_.warn()) << "unable to fetch inner node";
                        return false;
                    }
                    stack.emplace(next, otherNext);
                }
            }
        }
    }

    return true;
}

/**
 * Does this map have this inner node?
 */
bool
SHAMap::hasInnerNode(SHAMapNodeID const& targetNodeID, SHAMapHash const& targetNodeHash) const
{
    auto node = root_.get();
    SHAMapNodeID nodeID;

    while (node->isInner() && (nodeID.getDepth() < targetNodeID.getDepth()))
    {
        auto const branch = selectBranch(nodeID, targetNodeID.getNodeID());
        auto inner = safeDowncast<SHAMapInnerNode*>(node);
        if (inner->isEmptyBranch(branch))
            return false;

        node = descendThrow(inner, branch);
        nodeID = nodeID.getChildNodeID(branch);
    }

    return (node->isInner()) && (node->getHash() == targetNodeHash);
}

/**
 * Does this map have this leaf node?
 */
bool
SHAMap::hasLeafNode(UInt256 const& tag, SHAMapHash const& targetNodeHash) const
{
    auto node = root_.get();
    SHAMapNodeID nodeID;

    if (!node->isInner())  // only one leaf node in the tree
        return node->getHash() == targetNodeHash;

    do
    {
        // Same kLeafDepth hazard as in visitDifferences above. That guard bounds the caller's own
        // traversal, not the map queried here, and the loop below descends from this map's root
        // independently, so this check is what keeps a malformed map from reaching getChildNodeID.
        if (isLeafDepth(nodeID.getDepth()))
        {
            // LCOV_EXCL_START
            UNREACHABLE("xrpl::SHAMap::hasLeafNode : inner node at leaf depth");
            return false;
            // LCOV_EXCL_STOP
        }

        auto const branch = selectBranch(nodeID, tag);
        auto inner = safeDowncast<SHAMapInnerNode*>(node);
        if (inner->isEmptyBranch(branch))
            return false;  // Dead end, node must not be here

        if (inner->getChildHash(branch) == targetNodeHash)  // Matching leaf, no need to retrieve it
            return true;

        node = descendThrow(inner, branch);
        nodeID = nodeID.getChildNodeID(branch);
    } while (node->isInner());

    return false;  // If this was a matching leaf, we would have caught it
                   // already
}

std::optional<std::vector<Blob>>
SHAMap::getProofPath(UInt256 const& key) const
{
    NodePathStack stack;
    walkTowardsKey(key, &stack);

    if (stack.empty())
    {
        JLOG(journal_.debug()) << "no path to " << key;
        return {};
    }

    if (auto const& node = stack.top().first; !node || node->isInner() ||
        intr_ptr::staticPointerCast<SHAMapLeafNode>(node)->peekItem()->key() != key)
    {
        JLOG(journal_.debug()) << "no path to " << key;
        return {};
    }

    std::vector<Blob> path;
    path.reserve(stack.size());
    while (!stack.empty())
    {
        Serializer s;
        stack.top().first->serializeForWire(s);
        path.emplace_back(std::move(s.modData()));
        stack.pop();
    }

    JLOG(journal_.debug()) << "getPath for key " << key << ", path length " << path.size();
    return path;
}

bool
SHAMap::verifyProofPath(UInt256 const& rootHash, UInt256 const& key, std::vector<Blob> const& path)
{
    if (path.empty() || path.size() > kLeafDepth + 1u)
        return false;

    SHAMapHash hash{rootHash};
    try
    {
        for (auto rit = path.rbegin(); rit != path.rend(); ++rit)
        {
            auto const& blob = *rit;
            auto node = SHAMapTreeNode::makeFromWire(makeSlice(blob));
            if (!node)
                return false;
            node->updateHash();
            if (node->getHash() != hash)
                return false;

            auto const depth = static_cast<unsigned int>(std::distance(path.rbegin(), rit));
            if (node->isInner())
            {
                // Nibbles run out at kLeafDepth, so only the leaf terminating the path may sit
                // there. These nodes come off the wire, so a peer can still claim an inner one;
                // reject it rather than passing this depth to selectBranch.
                SOMETIMES(
                    depth >= kLeafDepth, "xrpl::SHAMap::verifyProofPath : inner at leaf depth");
                if (depth >= kLeafDepth)
                    return false;

                auto nodeId = SHAMapNodeID::createID(depth, key);
                hash = safeDowncast<SHAMapInnerNode*>(node.get())
                           ->getChildHash(selectBranch(nodeId, key));
            }
            else
            {
                // The hash chain up to rootHash proves this leaf sits where the path claims. Any
                // leaf whose subtree hashes the same at every level above satisfies that chain, so
                // the terminal leaf's own key is what ties the proof to `key`.
                if (leafKey(*node) != key)
                    return false;

                // should exhaust all the blobs now
                return depth + 1 == path.size();
            }
        }
    }
    catch (std::exception const&)
    {
        // the data in the path may come from the network,
        // exception could be thrown when parsing the data
        return false;
    }
    return false;
}

}  // namespace xrpl
