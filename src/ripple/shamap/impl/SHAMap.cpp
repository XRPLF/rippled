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
#include <ripple/shamap/SHAMapAccountStateLeafNode.h>
#include <ripple/shamap/SHAMapNodeID.h>
#include <ripple/shamap/SHAMapSyncFilter.h>
#include <ripple/shamap/SHAMapTxLeafNode.h>
#include <ripple/shamap/SHAMapTxPlusMetaLeafNode.h>

namespace ripple {

[[nodiscard]] std::shared_ptr<SHAMapLeafNode>
makeTypedLeaf(
    SHAMapNodeType type,
    boost::intrusive_ptr<SHAMapItem const> item,
    std::uint32_t owner)
{
    if (type == SHAMapNodeType::tnTRANSACTION_NM)
        return std::make_shared<SHAMapTxLeafNode>(std::move(item), owner);

    if (type == SHAMapNodeType::tnTRANSACTION_MD)
        return std::make_shared<SHAMapTxPlusMetaLeafNode>(
            std::move(item), owner);

    if (type == SHAMapNodeType::tnACCOUNT_STATE)
        return std::make_shared<SHAMapAccountStateLeafNode>(
            std::move(item), owner);

    LogicError(
        "Attempt to create leaf node of unknown type " +
        std::to_string(
            static_cast<std::underlying_type_t<SHAMapNodeType>>(type)));
}

SHAMap::SHAMap(SHAMapType t, Family& f)
    : f_(f), journal_(f.journal()), state_(SHAMapState::Modifying), type_(t)
{
    root_ = std::make_shared<SHAMapInnerNode>(cowid_);
}

// The `hash` parameter is unused. It is part of the interface so it's clear
// from the parameters that this is the constructor to use when the hash is
// known. The fact that the parameter is unused is an implementation detail that
// should not change the interface.
SHAMap::SHAMap(SHAMapType t, uint256 const& hash, Family& f)
    : f_(f), journal_(f.journal()), state_(SHAMapState::Synching), type_(t)
{
    root_ = std::make_shared<SHAMapInnerNode>(cowid_);
}

SHAMap::SHAMap(SHAMap const& other, bool isMutable)
    : f_(other.f_)
    , journal_(other.f_.journal())
    , cowid_(other.cowid_ + 1)
    , ledgerSeq_(other.ledgerSeq_)
    , root_(other.root_)
    , state_(isMutable ? SHAMapState::Modifying : SHAMapState::Immutable)
    , type_(other.type_)
    , backed_(other.backed_)
{
    // If either map may change, they cannot share nodes
    if ((state_ != SHAMapState::Immutable) ||
        (other.state_ != SHAMapState::Immutable))
    {
        unshare();
    }
}

std::shared_ptr<SHAMap>
SHAMap::snapShot(bool isMutable) const
{
    return std::make_shared<SHAMap>(*this, isMutable);
}

void
SHAMap::dirtyUp(
    SharedPtrNodeStack& stack,
    uint256 const& target,
    std::shared_ptr<SHAMapTreeNode> child)
{
    // walk the tree up from through the inner nodes to the root_
    // update hashes and links
    // stack is a path of inner nodes up to, but not including, child
    // child can be an inner node or a leaf

    assert(
        (state_ != SHAMapState::Synching) &&
        (state_ != SHAMapState::Immutable));
    assert(child && (child->cowid() == cowid_));

    while (!stack.empty())
    {
        auto node =
            std::dynamic_pointer_cast<SHAMapInnerNode>(stack.top().first);
        SHAMapNodeID nodeID = stack.top().second;
        stack.pop();
        assert(node != nullptr);

        int branch = selectBranch(nodeID, target);
        assert(branch >= 0);

        node = unshareNode(std::move(node), nodeID);
        node->setChild(branch, std::move(child));

        child = std::move(node);
    }
}

SHAMapLeafNode*
SHAMap::walkTowardsKey(uint256 const& id, SharedPtrNodeStack* stack) const
{
    assert(stack == nullptr || stack->empty());
    auto inNode = root_;
    SHAMapNodeID nodeID;

    while (inNode->isInner())
    {
        if (stack != nullptr)
            stack->push({inNode, nodeID});

        auto const inner = std::static_pointer_cast<SHAMapInnerNode>(inNode);
        auto const branch = selectBranch(nodeID, id);
        if (inner->isEmptyBranch(branch))
            return nullptr;

        inNode = descendThrow(inner, branch);
        nodeID = nodeID.getChildNodeID(branch);
    }

    if (stack != nullptr)
        stack->push({inNode, nodeID});
    return static_cast<SHAMapLeafNode*>(inNode.get());
}

SHAMapLeafNode*
SHAMap::findKey(uint256 const& id) const
{
    SHAMapLeafNode* leaf = walkTowardsKey(id);
    if (leaf && leaf->peekItem()->key() != id)
        leaf = nullptr;
    return leaf;
}

std::shared_ptr<SHAMapTreeNode>
SHAMap::fetchNodeFromDB(SHAMapHash const& hash) const
{
    assert(backed_);
    auto obj = f_.db().fetchNodeObject(hash.as_uint256(), ledgerSeq_);
    return finishFetch(hash, obj);
}

std::shared_ptr<SHAMapTreeNode>
SHAMap::finishFetch(
    SHAMapHash const& hash,
    std::shared_ptr<NodeObject> const& object) const
{
    assert(backed_);

    try
    {
        if (!object)
        {
            if (full_)
            {
                full_ = false;
                f_.missingNodeAcquireBySeq(ledgerSeq_, hash.as_uint256());
            }
            return {};
        }

        auto node =
            SHAMapTreeNode::makeFromPrefix(makeSlice(object->getData()), hash);
        if (node)
            canonicalize(hash, node);
        return node;
    }
    catch (std::runtime_error const& e)
    {
        JLOG(journal_.warn()) << "finishFetch exception: " << e.what();
    }
    catch (...)
    {
        JLOG(journal_.warn())
            << "finishFetch exception: unknonw exception: " << hash;
    }

    return {};
}

// See if a sync filter has a node
std::shared_ptr<SHAMapTreeNode>
SHAMap::checkFilter(SHAMapHash const& hash, SHAMapSyncFilter* filter) const
{
    if (auto nodeData = filter->getNode(hash))
    {
        try
        {
            auto node =
                SHAMapTreeNode::makeFromPrefix(makeSlice(*nodeData), hash);
            if (node)
            {
                filter->gotNode(
                    true,
                    hash,
                    ledgerSeq_,
                    std::move(*nodeData),
                    node->getType());
                if (backed_)
                    canonicalize(hash, node);
            }
            return node;
        }
        catch (std::exception const& x)
        {
            JLOG(f_.journal().warn())
                << "Invalid node/data, hash=" << hash << ": " << x.what();
        }
    }
    return {};
}

// Get a node without throwing
// Used on maps where missing nodes are expected
std::shared_ptr<SHAMapTreeNode>
SHAMap::fetchNodeNT(SHAMapHash const& hash, SHAMapSyncFilter* filter) const
{
    auto node = cacheLookup(hash);
    if (node)
        return node;

    if (backed_)
    {
        node = fetchNodeFromDB(hash);
        if (node)
        {
            canonicalize(hash, node);
            return node;
        }
    }

    if (filter)
        node = checkFilter(hash, filter);

    return node;
}

std::shared_ptr<SHAMapTreeNode>
SHAMap::fetchNodeNT(SHAMapHash const& hash) const
{
    auto node = cacheLookup(hash);

    if (!node && backed_)
        node = fetchNodeFromDB(hash);

    return node;
}

// Throw if the node is missing
std::shared_ptr<SHAMapTreeNode>
SHAMap::fetchNode(SHAMapHash const& hash) const
{
    auto node = fetchNodeNT(hash);

    if (!node)
        Throw<SHAMapMissingNode>(type_, hash);

    return node;
}

SHAMapTreeNode*
SHAMap::descendThrow(SHAMapInnerNode* parent, int branch) const
{
    SHAMapTreeNode* ret = descend(parent, branch);

    if (!ret && !parent->isEmptyBranch(branch))
        Throw<SHAMapMissingNode>(type_, parent->getChildHash(branch));

    return ret;
}

std::shared_ptr<SHAMapTreeNode>
SHAMap::descendThrow(std::shared_ptr<SHAMapInnerNode> const& parent, int branch)
    const
{
    std::shared_ptr<SHAMapTreeNode> ret = descend(parent, branch);

    if (!ret && !parent->isEmptyBranch(branch))
        Throw<SHAMapMissingNode>(type_, parent->getChildHash(branch));

    return ret;
}

SHAMapTreeNode*
SHAMap::descend(SHAMapInnerNode* parent, int branch) const
{
    SHAMapTreeNode* ret = parent->getChildPointer(branch);
    if (ret || !backed_)
        return ret;

    std::shared_ptr<SHAMapTreeNode> node =
        fetchNodeNT(parent->getChildHash(branch));
    if (!node)
        return nullptr;

    node = parent->canonicalizeChild(branch, std::move(node));
    return node.get();
}

std::shared_ptr<SHAMapTreeNode>
SHAMap::descend(std::shared_ptr<SHAMapInnerNode> const& parent, int branch)
    const
{
    std::shared_ptr<SHAMapTreeNode> node = parent->getChild(branch);
    if (node || !backed_)
        return node;

    node = fetchNode(parent->getChildHash(branch));
    if (!node)
        return nullptr;

    node = parent->canonicalizeChild(branch, std::move(node));
    return node;
}

// Gets the node that would be hooked to this branch,
// but doesn't hook it up.
std::shared_ptr<SHAMapTreeNode>
SHAMap::descendNoStore(
    std::shared_ptr<SHAMapInnerNode> const& parent,
    int branch) const
{
    std::shared_ptr<SHAMapTreeNode> ret = parent->getChild(branch);
    if (!ret && backed_)
        ret = fetchNode(parent->getChildHash(branch));
    return ret;
}

std::pair<SHAMapTreeNode*, SHAMapNodeID>
SHAMap::descend(
    SHAMapInnerNode* parent,
    SHAMapNodeID const& parentID,
    int branch,
    SHAMapSyncFilter* filter) const
{
    assert(parent->isInner());
    assert((branch >= 0) && (branch < branchFactor));
    assert(!parent->isEmptyBranch(branch));

    SHAMapTreeNode* child = parent->getChildPointer(branch);

    if (!child)
    {
        auto const& childHash = parent->getChildHash(branch);
        std::shared_ptr<SHAMapTreeNode> childNode =
            fetchNodeNT(childHash, filter);

        if (childNode)
        {
            childNode = parent->canonicalizeChild(branch, std::move(childNode));
            child = childNode.get();
        }
    }

    return std::make_pair(child, parentID.getChildNodeID(branch));
}

SHAMapTreeNode*
SHAMap::descendAsync(
    SHAMapInnerNode* parent,
    int branch,
    SHAMapSyncFilter* filter,
    bool& pending,
    descendCallback&& callback) const
{
    pending = false;

    SHAMapTreeNode* ret = parent->getChildPointer(branch);
    if (ret)
        return ret;

    auto const& hash = parent->getChildHash(branch);

    auto ptr = cacheLookup(hash);
    if (!ptr)
    {
        if (filter)
            ptr = checkFilter(hash, filter);

        if (!ptr && backed_)
        {
            f_.db().asyncFetch(
                hash.as_uint256(),
                ledgerSeq_,
                [this, hash, cb{std::move(callback)}](
                    std::shared_ptr<NodeObject> const& object) {
                    auto node = finishFetch(hash, object);
                    cb(node, hash);
                });
            pending = true;
            return nullptr;
        }
    }

    if (ptr)
        ptr = parent->canonicalizeChild(branch, std::move(ptr));

    return ptr.get();
}

template <class Node>
std::shared_ptr<Node>
SHAMap::unshareNode(std::shared_ptr<Node> node, SHAMapNodeID const& nodeID)
{
    // make sure the node is suitable for the intended operation (copy on write)
    assert(node->cowid() <= cowid_);
    if (node->cowid() != cowid_)
    {
        // have a CoW
        assert(state_ != SHAMapState::Immutable);
        node = std::static_pointer_cast<Node>(node->clone(cowid_));
        if (nodeID.isRoot())
            root_ = node;
    }
    return node;
}

SHAMapLeafNode*
SHAMap::belowHelper(
    std::shared_ptr<SHAMapTreeNode> node,
    SharedPtrNodeStack& stack,
    int branch,
    std::tuple<int, std::function<bool(int)>, std::function<void(int&)>> const&
        loopParams) const
{
    auto& [init, cmp, incr] = loopParams;
    if (node->isLeaf())
    {
        auto n = std::static_pointer_cast<SHAMapLeafNode>(node);
        stack.push({node, {leafDepth, n->peekItem()->key()}});
        return n.get();
    }
    auto inner = std::static_pointer_cast<SHAMapInnerNode>(node);
    if (stack.empty())
        stack.push({inner, SHAMapNodeID{}});
    else
        stack.push({inner, stack.top().second.getChildNodeID(branch)});
    for (int i = init; cmp(i);)
    {
        if (!inner->isEmptyBranch(i))
        {
            node = descendThrow(inner, i);
            assert(!stack.empty());
            if (node->isLeaf())
            {
                auto n = std::static_pointer_cast<SHAMapLeafNode>(node);
                stack.push({n, {leafDepth, n->peekItem()->key()}});
                return n.get();
            }
            inner = std::static_pointer_cast<SHAMapInnerNode>(node);
            stack.push({inner, stack.top().second.getChildNodeID(branch)});
            i = init;  // descend and reset loop
        }
        else
            incr(i);  // scan next branch
    }
    return nullptr;
}
SHAMapLeafNode*
SHAMap::lastBelow(
    std::shared_ptr<SHAMapTreeNode> node,
    SharedPtrNodeStack& stack,
    int branch) const
{
    auto init = branchFactor - 1;
    auto cmp = [](int i) { return i >= 0; };
    auto incr = [](int& i) { --i; };

    return belowHelper(node, stack, branch, {init, cmp, incr});
}
SHAMapLeafNode*
SHAMap::firstBelow(
    std::shared_ptr<SHAMapTreeNode> node,
    SharedPtrNodeStack& stack,
    int branch) const
{
    auto init = 0;
    auto cmp = [](int i) { return i <= branchFactor; };
    auto incr = [](int& i) { ++i; };

    return belowHelper(node, stack, branch, {init, cmp, incr});
}
static const boost::intrusive_ptr<SHAMapItem const> no_item;

boost::intrusive_ptr<SHAMapItem const> const&
SHAMap::onlyBelow(SHAMapTreeNode* node) const
{
    // If there is only one item below this node, return it

    while (!node->isLeaf())
    {
        SHAMapTreeNode* nextNode = nullptr;
        auto inner = static_cast<SHAMapInnerNode*>(node);
        for (int i = 0; i < branchFactor; ++i)
        {
            if (!inner->isEmptyBranch(i))
            {
                if (nextNode)
                    return no_item;

                nextNode = descendThrow(inner, i);
            }
        }

        if (!nextNode)
        {
            assert(false);
            return no_item;
        }

        node = nextNode;
    }

    // An inner node must have at least one leaf
    // below it, unless it's the root_
    auto const leaf = static_cast<SHAMapLeafNode const*>(node);
    assert(leaf->peekItem() || (leaf == root_.get()));
    return leaf->peekItem();
}

SHAMapLeafNode const*
SHAMap::peekFirstItem(SharedPtrNodeStack& stack) const
{
    assert(stack.empty());
    SHAMapLeafNode* node = firstBelow(root_, stack);
    if (!node)
    {
        while (!stack.empty())
            stack.pop();
        return nullptr;
    }
    return node;
}

SHAMapLeafNode const*
SHAMap::peekNextItem(uint256 const& id, SharedPtrNodeStack& stack) const
{
    assert(!stack.empty());
    assert(stack.top().first->isLeaf());
    stack.pop();
    while (!stack.empty())
    {
        auto [node, nodeID] = stack.top();
        assert(!node->isLeaf());
        auto inner = std::static_pointer_cast<SHAMapInnerNode>(node);
        for (auto i = selectBranch(nodeID, id) + 1; i < branchFactor; ++i)
        {
            if (!inner->isEmptyBranch(i))
            {
                node = descendThrow(inner, i);
                auto leaf = firstBelow(node, stack, i);
                if (!leaf)
                    Throw<SHAMapMissingNode>(type_, id);
                assert(leaf->isLeaf());
                return leaf;
            }
        }
        stack.pop();
    }
    // must be last item
    return nullptr;
}

boost::intrusive_ptr<SHAMapItem const> const&
SHAMap::peekItem(uint256 const& id) const
{
    SHAMapLeafNode* leaf = findKey(id);

    if (!leaf)
        return no_item;

    return leaf->peekItem();
}

boost::intrusive_ptr<SHAMapItem const> const&
SHAMap::peekItem(uint256 const& id, SHAMapHash& hash) const
{
    SHAMapLeafNode* leaf = findKey(id);

    if (!leaf)
        return no_item;

    hash = leaf->getHash();
    return leaf->peekItem();
}

SHAMap::const_iterator
SHAMap::upper_bound(uint256 const& id) const
{
    SharedPtrNodeStack stack;
    walkTowardsKey(id, &stack);
    while (!stack.empty())
    {
        auto [node, nodeID] = stack.top();
        if (node->isLeaf())
        {
            auto leaf = static_cast<SHAMapLeafNode*>(node.get());
            if (leaf->peekItem()->key() > id)
                return const_iterator(
                    this, leaf->peekItem().get(), std::move(stack));
        }
        else
        {
            auto inner = std::static_pointer_cast<SHAMapInnerNode>(node);
            for (auto branch = selectBranch(nodeID, id) + 1;
                 branch < branchFactor;
                 ++branch)
            {
                if (!inner->isEmptyBranch(branch))
                {
                    node = descendThrow(inner, branch);
                    auto leaf = firstBelow(node, stack, branch);
                    if (!leaf)
                        Throw<SHAMapMissingNode>(type_, id);
                    return const_iterator(
                        this, leaf->peekItem().get(), std::move(stack));
                }
            }
        }
        stack.pop();
    }
    return end();
}
SHAMap::const_iterator
SHAMap::lower_bound(uint256 const& id) const
{
    SharedPtrNodeStack stack;
    walkTowardsKey(id, &stack);
    while (!stack.empty())
    {
        auto [node, nodeID] = stack.top();
        if (node->isLeaf())
        {
            auto leaf = static_cast<SHAMapLeafNode*>(node.get());
            if (leaf->peekItem()->key() < id)
                return const_iterator(
                    this, leaf->peekItem().get(), std::move(stack));
        }
        else
        {
            auto inner = std::static_pointer_cast<SHAMapInnerNode>(node);
            for (int branch = selectBranch(nodeID, id) - 1; branch >= 0;
                 --branch)
            {
                if (!inner->isEmptyBranch(branch))
                {
                    node = descendThrow(inner, branch);
                    auto leaf = lastBelow(node, stack, branch);
                    if (!leaf)
                        Throw<SHAMapMissingNode>(type_, id);
                    return const_iterator(
                        this, leaf->peekItem().get(), std::move(stack));
                }
            }
        }
        stack.pop();
    }
    // TODO: what to return here?
    return end();
}

bool
SHAMap::hasItem(uint256 const& id) const
{
    return (findKey(id) != nullptr);
}

bool
SHAMap::delItem(uint256 const& id)
{
    // delete the item with this ID
    assert(state_ != SHAMapState::Immutable);

    SharedPtrNodeStack stack;
    walkTowardsKey(id, &stack);

    if (stack.empty())
        Throw<SHAMapMissingNode>(type_, id);

    auto leaf = std::dynamic_pointer_cast<SHAMapLeafNode>(stack.top().first);
    stack.pop();

    if (!leaf || (leaf->peekItem()->key() != id))
        return false;

    SHAMapNodeType type = leaf->getType();

    // What gets attached to the end of the chain
    // (For now, nothing, since we deleted the leaf)
    std::shared_ptr<SHAMapTreeNode> prevNode;

    while (!stack.empty())
    {
        auto node =
            std::static_pointer_cast<SHAMapInnerNode>(stack.top().first);
        SHAMapNodeID nodeID = stack.top().second;
        stack.pop();

        node = unshareNode(std::move(node), nodeID);
        node->setChild(selectBranch(nodeID, id), std::move(prevNode));

        if (!nodeID.isRoot())
        {
            // we may have made this a node with 1 or 0 children
            // And, if so, we need to remove this branch
            const int bc = node->getBranchCount();
            if (bc == 0)
            {
                // no children below this branch
                prevNode.reset();
            }
            else if (bc == 1)
            {
                // If there's only one item, pull up on the thread
                auto item = onlyBelow(node.get());

                if (item)
                {
                    for (int i = 0; i < branchFactor; ++i)
                    {
                        if (!node->isEmptyBranch(i))
                        {
                            node->setChild(i, nullptr);
                            break;
                        }
                    }

                    prevNode = makeTypedLeaf(type, item, node->cowid());
                }
                else
                {
                    prevNode = std::move(node);
                }
            }
            else
            {
                // This node is now the end of the branch
                prevNode = std::move(node);
            }
        }
    }

    return true;
}

bool
SHAMap::addGiveItem(
    SHAMapNodeType type,
    boost::intrusive_ptr<SHAMapItem const> item)
{
    assert(state_ != SHAMapState::Immutable);
    assert(type != SHAMapNodeType::tnINNER);

    // add the specified item, does not update
    uint256 tag = item->key();

    SharedPtrNodeStack stack;
    walkTowardsKey(tag, &stack);

    if (stack.empty())
        Throw<SHAMapMissingNode>(type_, tag);

    auto [node, nodeID] = stack.top();
    stack.pop();

    if (node->isLeaf())
    {
        auto leaf = std::static_pointer_cast<SHAMapLeafNode>(node);
        if (leaf->peekItem()->key() == tag)
            return false;
    }
    node = unshareNode(std::move(node), nodeID);
    if (node->isInner())
    {
        // easy case, we end on an inner node
        auto inner = std::static_pointer_cast<SHAMapInnerNode>(node);
        int branch = selectBranch(nodeID, tag);
        assert(inner->isEmptyBranch(branch));
        inner->setChild(branch, makeTypedLeaf(type, std::move(item), cowid_));
    }
    else
    {
        // this is a leaf node that has to be made an inner node holding two
        // items
        auto leaf = std::static_pointer_cast<SHAMapLeafNode>(node);
        auto otherItem = leaf->peekItem();
        assert(otherItem && (tag != otherItem->key()));

        node = std::make_shared<SHAMapInnerNode>(node->cowid());

        unsigned int b1, b2;

        while ((b1 = selectBranch(nodeID, tag)) ==
               (b2 = selectBranch(nodeID, otherItem->key())))
        {
            stack.push({node, nodeID});

            // we need a new inner node, since both go on same branch at this
            // level
            nodeID = nodeID.getChildNodeID(b1);
            node = std::make_shared<SHAMapInnerNode>(cowid_);
        }

        // we can add the two leaf nodes here
        assert(node->isInner());

        auto inner = static_cast<SHAMapInnerNode*>(node.get());
        inner->setChild(b1, makeTypedLeaf(type, std::move(item), cowid_));
        inner->setChild(b2, makeTypedLeaf(type, std::move(otherItem), cowid_));
    }

    dirtyUp(stack, tag, node);
    return true;
}

bool
SHAMap::addItem(
    SHAMapNodeType type,
    boost::intrusive_ptr<SHAMapItem const> item)
{
    return addGiveItem(type, std::move(item));
}

SHAMapHash
SHAMap::getHash() const
{
    auto hash = root_->getHash();
    if (hash.isZero())
    {
        const_cast<SHAMap&>(*this).unshare();
        hash = root_->getHash();
    }
    return hash;
}

bool
SHAMap::updateGiveItem(
    SHAMapNodeType type,
    boost::intrusive_ptr<SHAMapItem const> item)
{
    // can't change the tag but can change the hash
    uint256 tag = item->key();

    assert(state_ != SHAMapState::Immutable);

    SharedPtrNodeStack stack;
    walkTowardsKey(tag, &stack);

    if (stack.empty())
        Throw<SHAMapMissingNode>(type_, tag);

    auto node = std::dynamic_pointer_cast<SHAMapLeafNode>(stack.top().first);
    auto nodeID = stack.top().second;
    stack.pop();

    if (!node || (node->peekItem()->key() != tag))
    {
        assert(false);
        return false;
    }

    if (node->getType() != type)
    {
        JLOG(journal_.fatal()) << "SHAMap::updateGiveItem: cross-type change!";
        return false;
    }

    node = unshareNode(std::move(node), nodeID);

    if (node->setItem(item))
        dirtyUp(stack, tag, node);

    return true;
}

bool
SHAMap::fetchRoot(SHAMapHash const& hash, SHAMapSyncFilter* filter)
{
    if (hash == root_->getHash())
        return true;

    if (auto stream = journal_.trace())
    {
        if (type_ == SHAMapType::TRANSACTION)
        {
            stream << "Fetch root TXN node " << hash;
        }
        else if (type_ == SHAMapType::STATE)
        {
            stream << "Fetch root STATE node " << hash;
        }
        else
        {
            stream << "Fetch root SHAMap node " << hash;
        }
    }

    auto newRoot = fetchNodeNT(hash, filter);

    if (newRoot)
    {
        root_ = newRoot;
        assert(root_->getHash() == hash);
        return true;
    }

    return false;
}

/** Replace a node with a shareable node.

    This code handles two cases:

    1) An unshared, unshareable node needs to be made shareable
       so immutable SHAMap's can have references to it.
    2) An unshareable node is shared. This happens when you make
       a mutable snapshot of a mutable SHAMap.

    @note The node must have already been unshared by having the caller
          first call SHAMapTreeNode::unshare().
 */
std::shared_ptr<SHAMapTreeNode>
SHAMap::writeNode(NodeObjectType t, std::shared_ptr<SHAMapTreeNode> node) const
{
    assert(node->cowid() == 0);
    assert(backed_);

    canonicalize(node->getHash(), node);

    Serializer s;
    node->serializeWithPrefix(s);
    f_.db().store(
        t, std::move(s.modData()), node->getHash().as_uint256(), ledgerSeq_);
    return node;
}

// We can't modify an inner node someone else might have a
// pointer to because flushing modifies inner nodes -- it
// makes them point to canonical/shared nodes.
template <class Node>
std::shared_ptr<Node>
SHAMap::preFlushNode(std::shared_ptr<Node> node) const
{
    // A shared node should never need to be flushed
    // because that would imply someone modified it
    assert(node->cowid() != 0);

    if (node->cowid() != cowid_)
    {
        // Node is not uniquely ours, so unshare it before
        // possibly modifying it
        node = std::static_pointer_cast<Node>(node->clone(cowid_));
    }
    return node;
}

int
SHAMap::unshare()
{
    // Don't share nodes with parent map
    return walkSubTree(false, hotUNKNOWN);
}

int
SHAMap::flushDirty(NodeObjectType t)
{
    // We only write back if this map is backed.
    return walkSubTree(backed_, t);
}

int
SHAMap::walkSubTree(bool doWrite, NodeObjectType t)
{
    assert(!doWrite || backed_);

    int flushed = 0;

    if (!root_ || (root_->cowid() == 0))
        return flushed;

    if (root_->isLeaf())
    {  // special case -- root_ is leaf
        root_ = preFlushNode(std::move(root_));
        root_->updateHash();
        root_->unshare();

        if (doWrite)
            root_ = writeNode(t, std::move(root_));

        return 1;
    }

    auto node = std::static_pointer_cast<SHAMapInnerNode>(root_);

    if (node->isEmpty())
    {  // replace empty root with a new empty root
        root_ = std::make_shared<SHAMapInnerNode>(0);
        return 1;
    }

    // Stack of {parent,index,child} pointers representing
    // inner nodes we are in the process of flushing
    using StackEntry = std::pair<std::shared_ptr<SHAMapInnerNode>, int>;
    std::stack<StackEntry, std::vector<StackEntry>> stack;

    node = preFlushNode(std::move(node));

    int pos = 0;

    // We can't flush an inner node until we flush its children
    while (1)
    {
        while (pos < branchFactor)
        {
            if (node->isEmptyBranch(pos))
            {
                ++pos;
            }
            else
            {
                // No need to do I/O. If the node isn't linked,
                // it can't need to be flushed
                int branch = pos;
                auto child = node->getChild(pos++);

                if (child && (child->cowid() != 0))
                {
                    // This is a node that needs to be flushed

                    child = preFlushNode(std::move(child));

                    if (child->isInner())
                    {
                        // save our place and work on this node

                        stack.emplace(std::move(node), branch);
                        // The semantics of this changes when we move to c++-20
                        // Right now no move will occur; With c++-20 child will
                        // be moved from.
                        node = std::static_pointer_cast<SHAMapInnerNode>(
                            std::move(child));
                        pos = 0;
                    }
                    else
                    {
                        // flush this leaf
                        ++flushed;

                        assert(node->cowid() == cowid_);
                        child->updateHash();
                        child->unshare();

                        if (doWrite)
                            child = writeNode(t, std::move(child));

                        node->shareChild(branch, child);
                    }
                }
            }
        }

        // update the hash of this inner node
        node->updateHashDeep();

        // This inner node can now be shared
        node->unshare();

        if (doWrite)
            node = std::static_pointer_cast<SHAMapInnerNode>(
                writeNode(t, std::move(node)));

        ++flushed;

        if (stack.empty())
            break;

        auto parent = std::move(stack.top().first);
        pos = stack.top().second;
        stack.pop();

        // Hook this inner node to its parent
        assert(parent->cowid() == cowid_);
        parent->shareChild(pos, node);

        // Continue with parent's next child, if any
        node = std::move(parent);
        ++pos;
    }

    // Last inner node is the new root_
    root_ = std::move(node);

    return flushed;
}

void
SHAMap::dump(bool hash) const
{
    int leafCount = 0;
    JLOG(journal_.info()) << " MAP Contains";

    std::stack<std::pair<SHAMapTreeNode*, SHAMapNodeID>> stack;
    stack.push({root_.get(), SHAMapNodeID()});

    do
    {
        auto [node, nodeID] = stack.top();
        stack.pop();

        JLOG(journal_.info()) << node->getString(nodeID);
        if (hash)
        {
            JLOG(journal_.info()) << "Hash: " << node->getHash();
        }

        if (node->isInner())
        {
            auto inner = static_cast<SHAMapInnerNode*>(node);
            for (int i = 0; i < branchFactor; ++i)
            {
                if (!inner->isEmptyBranch(i))
                {
                    auto child = inner->getChildPointer(i);
                    if (child)
                    {
                        assert(child->getHash() == inner->getChildHash(i));
                        stack.push({child, nodeID.getChildNodeID(i)});
                    }
                }
            }
        }
        else
            ++leafCount;
    } while (!stack.empty());

    JLOG(journal_.info()) << leafCount << " resident leaves";
}

std::shared_ptr<SHAMapTreeNode>
SHAMap::cacheLookup(SHAMapHash const& hash) const
{
    auto ret = f_.getTreeNodeCache(ledgerSeq_)->fetch(hash.as_uint256());
    assert(!ret || !ret->cowid());
    return ret;
}

void
SHAMap::canonicalize(
    SHAMapHash const& hash,
    std::shared_ptr<SHAMapTreeNode>& node) const
{
    assert(backed_);
    assert(node->cowid() == 0);
    assert(node->getHash() == hash);

    f_.getTreeNodeCache(ledgerSeq_)
        ->canonicalize_replace_client(hash.as_uint256(), node);
}

void
SHAMap::invariants() const
{
    (void)getHash();  // update node hashes
    auto node = root_.get();
    assert(node != nullptr);
    assert(!node->isLeaf());
    SharedPtrNodeStack stack;
    for (auto leaf = peekFirstItem(stack); leaf != nullptr;
         leaf = peekNextItem(leaf->peekItem()->key(), stack))
        ;
    node->invariants(true);
}

}  // namespace ripple
