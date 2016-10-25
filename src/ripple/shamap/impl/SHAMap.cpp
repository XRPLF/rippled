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
#include <ripple/basics/contract.h>
#include <ripple/shamap/SHAMap.h>
#include <ripple/beast/unit_test.h>

namespace ripple {

SHAMap::SHAMap (
    SHAMapType t,
    Family& f,
    version v)
    : f_ (f)
    , journal_(f.journal())
    , seq_ (1)
    , state_ (SHAMapState::Modifying)
    , type_ (t)
{
    if (v == version{2})
        root_ = std::make_shared<SHAMapInnerNodeV2>(seq_, 0);
    else
        root_ = std::make_shared<SHAMapInnerNode>(seq_);
}

SHAMap::SHAMap (
    SHAMapType t,
    uint256 const& hash,
    Family& f,
    version v)
    : f_ (f)
    , journal_(f.journal())
    , seq_ (1)
    , state_ (SHAMapState::Synching)
    , type_ (t)
{
    if (v == version{2})
        root_ = std::make_shared<SHAMapInnerNodeV2>(seq_, 0);
    else
        root_ = std::make_shared<SHAMapInnerNode>(seq_);
}

SHAMap::~SHAMap ()
{
    state_ = SHAMapState::Invalid;
}

std::shared_ptr<SHAMap>
SHAMap::snapShot (bool isMutable) const
{
    auto ret = std::make_shared<SHAMap> (type_, f_, get_version());
    SHAMap& newMap = *ret;

    if (!isMutable)
        newMap.state_ = SHAMapState::Immutable;

    newMap.seq_ = seq_ + 1;
    newMap.root_ = root_;
    newMap.backed_ = backed_;

    if ((state_ != SHAMapState::Immutable) || !isMutable)
    {
        // If either map may change, they cannot share nodes
        newMap.unshare ();
    }

    return ret;
}

std::shared_ptr<SHAMap>
SHAMap::make_v2() const
{
    assert(!is_v2());
    auto ret = std::make_shared<SHAMap>(type_, f_, version{2});
    ret->seq_ = seq_ + 1;
    SharedPtrNodeStack stack;
    for (auto leaf = peekFirstItem(stack); leaf != nullptr;
         leaf = peekNextItem(leaf->peekItem()->key(), stack))
    {
        auto node_type = leaf->getType();
        ret->addGiveItem(leaf->peekItem(),
                         node_type != SHAMapTreeNode::tnACCOUNT_STATE,
                         node_type == SHAMapTreeNode::tnTRANSACTION_MD);
    }
    NodeObjectType t;
    switch (type_)
    {
    case SHAMapType::TRANSACTION:
        t = hotTRANSACTION_NODE;
        break;
    case SHAMapType::STATE:
        t = hotACCOUNT_NODE;
        break;
    default:
        t = hotUNKNOWN;
        break;
    }
    ret->flushDirty(t, ret->seq_);
    ret->unshare();
    return ret;
}

std::shared_ptr<SHAMap>
SHAMap::make_v1() const
{
    assert(is_v2());
    auto ret = std::make_shared<SHAMap>(type_, f_, version{1});
    ret->seq_ = seq_ + 1;
    SharedPtrNodeStack stack;
    for (auto leaf = peekFirstItem(stack); leaf != nullptr;
         leaf = peekNextItem(leaf->peekItem()->key(), stack))
    {
        auto node_type = leaf->getType();
        ret->addGiveItem(leaf->peekItem(),
                         node_type != SHAMapTreeNode::tnACCOUNT_STATE,
                         node_type == SHAMapTreeNode::tnTRANSACTION_MD);
    }
    NodeObjectType t;
    switch (type_)
    {
    case SHAMapType::TRANSACTION:
        t = hotTRANSACTION_NODE;
        break;
    case SHAMapType::STATE:
        t = hotACCOUNT_NODE;
        break;
    default:
        t = hotUNKNOWN;
        break;
    }
    ret->flushDirty(t, ret->seq_);
    ret->unshare();
    return ret;
}

void
SHAMap::dirtyUp (SharedPtrNodeStack& stack,
                 uint256 const& target, std::shared_ptr<SHAMapAbstractNode> child)
{
    // walk the tree up from through the inner nodes to the root_
    // update hashes and links
    // stack is a path of inner nodes up to, but not including, child
    // child can be an inner node or a leaf

    assert ((state_ != SHAMapState::Synching) && (state_ != SHAMapState::Immutable));
    assert (child && (child->getSeq() == seq_));

    while (!stack.empty ())
    {
        auto node = std::dynamic_pointer_cast<SHAMapInnerNode>(stack.top ().first);
        SHAMapNodeID nodeID = stack.top ().second;
        stack.pop ();
        assert (node != nullptr);

        int branch = nodeID.selectBranch (target);
        assert (branch >= 0);

        node = unshareNode(std::move(node), nodeID);
        node->setChild (branch, child);

        child = std::move (node);
    }
}

SHAMapTreeNode*
SHAMap::walkTowardsKey(uint256 const& id, SharedPtrNodeStack* stack) const
{
    assert(stack == nullptr || stack->empty());
    auto inNode = root_;
    SHAMapNodeID nodeID;
    auto const isv2 = is_v2();

    while (inNode->isInner())
    {
        if (stack != nullptr)
            stack->push({inNode, nodeID});

        if (isv2)
        {
            auto n = std::static_pointer_cast<SHAMapInnerNodeV2>(inNode);
            if (!n->has_common_prefix(id))
                return nullptr;
        }
        auto const inner = std::static_pointer_cast<SHAMapInnerNode>(inNode);
        auto const branch = nodeID.selectBranch (id);
        if (inner->isEmptyBranch (branch))
            return nullptr;

        inNode = descendThrow (inner, branch);
        if (isv2)
        {
            if (inNode->isInner())
            {
                auto n = std::dynamic_pointer_cast<SHAMapInnerNodeV2>(inNode);
                if (n == nullptr)
                {
                    assert (false);
                    return nullptr;
                }
                nodeID = SHAMapNodeID{n->depth(), n->common()};
            }
            else
            {
                nodeID = SHAMapNodeID{64, inNode->key()};
            }
        }
        else
        {
            nodeID = nodeID.getChildNodeID (branch);
        }
    }

    if (stack != nullptr)
        stack->push({inNode, nodeID});
    return static_cast<SHAMapTreeNode*>(inNode.get());
}

SHAMapTreeNode*
SHAMap::findKey(uint256 const& id) const
{
    SHAMapTreeNode* leaf = walkTowardsKey(id);
    if (leaf && leaf->peekItem()->key() != id)
        leaf = nullptr;
    return leaf;
}

std::shared_ptr<SHAMapAbstractNode>
SHAMap::fetchNodeFromDB (SHAMapHash const& hash) const
{
    std::shared_ptr<SHAMapAbstractNode> node;

    if (backed_)
    {
        std::shared_ptr<NodeObject> obj = f_.db().fetch (hash.as_uint256());
        if (obj)
        {
            try
            {
                node = SHAMapAbstractNode::make(makeSlice(obj->getData()),
                    0, snfPREFIX, hash, true, f_.journal());
                if (node && node->isInner())
                {
                    bool isv2 = std::dynamic_pointer_cast<SHAMapInnerNodeV2>(node) != nullptr;
                    if (isv2 != is_v2())
                    {
                        auto root =  std::dynamic_pointer_cast<SHAMapInnerNode>(root_);
                        assert(root);
                        assert(root->isEmpty());
                        if (isv2)
                        {
                            auto temp = make_v2();
                            swap(temp->root_, const_cast<std::shared_ptr<SHAMapAbstractNode>&>(root_));
                        }
                        else
                        {
                            auto temp = make_v1();
                            swap(temp->root_, const_cast<std::shared_ptr<SHAMapAbstractNode>&>(root_));
                        }
                    }
                }
                if (node)
                    canonicalize (hash, node);
            }
            catch (std::exception const&)
            {
                JLOG(journal_.warn()) <<
                    "Invalid DB node " << hash;
                return std::shared_ptr<SHAMapTreeNode> ();
            }
        }
        else if (ledgerSeq_ != 0)
        {
            f_.missing_node(ledgerSeq_);
            const_cast<std::uint32_t&>(ledgerSeq_) = 0;
        }
    }

    return node;
}

// See if a sync filter has a node
std::shared_ptr<SHAMapAbstractNode>
SHAMap::checkFilter(SHAMapHash const& hash,
                    SHAMapSyncFilter* filter) const
{
    std::shared_ptr<SHAMapAbstractNode> node;
    if (auto nodeData = filter->getNode (hash))
    {
        node = SHAMapAbstractNode::make(
            makeSlice(*nodeData), 0, snfPREFIX, hash, true, f_.journal ());
        if (node)
        {
            filter->gotNode (true, hash,
                std::move(*nodeData), node->getType ());
            if (backed_)
                canonicalize (hash, node);
        }
    }
    return node;
}

// Get a node without throwing
// Used on maps where missing nodes are expected
std::shared_ptr<SHAMapAbstractNode> SHAMap::fetchNodeNT(
    SHAMapHash const& hash,
    SHAMapSyncFilter* filter) const
{
    std::shared_ptr<SHAMapAbstractNode> node = getCache (hash);
    if (node)
        return node;

    if (backed_)
    {
        node = fetchNodeFromDB (hash);
        if (node)
        {
            canonicalize (hash, node);
            return node;
        }
    }

    if (filter)
        node = checkFilter (hash, filter);

    return node;
}

std::shared_ptr<SHAMapAbstractNode> SHAMap::fetchNodeNT (SHAMapHash const& hash) const
{
    auto node = getCache (hash);

    if (!node && backed_)
        node = fetchNodeFromDB (hash);

    return node;
}

// Throw if the node is missing
std::shared_ptr<SHAMapAbstractNode> SHAMap::fetchNode (SHAMapHash const& hash) const
{
    auto node = fetchNodeNT (hash);

    if (!node)
        Throw<SHAMapMissingNode> (type_, hash);

    return node;
}

SHAMapAbstractNode* SHAMap::descendThrow (SHAMapInnerNode* parent, int branch) const
{
    SHAMapAbstractNode* ret = descend (parent, branch);

    if (! ret && ! parent->isEmptyBranch (branch))
        Throw<SHAMapMissingNode> (type_, parent->getChildHash (branch));

    return ret;
}

std::shared_ptr<SHAMapAbstractNode>
SHAMap::descendThrow (std::shared_ptr<SHAMapInnerNode> const& parent, int branch) const
{
    std::shared_ptr<SHAMapAbstractNode> ret = descend (parent, branch);

    if (! ret && ! parent->isEmptyBranch (branch))
        Throw<SHAMapMissingNode> (type_, parent->getChildHash (branch));

    return ret;
}

SHAMapAbstractNode* SHAMap::descend (SHAMapInnerNode* parent, int branch) const
{
    SHAMapAbstractNode* ret = parent->getChildPointer (branch);
    if (ret || !backed_)
        return ret;

    std::shared_ptr<SHAMapAbstractNode> node = fetchNodeNT (parent->getChildHash (branch));
    if (!node || isInconsistentNode(node))
        return nullptr;

    node = parent->canonicalizeChild (branch, std::move(node));
    return node.get ();
}

std::shared_ptr<SHAMapAbstractNode>
SHAMap::descend (std::shared_ptr<SHAMapInnerNode> const& parent, int branch) const
{
    std::shared_ptr<SHAMapAbstractNode> node = parent->getChild (branch);
    if (node || !backed_)
        return node;

    node = fetchNode (parent->getChildHash (branch));
    if (!node || isInconsistentNode(node))
        return nullptr;

    node = parent->canonicalizeChild (branch, std::move(node));
    return node;
}

// Gets the node that would be hooked to this branch,
// but doesn't hook it up.
std::shared_ptr<SHAMapAbstractNode>
SHAMap::descendNoStore (std::shared_ptr<SHAMapInnerNode> const& parent, int branch) const
{
    std::shared_ptr<SHAMapAbstractNode> ret = parent->getChild (branch);
    if (!ret && backed_)
        ret = fetchNode (parent->getChildHash (branch));
    return ret;
}

std::pair <SHAMapAbstractNode*, SHAMapNodeID>
SHAMap::descend (SHAMapInnerNode * parent, SHAMapNodeID const& parentID,
    int branch, SHAMapSyncFilter * filter) const
{
    assert (parent->isInner ());
    assert ((branch >= 0) && (branch < 16));
    assert (!parent->isEmptyBranch (branch));

    SHAMapAbstractNode* child = parent->getChildPointer (branch);
    auto const& childHash = parent->getChildHash (branch);

    if (!child)
    {
        std::shared_ptr<SHAMapAbstractNode> childNode = fetchNodeNT (childHash, filter);

        if (childNode)
        {
            childNode = parent->canonicalizeChild (branch, std::move(childNode));
            child = childNode.get ();
        }

        if (child && isInconsistentNode(childNode))
            child = nullptr;
    }

    if (child && is_v2())
    {
        if (child->isInner())
        {
            auto n = static_cast<SHAMapInnerNodeV2*>(child);
            return std::make_pair(child, SHAMapNodeID{n->depth(), n->key()});
        }
        return std::make_pair(child, SHAMapNodeID{64, child->key()});
    }

    return std::make_pair (child, parentID.getChildNodeID (branch));
}

SHAMapAbstractNode*
SHAMap::descendAsync (SHAMapInnerNode* parent, int branch,
    SHAMapSyncFilter * filter, bool & pending) const
{
    pending = false;

    SHAMapAbstractNode* ret = parent->getChildPointer (branch);
    if (ret)
        return ret;

    auto const& hash = parent->getChildHash (branch);

    std::shared_ptr<SHAMapAbstractNode> ptr = getCache (hash);
    if (!ptr)
    {
        if (filter)
            ptr = checkFilter (hash, filter);

        if (!ptr && backed_)
        {
            std::shared_ptr<NodeObject> obj;
            if (! f_.db().asyncFetch (hash.as_uint256(), obj))
            {
                pending = true;
                return nullptr;
            }
            if (!obj)
                return nullptr;

            ptr = SHAMapAbstractNode::make(makeSlice(obj->getData()), 0, snfPREFIX,
                                           hash, true, f_.journal());
            if (ptr && backed_)
                canonicalize (hash, ptr);
        }
    }

    if (ptr && isInconsistentNode(ptr))
        ptr = nullptr;
    if (ptr)
        ptr = parent->canonicalizeChild (branch, std::move(ptr));

    return ptr.get ();
}

template <class Node>
std::shared_ptr<Node>
SHAMap::unshareNode (std::shared_ptr<Node> node, SHAMapNodeID const& nodeID)
{
    // make sure the node is suitable for the intended operation (copy on write)
    assert (node->isValid ());
    assert (node->getSeq () <= seq_);
    if (node->getSeq () != seq_)
    {
        // have a CoW
        assert (state_ != SHAMapState::Immutable);
        node = std::static_pointer_cast<Node>(node->clone(seq_));
        assert (node->isValid ());
        if (nodeID.isRoot ())
            root_ = node;
    }
    return node;
}

SHAMapTreeNode*
SHAMap::firstBelow(std::shared_ptr<SHAMapAbstractNode> node,
                   SharedPtrNodeStack& stack, int branch) const
{
    // Return the first item at or below this node
    if (node->isLeaf())
    {
        auto n = std::static_pointer_cast<SHAMapTreeNode>(node);
        stack.push({node, {64, n->peekItem()->key()}});
        return n.get();
    }
    auto inner = std::static_pointer_cast<SHAMapInnerNode>(node);
    if (stack.empty())
        stack.push({inner, SHAMapNodeID{}});
    else
    {
        if (is_v2())
        {
            auto inner2 = std::dynamic_pointer_cast<SHAMapInnerNodeV2>(inner);
            assert(inner2 != nullptr);
            stack.push({inner2, {inner2->depth(), inner2->common()}});
        }
        else
        {
            stack.push({inner, stack.top().second.getChildNodeID(branch)});
        }
    }
    for (int i = 0; i < 16;)
    {
        if (!inner->isEmptyBranch(i))
        {
            node = descendThrow(inner, i);
            assert(!stack.empty());
            if (node->isLeaf())
            {
                auto n = std::static_pointer_cast<SHAMapTreeNode>(node);
                stack.push({n, {64, n->peekItem()->key()}});
                return n.get();
            }
            inner = std::static_pointer_cast<SHAMapInnerNode>(node);
            if (is_v2())
            {
                auto inner2 = std::static_pointer_cast<SHAMapInnerNodeV2>(inner);
                stack.push({inner2, {inner2->depth(), inner2->common()}});
            }
            else
            {
                stack.push({inner, stack.top().second.getChildNodeID(branch)});
            }
            i = 0;  // scan all 16 branches of this new node
        }
        else
            ++i;  // scan next branch
    }
    return nullptr;
}

static const std::shared_ptr<SHAMapItem const> no_item;

std::shared_ptr<SHAMapItem const> const&
SHAMap::onlyBelow (SHAMapAbstractNode* node) const
{
    // If there is only one item below this node, return it

    while (!node->isLeaf ())
    {
        SHAMapAbstractNode* nextNode = nullptr;
        auto inner = static_cast<SHAMapInnerNode*>(node);
        for (int i = 0; i < 16; ++i)
        {
            if (!inner->isEmptyBranch (i))
            {
                if (nextNode)
                    return no_item;

                nextNode = descendThrow (inner, i);
            }
        }

        if (!nextNode)
        {
            assert (false);
            return no_item;
        }

        node = nextNode;
    }

    // An inner node must have at least one leaf
    // below it, unless it's the root_
    auto leaf = static_cast<SHAMapTreeNode*>(node);
    assert (leaf->hasItem () || (leaf == root_.get ()));

    return leaf->peekItem ();
}

static std::shared_ptr<
    SHAMapItem const> const nullConstSHAMapItem;

SHAMapTreeNode const*
SHAMap::peekFirstItem(SharedPtrNodeStack& stack) const
{
    assert(stack.empty());
    SHAMapTreeNode* node = firstBelow(root_, stack);
    if (!node)
    {
        while (!stack.empty())
            stack.pop();
        return nullptr;
    }
    return node;
}

SHAMapTreeNode const*
SHAMap::peekNextItem(uint256 const& id, SharedPtrNodeStack& stack) const
{
    assert(!stack.empty());
    assert(stack.top().first->isLeaf());
    stack.pop();
    while (!stack.empty())
    {
        auto node = stack.top().first;
        auto nodeID = stack.top().second;
        assert(!node->isLeaf());
        auto inner = std::static_pointer_cast<SHAMapInnerNode>(node);
        for (auto i = nodeID.selectBranch(id) + 1; i < 16; ++i)
        {
            if (!inner->isEmptyBranch(i))
            {
                node = descendThrow(inner, i);
                auto leaf = firstBelow(node, stack, i);
                if (!leaf)
                    Throw<SHAMapMissingNode> (type_, id);
                assert(leaf->isLeaf());
                return leaf;
            }
        }
        stack.pop();
    }
    // must be last item
    return nullptr;
}

std::shared_ptr<SHAMapItem const> const&
SHAMap::peekItem (uint256 const& id) const
{
    SHAMapTreeNode* leaf = findKey(id);

    if (!leaf)
        return no_item;

    return leaf->peekItem ();
}

std::shared_ptr<SHAMapItem const> const&
SHAMap::peekItem (uint256 const& id, SHAMapTreeNode::TNType& type) const
{
    SHAMapTreeNode* leaf = findKey(id);

    if (!leaf)
        return no_item;

    type = leaf->getType ();
    return leaf->peekItem ();
}

std::shared_ptr<SHAMapItem const> const&
SHAMap::peekItem (uint256 const& id, SHAMapHash& hash) const
{
    SHAMapTreeNode* leaf = findKey(id);

    if (!leaf)
        return no_item;

    hash = leaf->getNodeHash ();
    return leaf->peekItem ();
}

SHAMap::const_iterator
SHAMap::upper_bound(uint256 const& id) const
{
    // Get a const_iterator to the next item in the tree after a given item
    // item need not be in tree
    SharedPtrNodeStack stack;
    walkTowardsKey(id, &stack);
    std::shared_ptr<SHAMapAbstractNode> node;
    SHAMapNodeID nodeID;
    auto const isv2 = is_v2();
    while (!stack.empty())
    {
        std::tie(node, nodeID) = stack.top();
        if (node->isLeaf())
        {
            auto leaf = static_cast<SHAMapTreeNode*>(node.get());
            if (leaf->peekItem()->key() > id)
                return const_iterator(this, leaf->peekItem().get(), std::move(stack));
        }
        else
        {
            auto inner = std::static_pointer_cast<SHAMapInnerNode>(node);
            int branch;
            if (isv2)
            {
                auto n = std::static_pointer_cast<SHAMapInnerNodeV2>(inner);
                if (n->has_common_prefix(id))
                    branch = nodeID.selectBranch(id) + 1;
                else if (id < n->common())
                    branch = 0;
                else
                    branch = 16;
            }
            else
            {
                branch = nodeID.selectBranch(id) + 1;
            }
            for (; branch < 16; ++branch)
            {
                if (!inner->isEmptyBranch(branch))
                {
                    node = descendThrow(inner, branch);
                    auto leaf = firstBelow(node, stack, branch);
                    if (!leaf)
                        Throw<SHAMapMissingNode> (type_, id);
                    return const_iterator(this, leaf->peekItem().get(),
                                          std::move(stack));
                }
            }
        }
        stack.pop();
    }
    return end();
}

bool SHAMap::hasItem (uint256 const& id) const
{
    // does the tree have an item with this ID
    SHAMapTreeNode* leaf = findKey(id);
    return (leaf != nullptr);
}

bool SHAMap::delItem (uint256 const& id)
{
    // delete the item with this ID
    assert (state_ != SHAMapState::Immutable);

    SharedPtrNodeStack stack;
    walkTowardsKey(id, &stack);

    if (stack.empty ())
        Throw<SHAMapMissingNode> (type_, id);

    auto leaf = std::dynamic_pointer_cast<SHAMapTreeNode>(stack.top ().first);
    stack.pop ();

    if (!leaf || (leaf->peekItem ()->key() != id))
        return false;

    SHAMapTreeNode::TNType type = leaf->getType ();

    // What gets attached to the end of the chain
    // (For now, nothing, since we deleted the leaf)
    std::shared_ptr<SHAMapAbstractNode> prevNode;

    while (!stack.empty ())
    {
        auto node = std::static_pointer_cast<SHAMapInnerNode>(stack.top().first);
        SHAMapNodeID nodeID = stack.top().second;
        stack.pop();

        node = unshareNode(std::move(node), nodeID);
        node->setChild(nodeID.selectBranch(id), prevNode);

        if (!nodeID.isRoot ())
        {
            // we may have made this a node with 1 or 0 children
            // And, if so, we need to remove this branch
            int bc = node->getBranchCount();
            if (is_v2())
            {
                assert(bc != 0);
                if (bc == 1)
                {
                    for (int i = 0; i < 16; ++i)
                    {
                        if (!node->isEmptyBranch (i))
                        {
                            prevNode = descendThrow(node, i);
                            break;
                        }
                    }
                }
                else  // bc >= 2
                {
                    // This node is now the end of the branch
                    prevNode = std::move(node);
                }
            }
            else
            {
                if (bc == 0)
                {
                    // no children below this branch
                    prevNode.reset ();
                }
                else if (bc == 1)
                {
                    // If there's only one item, pull up on the thread
                    auto item = onlyBelow (node.get ());

                    if (item)
                    {
                        for (int i = 0; i < 16; ++i)
                        {
                            if (!node->isEmptyBranch (i))
                            {
                                node->setChild (i, nullptr);
                                break;
                            }
                        }
                        prevNode = std::make_shared<SHAMapTreeNode>(item, type, node->getSeq());
                    }
                    else
                    {
                        prevNode = std::move (node);
                    }
                }
                else
                {
                    // This node is now the end of the branch
                    prevNode = std::move (node);
                }
            }
        }
    }

    return true;
}

static
uint256
prefix(unsigned depth, uint256 const& key)
{
    uint256 r{};
    auto x = r.begin();
    auto y = key.begin();
    for (auto i = 0; i < depth/2; ++i, ++x, ++y)
        *x = *y;
    if (depth & 1)
        *x = *y & 0xF0;
    return r;
}

bool
SHAMap::addGiveItem (std::shared_ptr<SHAMapItem const> const& item,
                     bool isTransaction, bool hasMeta)
{
    // add the specified item, does not update
    uint256 tag = item->key();
    SHAMapTreeNode::TNType type = !isTransaction ? SHAMapTreeNode::tnACCOUNT_STATE :
        (hasMeta ? SHAMapTreeNode::tnTRANSACTION_MD : SHAMapTreeNode::tnTRANSACTION_NM);

    assert (state_ != SHAMapState::Immutable);

    SharedPtrNodeStack stack;
    walkTowardsKey(tag, &stack);

    if (stack.empty ())
        Throw<SHAMapMissingNode> (type_, tag);

    auto node = stack.top ().first;
    auto nodeID = stack.top ().second;
    stack.pop ();

    if (node->isLeaf())
    {
        auto leaf = std::static_pointer_cast<SHAMapTreeNode>(node);
        if (leaf->peekItem()->key() == tag)
            return false;
    }
    node = unshareNode(std::move(node), nodeID);
    if (is_v2())
    {
        if (node->isInner())
        {
            auto inner = std::static_pointer_cast<SHAMapInnerNodeV2>(node);
            if (inner->has_common_prefix(tag))
            {
                int branch = nodeID.selectBranch(tag);
                assert(inner->isEmptyBranch(branch));
                auto newNode = std::make_shared<SHAMapTreeNode>(item, type, seq_);
                inner->setChild(branch, newNode);
            }
            else
            {
                assert(!stack.empty());
                auto parent = unshareNode(
                    std::static_pointer_cast<SHAMapInnerNodeV2>(stack.top().first),
                    stack.top().second);
                stack.top().first = parent;
                auto parent_depth = parent->depth();
                auto depth = inner->get_common_prefix(tag);
                auto new_inner = std::make_shared<SHAMapInnerNodeV2>(seq_);
                nodeID = SHAMapNodeID{depth, prefix(depth, inner->common())};
                new_inner->setChild(nodeID.selectBranch(inner->common()), inner);
                nodeID = SHAMapNodeID{depth, prefix(depth, tag)};
                new_inner->setChild(nodeID.selectBranch(tag),
                                    std::make_shared<SHAMapTreeNode>(item, type, seq_));
                new_inner->set_common(depth, prefix(depth, tag));
                nodeID = SHAMapNodeID{parent_depth, prefix(parent_depth, tag)};
                parent->setChild(nodeID.selectBranch(tag), new_inner);
                node = new_inner;
            }
        }
        else
        {
            auto leaf = std::static_pointer_cast<SHAMapTreeNode>(node);
            auto inner = std::make_shared<SHAMapInnerNodeV2>(seq_);
            inner->setChildren(leaf, std::make_shared<SHAMapTreeNode>(item, type, seq_));
            assert(!stack.empty());
            auto parent = unshareNode(
                std::static_pointer_cast<SHAMapInnerNodeV2>(stack.top().first),
                stack.top().second);
            stack.top().first = parent;
            node = inner;
        }
    }
    else  // !is_v2()
    {
        if (node->isInner ())
        {
            // easy case, we end on an inner node
            auto inner = std::static_pointer_cast<SHAMapInnerNode>(node);
            int branch = nodeID.selectBranch (tag);
            assert (inner->isEmptyBranch (branch));
            auto newNode = std::make_shared<SHAMapTreeNode> (item, type, seq_);
            inner->setChild (branch, newNode);
        }
        else
        {
            // this is a leaf node that has to be made an inner node holding two items
            auto leaf = std::static_pointer_cast<SHAMapTreeNode>(node);
            std::shared_ptr<SHAMapItem const> otherItem = leaf->peekItem ();
            assert (otherItem && (tag != otherItem->key()));

            node = std::make_shared<SHAMapInnerNode>(node->getSeq());

            int b1, b2;

            while ((b1 = nodeID.selectBranch (tag)) ==
                   (b2 = nodeID.selectBranch (otherItem->key())))
            {
                stack.push ({node, nodeID});

                // we need a new inner node, since both go on same branch at this level
                nodeID = nodeID.getChildNodeID (b1);
                node = std::make_shared<SHAMapInnerNode> (seq_);
            }

            // we can add the two leaf nodes here
            assert (node->isInner ());

            std::shared_ptr<SHAMapTreeNode> newNode =
                std::make_shared<SHAMapTreeNode> (item, type, seq_);
            assert (newNode->isValid () && newNode->isLeaf ());
            auto inner = std::static_pointer_cast<SHAMapInnerNode>(node);
            inner->setChild (b1, newNode);

            newNode = std::make_shared<SHAMapTreeNode> (otherItem, type, seq_);
            assert (newNode->isValid () && newNode->isLeaf ());
            inner->setChild (b2, newNode);
        }
    }

    dirtyUp (stack, tag, node);
    return true;
}

bool
SHAMap::addItem(SHAMapItem&& i, bool isTransaction, bool hasMetaData)
{
    return addGiveItem(std::make_shared<SHAMapItem const>(std::move(i)),
                                                          isTransaction, hasMetaData);
}

SHAMapHash
SHAMap::getHash () const
{
    auto hash = root_->getNodeHash();
    if (hash.isZero())
    {
        const_cast<SHAMap&>(*this).unshare();
        hash = root_->getNodeHash();
    }
    return hash;
}

bool
SHAMap::updateGiveItem (std::shared_ptr<SHAMapItem const> const& item,
                        bool isTransaction, bool hasMeta)
{
    // can't change the tag but can change the hash
    uint256 tag = item->key();

    assert (state_ != SHAMapState::Immutable);

    SharedPtrNodeStack stack;
    walkTowardsKey(tag, &stack);

    if (stack.empty ())
        Throw<SHAMapMissingNode> (type_, tag);

    auto node = std::dynamic_pointer_cast<SHAMapTreeNode>(stack.top().first);
    auto nodeID = stack.top ().second;
    stack.pop ();

    if (!node || (node->peekItem ()->key() != tag))
    {
        assert (false);
        return false;
    }

    node = unshareNode(std::move(node), nodeID);

    if (!node->setItem (item, !isTransaction ? SHAMapTreeNode::tnACCOUNT_STATE :
                        (hasMeta ? SHAMapTreeNode::tnTRANSACTION_MD : SHAMapTreeNode::tnTRANSACTION_NM)))
    {
        JLOG(journal_.trace()) <<
            "SHAMap setItem, no change";
        return true;
    }

    dirtyUp (stack, tag, node);
    return true;
}

bool SHAMap::fetchRoot (SHAMapHash const& hash, SHAMapSyncFilter* filter)
{
    if (hash == root_->getNodeHash ())
        return true;

    if (auto stream = journal_.trace())
    {
        if (type_ == SHAMapType::TRANSACTION)
        {
            stream
                << "Fetch root TXN node " << hash;
        }
        else if (type_ == SHAMapType::STATE)
        {
            stream <<
                "Fetch root STATE node " << hash;
        }
        else
        {
            stream <<
                "Fetch root SHAMap node " << hash;
        }
    }

    auto newRoot = fetchNodeNT (hash, filter);

    if (newRoot)
    {
        root_ = newRoot;
        assert (root_->getNodeHash () == hash);
        return true;
    }

    return false;
}

// Replace a node with a shareable node.
//
// This code handles two cases:
//
// 1) An unshared, unshareable node needs to be made shareable
// so immutable SHAMap's can have references to it.
//
// 2) An unshareable node is shared. This happens when you make
// a mutable snapshot of a mutable SHAMap.
std::shared_ptr<SHAMapAbstractNode>
SHAMap::writeNode (
    NodeObjectType t, std::uint32_t seq, std::shared_ptr<SHAMapAbstractNode> node) const
{
    // Node is ours, so we can just make it shareable
    assert (node->getSeq() == seq_);
    assert (backed_);
    node->setSeq (0);

    canonicalize (node->getNodeHash(), node);

    Serializer s;
    node->addRaw (s, snfPREFIX);
    f_.db().store (t,
        std::move (s.modData ()), node->getNodeHash ().as_uint256());
    return node;
}

// We can't modify an inner node someone else might have a
// pointer to because flushing modifies inner nodes -- it
// makes them point to canonical/shared nodes.
template <class Node>
std::shared_ptr<Node>
SHAMap::preFlushNode (std::shared_ptr<Node> node) const
{
    // A shared node should never need to be flushed
    // because that would imply someone modified it
    assert (node->getSeq() != 0);

    if (node->getSeq() != seq_)
    {
        // Node is not uniquely ours, so unshare it before
        // possibly modifying it
        node = std::static_pointer_cast<Node>(node->clone(seq_));
    }
    return node;
}

int SHAMap::unshare ()
{
    // Don't share nodes wth parent map
    return walkSubTree (false, hotUNKNOWN, 0);
}

/** Convert all modified nodes to shared nodes */
// If requested, write them to the node store
int SHAMap::flushDirty (NodeObjectType t, std::uint32_t seq)
{
    return walkSubTree (true, t, seq);
}

int
SHAMap::walkSubTree (bool doWrite, NodeObjectType t, std::uint32_t seq)
{
    int flushed = 0;
    Serializer s;

    if (!root_ || (root_->getSeq() == 0))
        return flushed;

    if (root_->isLeaf())
    { // special case -- root_ is leaf
        root_ = preFlushNode (std::move(root_));
        root_->updateHash();
        if (doWrite && backed_)
            root_ = writeNode(t, seq, std::move(root_));
        else
            root_->setSeq (0);
        return 1;
    }

    auto node = std::static_pointer_cast<SHAMapInnerNode>(root_);

    if (node->isEmpty ())
    { // replace empty root with a new empty root
        if (is_v2())
            root_ = std::make_shared<SHAMapInnerNodeV2>(0, 0);
        else
            root_ = std::make_shared<SHAMapInnerNode>(0);
        return 1;
    }

    // Stack of {parent,index,child} pointers representing
    // inner nodes we are in the process of flushing
    using StackEntry = std::pair <std::shared_ptr<SHAMapInnerNode>, int>;
    std::stack <StackEntry, std::vector<StackEntry>> stack;

    node = preFlushNode(std::move(node));

    int pos = 0;

    // We can't flush an inner node until we flush its children
    while (1)
    {
        while (pos < 16)
        {
            if (node->isEmptyBranch (pos))
            {
                ++pos;
            }
            else
            {
                // No need to do I/O. If the node isn't linked,
                // it can't need to be flushed
                int branch = pos;
                auto child = node->getChild(pos++);

                if (child && (child->getSeq() != 0))
                {
                    // This is a node that needs to be flushed

                    child = preFlushNode(std::move(child));

                    if (child->isInner ())
                    {
                        // save our place and work on this node

                        stack.emplace (std::move (node), branch);

                        node = std::static_pointer_cast<SHAMapInnerNode>(std::move(child));
                        pos = 0;
                    }
                    else
                    {
                        // flush this leaf
                        ++flushed;

                        assert (node->getSeq() == seq_);
                        child->updateHash();

                        if (doWrite && backed_)
                            child = writeNode(t, seq, std::move(child));
                        else
                            child->setSeq (0);

                        node->shareChild (branch, child);
                    }
                }
            }
        }

        // update the hash of this inner node
        node->updateHashDeep();

        // This inner node can now be shared
        if (doWrite && backed_)
            node = std::static_pointer_cast<SHAMapInnerNode>(writeNode(t, seq,
                                                                       std::move(node)));
        else
            node->setSeq (0);

        ++flushed;

        if (stack.empty ())
           break;

        auto parent = std::move (stack.top().first);
        pos = stack.top().second;
        stack.pop();

        // Hook this inner node to its parent
        assert (parent->getSeq() == seq_);
        parent->shareChild (pos, node);

        // Continue with parent's next child, if any
        node = std::move (parent);
        ++pos;
    }

    // Last inner node is the new root_
    root_ = std::move (node);

    return flushed;
}

void SHAMap::dump (bool hash) const
{
    int leafCount = 0;
    JLOG(journal_.info()) << " MAP Contains";

    std::stack <std::pair <SHAMapAbstractNode*, SHAMapNodeID> > stack;
    stack.push ({root_.get (), SHAMapNodeID ()});

    do
    {
        auto node = stack.top().first;
        auto nodeID = stack.top().second;
        stack.pop();

        JLOG(journal_.info()) << node->getString (nodeID);
        if (hash)
        {
           JLOG(journal_.info()) << "Hash: " << node->getNodeHash();
        }

        if (node->isInner ())
        {
            auto inner = static_cast<SHAMapInnerNode*>(node);
            for (int i = 0; i < 16; ++i)
            {
                if (!inner->isEmptyBranch (i))
                {
                    auto child = inner->getChildPointer (i);
                    if (child)
                    {
                        assert (child->getNodeHash() == inner->getChildHash (i));
                        stack.push ({child, nodeID.getChildNodeID (i)});
                     }
                }
            }
        }
        else
            ++leafCount;
    }
    while (!stack.empty ());

    JLOG(journal_.info()) << leafCount << " resident leaves";
}

std::shared_ptr<SHAMapAbstractNode> SHAMap::getCache (SHAMapHash const& hash) const
{
    auto ret = f_.treecache().fetch (hash.as_uint256());
    assert (!ret || !ret->getSeq());
    return ret;
}

void
SHAMap::canonicalize(SHAMapHash const& hash, std::shared_ptr<SHAMapAbstractNode>& node) const
{
    assert (backed_);
    assert (node->getSeq() == 0);
    assert (node->getNodeHash() == hash);

    f_.treecache().canonicalize (hash.as_uint256(), node);
}

SHAMap::version
SHAMap::get_version() const
{
    if (is_v2())
        return version{2};
    return version{1};
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
    node->invariants(is_v2(), true);
}

bool
SHAMap::isInconsistentNode(std::shared_ptr<SHAMapAbstractNode> const& node) const
{
    assert(root_);
    assert(node);
    if (std::dynamic_pointer_cast<SHAMapTreeNode>(node) != nullptr)
        return false;
    bool is_node_v2 = std::dynamic_pointer_cast<SHAMapInnerNodeV2>(node) != nullptr;
    assert (! is_node_v2 || (std::dynamic_pointer_cast<SHAMapInnerNodeV2>(node)->depth() != 0));

    if (is_v2() == is_node_v2)
        return false;

    state_ = SHAMapState::Invalid;
    return true;
}

} // ripple
