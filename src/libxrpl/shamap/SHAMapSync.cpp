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

    using StackEntry = std::pair<int, intr_ptr::SharedPtr<SHAMapInnerNode>>;
    std::stack<StackEntry, std::vector<StackEntry>> stack;

    auto node = intr_ptr::staticPointerCast<SHAMapInnerNode>(root_);
    int pos = 0;

    while (true)
    {
        while (pos < 16)
        {
            if (!node->isEmptyBranch(pos))
            {
                intr_ptr::SharedPtr<SHAMapTreeNode> const child = descendNoStore(*node, pos);
                if (!function(*child))
                    return;

                if (child->isLeaf())
                {
                    ++pos;
                }
                else
                {
                    // If there are no more children, don't push this node
                    while ((pos != 15) && (node->isEmptyBranch(pos + 1)))
                        ++pos;

                    if (pos != 15)
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
    SHAMap const* have,
    std::function<bool(SHAMapTreeNode const&)> const& function) const
{
    // Visit every node in this SHAMap that is not present
    // in the specified SHAMap
    if (!root_)
        return;

    if (root_->getHash().isZero())
        return;

    if ((have != nullptr) && (root_->getHash() == have->root_->getHash()))
        return;

    if (root_->isLeaf())
    {
        auto leaf = intr_ptr::staticPointerCast<SHAMapLeafNode>(root_);
        if ((have == nullptr) || !have->hasLeafNode(leaf->peekItem()->key(), leaf->getHash()))
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

        // 2) push non-matching child inner nodes
        for (int i = 0; i < 16; ++i)
        {
            if (!node->isEmptyBranch(i))
            {
                auto const& childHash = node->getChildHash(i);
                SHAMapNodeID const childID = nodeID.getChildNodeID(i);
                auto next = descendThrow(node, i);

                if (next->isInner())
                {
                    if ((have == nullptr) || !have->hasInnerNode(childID, childHash))
                        stack.emplace(safeDowncast<SHAMapInnerNode*>(next), childID);
                }
                else if (
                    (have == nullptr) ||
                    !have->hasLeafNode(
                        safeDowncast<SHAMapLeafNode*>(next)->peekItem()->key(), childHash))
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
    int& firstChild = std::get<2>(se);
    int& currentChild = std::get<3>(se);
    bool& fullBelow = std::get<4>(se);

    while (currentChild < 16)
    {
        int const branch = (firstChild + currentChild++) % 16;
        if (node->isEmptyBranch(branch))
            continue;

        auto const& childHash = node->getChildHash(branch);

        if (mn.missingHashes.contains(childHash))
        {
            // we already know this child node is missing
            fullBelow = false;
        }
        else if (!backed_ || !f_.getFullBelowCache()->touchIfExists(childHash.asUInt256()))
        {
            bool pending = false;
            auto d = descendAsync(
                node,
                branch,
                mn.filter,
                pending,
                [node, nodeID, branch, &mn](
                    intr_ptr::SharedPtr<SHAMapTreeNode> found, SHAMapHash const&) {
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
            else if (d->isInner() && !safeDowncast<SHAMapInnerNode*>(d)->isFullBelow(mn.generation))
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
        node->setFullBelowGen(mn.generation);
        if (backed_)
        {
            f_.getFullBelowCache()->insert(node->getHash().asUInt256());
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
        std::tuple<SHAMapInnerNode*, SHAMapNodeID, int, intr_ptr::SharedPtr<SHAMapTreeNode>>
            deferredNode;
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
            mn.missingNodes.emplace_back(parentID.getChildNodeID(branch), nodeHash.asUInt256());
            --mn.max;
        }
    }

    mn.finishedReads.clear();
    mn.finishedReads.reserve(mn.maxDefer);
    mn.deferred = 0;
}

/** Get a list of node IDs and hashes for nodes that are part of this SHAMap
    but not available locally.  The filter can hold alternate sources of
    nodes that are not permanently stored locally
*/
std::vector<std::pair<SHAMapNodeID, uint256>>
SHAMap::getMissingNodes(int max, SHAMapSyncFilter* filter)
{
    XRPL_ASSERT(root_->getHash().isNonZero(), "xrpl::SHAMap::getMissingNodes : nonzero root hash");
    XRPL_ASSERT(max > 0, "xrpl::SHAMap::getMissingNodes : valid max input");

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

        if (mn.max <= 0)
            return std::move(mn.missingNodes);

        if (node == nullptr)
        {  // We weren't in the middle of processing a node

            if (mn.stack.empty() && !mn.resumes.empty())
            {
                // Recheck nodes we could not finish before
                for (auto const& [innerNode, nodeId] : mn.resumes)
                {
                    if (!innerNode->isFullBelow(mn.generation))
                        mn.stack.emplace(innerNode, nodeId, randInt(255), 0, true);
                }

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

    if (mn.missingNodes.empty())
        clearSynching();

    return std::move(mn.missingNodes);
}

bool
SHAMap::getNodeFat(
    SHAMapNodeID const& wanted,
    std::vector<std::pair<SHAMapNodeID, Blob>>& data,
    bool fatLeaves,
    std::uint32_t depth) const
{
    // Gets a node and some of its children
    // to a specified depth

    auto node = root_.get();
    SHAMapNodeID nodeID;

    while ((node != nullptr) && node->isInner() && (nodeID.getDepth() < wanted.getDepth()))
    {
        int const branch = selectBranch(nodeID, wanted.getNodeID());
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

    std::stack<std::tuple<SHAMapTreeNode*, SHAMapNodeID, int>> stack;
    stack.emplace(node, nodeID, depth);

    Serializer s(8192);

    while (!stack.empty())
    {
        std::tie(node, nodeID, depth) = stack.top();
        stack.pop();

        // Add this node to the reply
        s.erase();
        node->serializeForWire(s);
        data.emplace_back(nodeID, s.getData());

        if (node->isInner())
        {
            // We descend inner nodes with only a single child
            // without decrementing the depth
            auto inner = safeDowncast<SHAMapInnerNode*>(node);
            int const bc = inner->getBranchCount();

            if ((depth > 0) || (bc == 1))
            {
                // We need to process this node's children
                for (int i = 0; i < 16; ++i)
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
                            data.emplace_back(childID, s.getData());
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
SHAMap::addRootNode(SHAMapHash const& hash, Slice const& rootNode, SHAMapSyncFilter* filter)
{
    // we already have a root_ node
    if (root_->getHash().isNonZero())
    {
        JLOG(journal_.trace()) << "got root node, already have one";
        XRPL_ASSERT(root_->getHash() == hash, "xrpl::SHAMap::addRootNode : valid hash input");
        return SHAMapAddNode::duplicate();
    }

    XRPL_ASSERT(cowid_ >= 1, "xrpl::SHAMap::addRootNode : valid cowid");
    auto node = SHAMapTreeNode::makeFromWire(rootNode);
    if (!node || node->getHash() != hash)
        return SHAMapAddNode::invalid();

    if (backed_)
        canonicalize(hash, node);

    root_ = node;

    if (root_->isLeaf())
        clearSynching();

    if (filter != nullptr)
    {
        Serializer s;
        root_->serializeWithPrefix(s);
        filter->gotNode(
            false, root_->getHash(), ledgerSeq_, std::move(s.modData()), root_->getType());
    }

    return SHAMapAddNode::useful();
}

SHAMapAddNode
SHAMap::addKnownNode(SHAMapNodeID const& node, Slice const& rawNode, SHAMapSyncFilter* filter)
{
    XRPL_ASSERT(!node.isRoot(), "xrpl::SHAMap::addKnownNode : valid node input");

    if (!isSynching())
    {
        JLOG(journal_.trace()) << "AddKnownNode while not synching";
        return SHAMapAddNode::duplicate();
    }

    auto const generation = f_.getFullBelowCache()->getGeneration();
    SHAMapNodeID currNodeID;
    auto currNode = root_.get();

    while (currNode->isInner() &&
           !safeDowncast<SHAMapInnerNode*>(currNode)->isFullBelow(generation) &&
           (currNodeID.getDepth() < node.getDepth()))
    {
        int const branch = selectBranch(currNodeID, node.getNodeID());
        XRPL_ASSERT(branch >= 0, "xrpl::SHAMap::addKnownNode : valid branch");
        auto inner = safeDowncast<SHAMapInnerNode*>(currNode);
        if (inner->isEmptyBranch(branch))
        {
            JLOG(journal_.warn()) << "Add known node for empty branch" << node;
            return SHAMapAddNode::invalid();
        }

        auto childHash = inner->getChildHash(branch);
        if (f_.getFullBelowCache()->touchIfExists(childHash.asUInt256()))
        {
            return SHAMapAddNode::duplicate();
        }

        auto prevNode = inner;
        std::tie(currNode, currNodeID) = descend(inner, currNodeID, branch, filter);

        if (currNode != nullptr)
            continue;

        auto newNode = SHAMapTreeNode::makeFromWire(rawNode);

        if (!newNode || childHash != newNode->getHash())
        {
            JLOG(journal_.warn()) << "Corrupt node received";
            return SHAMapAddNode::invalid();
        }

        // In rare cases, a node can still be corrupt even after hash
        // validation. For leaf nodes, we perform an additional check to
        // ensure the node's position in the tree is consistent with its
        // content to prevent inconsistencies that could
        // propagate further down the line.
        if (newNode->isLeaf())
        {
            auto const& actualKey =
                safeDowncast<SHAMapLeafNode const*>(newNode.get())->peekItem()->key();

            // Validate that this leaf belongs at the target position
            auto const expectedNodeID = SHAMapNodeID::createID(node.getDepth(), actualKey);
            if (expectedNodeID.getNodeID() != node.getNodeID())
            {
                JLOG(journal_.debug())
                    << "Leaf node position mismatch: "
                    << "expected=" << expectedNodeID.getNodeID() << ", actual=" << node.getNodeID();
                return SHAMapAddNode::invalid();
            }
        }

        // Inner nodes must be at a level strictly less than 64
        // but leaf nodes (while notionally at level 64) can be
        // at any depth up to and including 64:
        if ((currNodeID.getDepth() > kLeafDepth) ||
            (newNode->isInner() && currNodeID.getDepth() == kLeafDepth))
        {
            // Map is provably invalid
            state_ = SHAMapState::Invalid;
            return SHAMapAddNode::useful();
        }

        if (currNodeID != node)
        {
            // Either this node is broken or we didn't request it (yet)
            JLOG(journal_.warn()) << "unable to hook node " << node;
            JLOG(journal_.info()) << " stuck at " << currNodeID;
            JLOG(journal_.info()) << "got depth=" << node.getDepth()
                                  << ", walked to= " << currNodeID.getDepth();
            return SHAMapAddNode::useful();
        }

        if (backed_)
            canonicalize(childHash, newNode);

        newNode = prevNode->canonicalizeChild(branch, std::move(newNode));

        if (filter != nullptr)
        {
            Serializer s;
            newNode->serializeWithPrefix(s);
            filter->gotNode(
                false, childHash, ledgerSeq_, std::move(s.modData()), newNode->getType());
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
            for (int i = 0; i < 16; ++i)
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

/** Does this map have this inner node?
 */
bool
SHAMap::hasInnerNode(SHAMapNodeID const& targetNodeID, SHAMapHash const& targetNodeHash) const
{
    auto node = root_.get();
    SHAMapNodeID nodeID;

    while (node->isInner() && (nodeID.getDepth() < targetNodeID.getDepth()))
    {
        int const branch = selectBranch(nodeID, targetNodeID.getNodeID());
        auto inner = safeDowncast<SHAMapInnerNode*>(node);
        if (inner->isEmptyBranch(branch))
            return false;

        node = descendThrow(inner, branch);
        nodeID = nodeID.getChildNodeID(branch);
    }

    return (node->isInner()) && (node->getHash() == targetNodeHash);
}

/** Does this map have this leaf node?
 */
bool
SHAMap::hasLeafNode(uint256 const& tag, SHAMapHash const& targetNodeHash) const
{
    auto node = root_.get();
    SHAMapNodeID nodeID;

    if (!node->isInner())  // only one leaf node in the tree
        return node->getHash() == targetNodeHash;

    do
    {
        int const branch = selectBranch(nodeID, tag);
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
SHAMap::getProofPath(uint256 const& key) const
{
    SharedPtrNodeStack stack;
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
SHAMap::verifyProofPath(uint256 const& rootHash, uint256 const& key, std::vector<Blob> const& path)
{
    if (path.empty() || path.size() > 65)
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

            auto depth = std::distance(path.rbegin(), rit);
            if (node->isInner())
            {
                auto nodeId = SHAMapNodeID::createID(depth, key);
                hash = safeDowncast<SHAMapInnerNode*>(node.get())
                           ->getChildHash(selectBranch(nodeId, key));
            }
            else
            {
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
