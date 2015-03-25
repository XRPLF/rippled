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
#include <ripple/shamap/SHAMap.h>
#include <beast/unit_test/suite.h>
#include <beast/chrono/manual_clock.h>

namespace ripple {

SHAMap::SHAMap (
    SHAMapType t,
    Family& f,
    beast::Journal journal,
    std::uint32_t seq)
    : f_ (f)
    , journal_(journal)
    , seq_ (seq)
    , ledgerSeq_ (0)
    , state_ (SHAMapState::Modifying)
    , type_ (t)
{
    assert (seq_ != 0);

    root_ = std::make_shared<SHAMapTreeNode> (seq_);
    root_->makeInner ();
}

SHAMap::SHAMap (
    SHAMapType t,
    uint256 const& hash,
    Family& f,
    beast::Journal journal)
    : f_ (f)
    , journal_(journal)
    , seq_ (1)
    , ledgerSeq_ (0)
    , state_ (SHAMapState::Synching)
    , type_ (t)
{
    root_ = std::make_shared<SHAMapTreeNode> (seq_);
    root_->makeInner ();
}

SHAMap::~SHAMap ()
{
    state_ = SHAMapState::Invalid;
}

std::shared_ptr<SHAMap>
SHAMap::snapShot (bool isMutable) const
{
    auto ret = std::make_shared<SHAMap> (type_, f_, journal_);
    SHAMap& newMap = *ret;

    if (!isMutable)
        newMap.state_ = SHAMapState::Immutable;

    newMap.seq_ = seq_ + 1;
    newMap.root_ = root_;

    if ((state_ != SHAMapState::Immutable) || isMutable)
    {
        // If either map may change, they cannot share nodes
        newMap.unshare ();
    }

    return ret;
}

SHAMap::SharedPtrNodeStack
SHAMap::getStack (uint256 const& id, bool include_nonmatching_leaf) const
{
    // Walk the tree as far as possible to the specified identifier
    // produce a stack of nodes along the way, with the terminal node at the top
    SharedPtrNodeStack stack;

    std::shared_ptr<SHAMapTreeNode> node = root_;
    SHAMapNodeID nodeID;

    while (!node->isLeaf ())
    {
        stack.push ({node, nodeID});

        int branch = nodeID.selectBranch (id);
        assert (branch >= 0);

        if (node->isEmptyBranch (branch))
            return stack;

        node = descendThrow (node, branch);
        nodeID = nodeID.getChildNodeID (branch);
    }

    if (include_nonmatching_leaf || (node->peekItem ()->getTag () == id))
        stack.push ({node, nodeID});

    return stack;
}

void
SHAMap::dirtyUp (SharedPtrNodeStack& stack,
                 uint256 const& target, std::shared_ptr<SHAMapTreeNode> child)
{
    // walk the tree up from through the inner nodes to the root_
    // update hashes and links
    // stack is a path of inner nodes up to, but not including, child
    // child can be an inner node or a leaf

    assert ((state_ != SHAMapState::Synching) && (state_ != SHAMapState::Immutable));
    assert (child && (child->getSeq() == seq_));

    while (!stack.empty ())
    {
        std::shared_ptr<SHAMapTreeNode> node = stack.top ().first;
        SHAMapNodeID nodeID = stack.top ().second;
        stack.pop ();
        assert (node->isInnerNode ());

        int branch = nodeID.selectBranch (target);
        assert (branch >= 0);

        unshareNode (node, nodeID);
        node->setChild (branch, child);

    #ifdef ST_DEBUG
        if (journal_.trace) journal_.trace <<
            "dirtyUp sets branch " << branch << " to " << prevHash;
    #endif
        child = std::move (node);
    }
}

SHAMapTreeNode* SHAMap::walkToPointer (uint256 const& id) const
{
    SHAMapTreeNode* inNode = root_.get ();
    SHAMapNodeID nodeID;
    uint256 nodeHash;

    while (inNode->isInner ())
    {
        int branch = nodeID.selectBranch (id);

        if (inNode->isEmptyBranch (branch))
            return nullptr;

        inNode = descendThrow (inNode, branch);
        nodeID = nodeID.getChildNodeID (branch);
    }

    return (inNode->peekItem()->getTag () == id) ? inNode : nullptr;
}

std::shared_ptr<SHAMapTreeNode>
SHAMap::fetchNodeFromDB (uint256 const& hash) const
{
    std::shared_ptr<SHAMapTreeNode> node;

    if (backed_)
    {
        NodeObject::pointer obj = f_.db().fetch (hash);
        if (obj)
        {
            try
            {
                node = std::make_shared <SHAMapTreeNode> (obj->getData(),
                    0, snfPREFIX, hash, true);
                canonicalize (hash, node);
            }
            catch (...)
            {
                if (journal_.warning) journal_.warning <<
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
std::shared_ptr<SHAMapTreeNode> SHAMap::checkFilter (
    uint256 const& hash,
    SHAMapNodeID const& id,
    SHAMapSyncFilter* filter) const
{
    std::shared_ptr<SHAMapTreeNode> node;
    Blob nodeData;

    if (filter->haveNode (id, hash, nodeData))
    {
        node = std::make_shared <SHAMapTreeNode> (
            nodeData, 0, snfPREFIX, hash, true);

       filter->gotNode (true, id, hash, nodeData, node->getType ());

       if (backed_)
           canonicalize (hash, node);
    }

    return node;
}

// Get a node without throwing
// Used on maps where missing nodes are expected
std::shared_ptr<SHAMapTreeNode> SHAMap::fetchNodeNT(
    SHAMapNodeID const& id,
    uint256 const& hash,
    SHAMapSyncFilter* filter) const
{
    std::shared_ptr<SHAMapTreeNode> node = getCache (hash);
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
        node = checkFilter (hash, id, filter);

    return node;
}

std::shared_ptr<SHAMapTreeNode> SHAMap::fetchNodeNT (uint256 const& hash) const
{
    std::shared_ptr<SHAMapTreeNode> node = getCache (hash);

    if (!node && backed_)
        node = fetchNodeFromDB (hash);

    return node;
}

// Throw if the node is missing
std::shared_ptr<SHAMapTreeNode> SHAMap::fetchNode (uint256 const& hash) const
{
    std::shared_ptr<SHAMapTreeNode> node = fetchNodeNT (hash);

    if (!node)
        throw SHAMapMissingNode (type_, hash);

    return node;
}

SHAMapTreeNode* SHAMap::descendThrow (SHAMapTreeNode* parent, int branch) const
{
    SHAMapTreeNode* ret = descend (parent, branch);

    if (! ret && ! parent->isEmptyBranch (branch))
        throw SHAMapMissingNode (type_, parent->getChildHash (branch));

    return ret;
}

std::shared_ptr<SHAMapTreeNode>
SHAMap::descendThrow (std::shared_ptr<SHAMapTreeNode> const& parent, int branch) const
{
    std::shared_ptr<SHAMapTreeNode> ret = descend (parent, branch);

    if (! ret && ! parent->isEmptyBranch (branch))
        throw SHAMapMissingNode (type_, parent->getChildHash (branch));

    return ret;
}

SHAMapTreeNode* SHAMap::descend (SHAMapTreeNode* parent, int branch) const
{
    SHAMapTreeNode* ret = parent->getChildPointer (branch);
    if (ret || !backed_)
        return ret;

    std::shared_ptr<SHAMapTreeNode> node = fetchNodeNT (parent->getChildHash (branch));
    if (!node)
        return nullptr;

    parent->canonicalizeChild (branch, node);
    return node.get ();
}

std::shared_ptr<SHAMapTreeNode>
SHAMap::descend (std::shared_ptr<SHAMapTreeNode> const& parent, int branch) const
{
    std::shared_ptr<SHAMapTreeNode> node = parent->getChild (branch);
    if (node || !backed_)
        return node;

    node = fetchNode (parent->getChildHash (branch));
    if (!node)
        return nullptr;

    parent->canonicalizeChild (branch, node);
    return node;
}

// Gets the node that would be hooked to this branch,
// but doesn't hook it up.
std::shared_ptr<SHAMapTreeNode>
SHAMap::descendNoStore (std::shared_ptr<SHAMapTreeNode> const& parent, int branch) const
{
    std::shared_ptr<SHAMapTreeNode> ret = parent->getChild (branch);
    if (!ret && backed_)
        ret = fetchNode (parent->getChildHash (branch));
    return ret;
}

std::pair <SHAMapTreeNode*, SHAMapNodeID>
SHAMap::descend (SHAMapTreeNode * parent, SHAMapNodeID const& parentID,
    int branch, SHAMapSyncFilter * filter) const
{
    assert (parent->isInner ());
    assert ((branch >= 0) && (branch < 16));
    assert (!parent->isEmptyBranch (branch));

    SHAMapNodeID childID = parentID.getChildNodeID (branch);
    SHAMapTreeNode* child = parent->getChildPointer (branch);
    uint256 const& childHash = parent->getChildHash (branch);

    if (!child)
    {
        std::shared_ptr<SHAMapTreeNode> childNode = fetchNodeNT (childID, childHash, filter);

        if (childNode)
        {
            parent->canonicalizeChild (branch, childNode);
            child = childNode.get ();
        }
    }

    return std::make_pair (child, childID);
}

SHAMapTreeNode* SHAMap::descendAsync (SHAMapTreeNode* parent, int branch,
    SHAMapNodeID const& childID, SHAMapSyncFilter * filter, bool & pending) const
{
    pending = false;

    SHAMapTreeNode* ret = parent->getChildPointer (branch);
    if (ret)
        return ret;

    uint256 const& hash = parent->getChildHash (branch);

    std::shared_ptr<SHAMapTreeNode> ptr = getCache (hash);
    if (!ptr)
    {
        if (filter)
            ptr = checkFilter (hash, childID, filter);

        if (!ptr && backed_)
        {
            NodeObject::pointer obj;
            if (! f_.db().asyncFetch (hash, obj))
            {
                pending = true;
                return nullptr;
            }
            if (!obj)
                return nullptr;

            ptr = std::make_shared <SHAMapTreeNode> (obj->getData(), 0, snfPREFIX, hash, true);

            if (backed_)
                canonicalize (hash, ptr);
        }
    }

    if (ptr)
        parent->canonicalizeChild (branch, ptr);

    return ptr.get ();
}

void
SHAMap::unshareNode (std::shared_ptr<SHAMapTreeNode>& node, SHAMapNodeID const& nodeID)
{
    // make sure the node is suitable for the intended operation (copy on write)
    assert (node->isValid ());
    assert (node->getSeq () <= seq_);

    if (node->getSeq () != seq_)
    {
        // have a CoW
        assert (state_ != SHAMapState::Immutable);

        node = std::make_shared<SHAMapTreeNode> (*node, seq_); // here's to the new node, same as the old node
        assert (node->isValid ());

        if (nodeID.isRoot ())
            root_ = node;
    }
}

SHAMapTreeNode*
SHAMap::firstBelow (SHAMapTreeNode* node) const
{
    // Return the first item below this node
    do
    {
        assert(node != nullptr);

        if (node->hasItem ())
            return node;

        // Walk down the tree
        bool foundNode = false;
        for (int i = 0; i < 16; ++i)
        {
            if (!node->isEmptyBranch (i))
            {
                node = descendThrow (node, i);
                foundNode = true;
                break;
            }
        }
        if (!foundNode)
            return nullptr;
    }
    while (true);
}

SHAMapTreeNode*
SHAMap::lastBelow (SHAMapTreeNode* node) const
{
    do
    {
        if (node->hasItem ())
            return node;

        // Walk down the tree
        bool foundNode = false;
        for (int i = 15; i >= 0; --i)
        {
            if (!node->isEmptyBranch (i))
            {
                node = descendThrow (node, i);
                foundNode = true;
                break;
            }
        }
        if (!foundNode)
            return nullptr;
    }
    while (true);
}

std::shared_ptr<SHAMapItem>
SHAMap::onlyBelow (SHAMapTreeNode* node) const
{
    // If there is only one item below this node, return it

    while (!node->isLeaf ())
    {
        SHAMapTreeNode* nextNode = nullptr;
        for (int i = 0; i < 16; ++i)
        {
            if (!node->isEmptyBranch (i))
            {
                if (nextNode)
                    return std::shared_ptr<SHAMapItem> ();

                nextNode = descendThrow (node, i);
            }
        }

        if (!nextNode)
        {
            assert (false);
            return std::shared_ptr<SHAMapItem> ();
        }

        node = nextNode;
    }

    // An inner node must have at least one leaf
    // below it, unless it's the root_
    assert (node->hasItem () || (node == root_.get ()));

    return node->peekItem ();
}

static const std::shared_ptr<SHAMapItem> no_item;

std::shared_ptr<SHAMapItem> SHAMap::peekFirstItem () const
{
    SHAMapTreeNode* node = firstBelow (root_.get ());

    if (!node)
        return no_item;

    return node->peekItem ();
}

std::shared_ptr<SHAMapItem> SHAMap::peekFirstItem (SHAMapTreeNode::TNType& type) const
{
    SHAMapTreeNode* node = firstBelow (root_.get ());

    if (!node)
        return no_item;

    type = node->getType ();
    return node->peekItem ();
}

std::shared_ptr<SHAMapItem> SHAMap::peekLastItem () const
{
    SHAMapTreeNode* node = lastBelow (root_.get ());

    if (!node)
        return no_item;

    return node->peekItem ();
}

std::shared_ptr<SHAMapItem> SHAMap::peekNextItem (uint256 const& id) const
{
    SHAMapTreeNode::TNType type;
    return peekNextItem (id, type);
}

std::shared_ptr<SHAMapItem> SHAMap::peekNextItem (uint256 const& id, SHAMapTreeNode::TNType& type) const
{
    // Get a pointer to the next item in the tree after a given item - item need not be in tree

    auto stack = getStack (id, true);

    while (!stack.empty ())
    {
        SHAMapTreeNode* node = stack.top().first.get();
        SHAMapNodeID nodeID = stack.top().second;
        stack.pop ();

        if (node->isLeaf ())
        {
            if (node->peekItem ()->getTag () > id)
            {
                type = node->getType ();
                return node->peekItem ();
            }
        }
        else
        {
            // breadth-first
            for (int i = nodeID.selectBranch (id) + 1; i < 16; ++i)
                if (!node->isEmptyBranch (i))
                {
                    node = descendThrow (node, i);
                    node = firstBelow (node);

                    if (!node || node->isInner ())
                        throw (std::runtime_error ("missing/corrupt node"));

                    type = node->getType ();
                    return node->peekItem ();
                }
        }
    }

    // must be last item
    return no_item;
}

// Get a pointer to the previous item in the tree after a given item - item need not be in tree
std::shared_ptr<SHAMapItem> SHAMap::peekPrevItem (uint256 const& id) const
{
    auto stack = getStack (id, true);

    while (!stack.empty ())
    {
        SHAMapTreeNode* node = stack.top ().first.get();
        SHAMapNodeID nodeID = stack.top ().second;
        stack.pop ();

        if (node->isLeaf ())
        {
            if (node->peekItem ()->getTag () < id)
                return node->peekItem ();
        }
        else
        {
            for (int i = nodeID.selectBranch (id) - 1; i >= 0; --i)
            {
                if (!node->isEmptyBranch (i))
                {
                    node = descendThrow (node, i);
                    node = lastBelow (node);
                    return node->peekItem ();
                }
            }
        }
    }

    // must be first item
    return no_item;
}

std::shared_ptr<SHAMapItem> SHAMap::peekItem (uint256 const& id) const
{
    SHAMapTreeNode* leaf = walkToPointer (id);

    if (!leaf)
        return no_item;

    return leaf->peekItem ();
}

std::shared_ptr<SHAMapItem> SHAMap::peekItem (uint256 const& id, SHAMapTreeNode::TNType& type) const
{
    SHAMapTreeNode* leaf = walkToPointer (id);

    if (!leaf)
        return no_item;

    type = leaf->getType ();
    return leaf->peekItem ();
}

std::shared_ptr<SHAMapItem> SHAMap::peekItem (uint256 const& id, uint256& hash) const
{
    SHAMapTreeNode* leaf = walkToPointer (id);

    if (!leaf)
        return no_item;

    hash = leaf->getNodeHash ();
    return leaf->peekItem ();
}


bool SHAMap::hasItem (uint256 const& id) const
{
    // does the tree have an item with this ID
    SHAMapTreeNode* leaf = walkToPointer (id);
    return (leaf != nullptr);
}

bool SHAMap::delItem (uint256 const& id)
{
    // delete the item with this ID
    assert (state_ != SHAMapState::Immutable);

    auto stack = getStack (id, true);

    if (stack.empty ())
        throw (std::runtime_error ("missing node"));

    std::shared_ptr<SHAMapTreeNode> leaf = stack.top ().first;
    stack.pop ();

    if (!leaf || !leaf->hasItem () || (leaf->peekItem ()->getTag () != id))
        return false;

    SHAMapTreeNode::TNType type = leaf->getType ();

    // What gets attached to the end of the chain
    // (For now, nothing, since we deleted the leaf)
    uint256 prevHash;
    std::shared_ptr<SHAMapTreeNode> prevNode;

    while (!stack.empty ())
    {
        std::shared_ptr<SHAMapTreeNode> node = stack.top ().first;
        SHAMapNodeID nodeID = stack.top ().second;
        stack.pop ();

        assert (node->isInner ());

        unshareNode (node, nodeID);
        node->setChild (nodeID.selectBranch (id), prevNode);

        if (!nodeID.isRoot ())
        {
            // we may have made this a node with 1 or 0 children
            // And, if so, we need to remove this branch
            int bc = node->getBranchCount ();

            if (bc == 0)
            {
                // no children below this branch
                prevHash = uint256 ();
                prevNode.reset ();
            }
            else if (bc == 1)
            {
                // If there's only one item, pull up on the thread
                std::shared_ptr<SHAMapItem> item = onlyBelow (node.get ());

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
                    node->setItem (item, type);
                }

                prevHash = node->getNodeHash ();
                prevNode = std::move (node);
            }
            else
            {
                // This node is now the end of the branch
                prevHash = node->getNodeHash ();
                prevNode = std::move (node);
            }
        }
    }

    return true;
}

bool
SHAMap::addGiveItem (std::shared_ptr<SHAMapItem> const& item,
                     bool isTransaction, bool hasMeta)
{
    // add the specified item, does not update
    uint256 tag = item->getTag ();
    SHAMapTreeNode::TNType type = !isTransaction ? SHAMapTreeNode::tnACCOUNT_STATE :
        (hasMeta ? SHAMapTreeNode::tnTRANSACTION_MD : SHAMapTreeNode::tnTRANSACTION_NM);

    assert (state_ != SHAMapState::Immutable);

    auto stack = getStack (tag, true);

    if (stack.empty ())
        throw (std::runtime_error ("missing node"));

    std::shared_ptr<SHAMapTreeNode> node = stack.top ().first;
    SHAMapNodeID nodeID = stack.top ().second;
    stack.pop ();

    if (node->isLeaf () && (node->peekItem ()->getTag () == tag))
        return false;

    unshareNode (node, nodeID);
    if (node->isInner ())
    {
        // easy case, we end on an inner node
        int branch = nodeID.selectBranch (tag);
        assert (node->isEmptyBranch (branch));
        auto newNode = std::make_shared<SHAMapTreeNode> (item, type, seq_);
        node->setChild (branch, newNode);
    }
    else
    {
        // this is a leaf node that has to be made an inner node holding two items
        std::shared_ptr<SHAMapItem> otherItem = node->peekItem ();
        assert (otherItem && (tag != otherItem->getTag ()));

        node->makeInner ();

        int b1, b2;

        while ((b1 = nodeID.selectBranch (tag)) ==
               (b2 = nodeID.selectBranch (otherItem->getTag ())))
        {
            stack.push ({node, nodeID});

            // we need a new inner node, since both go on same branch at this level
            nodeID = nodeID.getChildNodeID (b1);
            node = std::make_shared<SHAMapTreeNode> (seq_);
            node->makeInner ();
        }

        // we can add the two leaf nodes here
        assert (node->isInner ());

        std::shared_ptr<SHAMapTreeNode> newNode =
            std::make_shared<SHAMapTreeNode> (item, type, seq_);
        assert (newNode->isValid () && newNode->isLeaf ());
        node->setChild (b1, newNode);

        newNode = std::make_shared<SHAMapTreeNode> (otherItem, type, seq_);
        assert (newNode->isValid () && newNode->isLeaf ());
        node->setChild (b2, newNode);
    }

    dirtyUp (stack, tag, node);
    return true;
}

bool SHAMap::addItem (const SHAMapItem& i, bool isTransaction, bool hasMetaData)
{
    return addGiveItem (std::make_shared<SHAMapItem> (i), isTransaction, hasMetaData);
}

uint256
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
SHAMap::updateGiveItem (std::shared_ptr<SHAMapItem> const& item,
                        bool isTransaction, bool hasMeta)
{
    // can't change the tag but can change the hash
    uint256 tag = item->getTag ();

    assert (state_ != SHAMapState::Immutable);

    auto stack = getStack (tag, true);

    if (stack.empty ())
        throw (std::runtime_error ("missing node"));

    std::shared_ptr<SHAMapTreeNode> node = stack.top ().first;
    SHAMapNodeID nodeID = stack.top ().second;
    stack.pop ();

    if (!node->isLeaf () || (node->peekItem ()->getTag () != tag))
    {
        assert (false);
        return false;
    }

    unshareNode (node, nodeID);

    if (!node->setItem (item, !isTransaction ? SHAMapTreeNode::tnACCOUNT_STATE :
                        (hasMeta ? SHAMapTreeNode::tnTRANSACTION_MD : SHAMapTreeNode::tnTRANSACTION_NM)))
    {
        journal_.trace <<
            "SHAMap setItem, no change";
        return true;
    }

    dirtyUp (stack, tag, node);
    return true;
}

bool SHAMap::fetchRoot (uint256 const& hash, SHAMapSyncFilter* filter)
{
    if (hash == root_->getNodeHash ())
        return true;

    if (journal_.trace)
    {
        if (type_ == SHAMapType::TRANSACTION)
        {
            journal_.trace
                << "Fetch root TXN node " << hash;
        }
        else if (type_ == SHAMapType::STATE)
        {
            journal_.trace <<
                "Fetch root STATE node " << hash;
        }
        else
        {
            journal_.trace <<
                "Fetch root SHAMap node " << hash;
        }
    }

    std::shared_ptr<SHAMapTreeNode> newRoot = fetchNodeNT (SHAMapNodeID(), hash, filter);

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
void SHAMap::writeNode (
    NodeObjectType t, std::uint32_t seq, std::shared_ptr<SHAMapTreeNode>& node) const
{
    // Node is ours, so we can just make it shareable
    assert (node->getSeq() == seq_);
    assert (backed_);
    node->setSeq (0);

    canonicalize (node->getNodeHash(), node);

    Serializer s;
    node->addRaw (s, snfPREFIX);
    f_.db().store (t,
        std::move (s.modData ()), node->getNodeHash ());
}

// We can't modify an inner node someone else might have a
// pointer to because flushing modifies inner nodes -- it
// makes them point to canonical/shared nodes.
void SHAMap::preFlushNode (std::shared_ptr<SHAMapTreeNode>& node) const
{
    // A shared node should never need to be flushed
    // because that would imply someone modified it
    assert (node->getSeq() != 0);

    if (node->getSeq() != seq_)
    {
        // Node is not uniquely ours, so unshare it before
        // possibly modifying it
        node = std::make_shared <SHAMapTreeNode> (*node, seq_);
    }
}

int SHAMap::unshare ()
{
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

    if (!root_ || (root_->getSeq() == 0) || root_->isEmpty ())
        return flushed;

    if (root_->isLeaf())
    { // special case -- root_ is leaf
        preFlushNode (root_);
        if (doWrite && backed_)
            writeNode (t, seq, root_);
        return 1;
    }

    // Stack of {parent,index,child} pointers representing
    // inner nodes we are in the process of flushing
    using StackEntry = std::pair <std::shared_ptr<SHAMapTreeNode>, int>;
    std::stack <StackEntry, std::vector<StackEntry>> stack;

    std::shared_ptr<SHAMapTreeNode> node = root_;
    preFlushNode (node);

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
                std::shared_ptr<SHAMapTreeNode> child = node->getChild (pos++);

                if (child && (child->getSeq() != 0))
                {
                    // This is a node that needs to be flushed

                    if (child->isInner ())
                    {
                        // save our place and work on this node
                        preFlushNode (child);

                        stack.emplace (std::move (node), branch);

                        node = std::move (child);
                        pos = 0;
                    }
                    else
                    {
                        // flush this leaf
                        ++flushed;

                        preFlushNode (child);

                        assert (node->getSeq() == seq_);
                        child->updateHash();

                        if (doWrite && backed_)
                            writeNode (t, seq, child);

                        node->shareChild (branch, child);
                    }
                }
            }
        }

        // update the hash of this inner node
        node->updateHashDeep();

        // This inner node can now be shared
        if (doWrite && backed_)
            writeNode (t, seq, node);

        ++flushed;

        if (stack.empty ())
           break;

        std::shared_ptr<SHAMapTreeNode> parent = std::move (stack.top().first);
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
    if (journal_.info) journal_.info <<
        " MAP Contains";

    std::stack <std::pair <SHAMapTreeNode*, SHAMapNodeID> > stack;
    stack.push ({root_.get (), SHAMapNodeID ()});

    do
    {
        SHAMapTreeNode* node = stack.top().first;
        SHAMapNodeID nodeID = stack.top().second;
        stack.pop();

        if (journal_.info) journal_.info <<
            node->getString (nodeID);
        if (hash)
           if (journal_.info) journal_.info <<
               "Hash: " << node->getNodeHash();

        if (node->isInner ())
        {
            for (int i = 0; i < 16; ++i)
            {
                if (!node->isEmptyBranch (i))
                {
                    SHAMapTreeNode* child = node->getChildPointer (i);
                    if (child)
                    {
                        assert (child->getNodeHash() == node->getChildHash (i));
                        stack.push ({child, nodeID.getChildNodeID (i)});
                     }
                }
            }
        }
        else
            ++leafCount;
    }
    while (!stack.empty ());

    if (journal_.info) journal_.info <<
        leafCount << " resident leaves";
}

std::shared_ptr<SHAMapTreeNode> SHAMap::getCache (uint256 const& hash) const
{
    std::shared_ptr<SHAMapTreeNode> ret = f_.treecache().fetch (hash);
    assert (!ret || !ret->getSeq());
    return ret;
}

void SHAMap::canonicalize (uint256 const& hash, std::shared_ptr<SHAMapTreeNode>& node) const
{
    assert (backed_);
    assert (node->getSeq() == 0);
    assert (node->getNodeHash() == hash);

    f_.treecache().canonicalize (hash, node);
}

} // ripple
