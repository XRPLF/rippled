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

#include <ripple/nodestore/Database.h>
#include <beast/unit_test/suite.h>
#include <beast/chrono/manual_clock.h>

namespace ripple {

void SHAMap::DefaultMissingNodeHandler::operator() (std::uint32_t refNUm)
{
    getApp().getOPs ().missingNodeInLedger (refNUm);
};

//------------------------------------------------------------------------------

SHAMap::SHAMap (
    SHAMapType t,
    FullBelowCache& fullBelowCache,
    TreeNodeCache& treeNodeCache,
    std::uint32_t seq,
    MissingNodeHandler missing_node_handler)
    : m_fullBelowCache (fullBelowCache)
    , mSeq (seq)
    , mLedgerSeq (0)
    , mTreeNodeCache (treeNodeCache)
    , mState (smsModifying)
    , mType (t)
    , mBacked (true)
    , m_missing_node_handler (missing_node_handler)
{
    assert (mSeq != 0);

    root = std::make_shared<SHAMapTreeNode> (mSeq);
    root->makeInner ();
}

SHAMap::SHAMap (
    SHAMapType t,
    uint256 const& hash,
    FullBelowCache& fullBelowCache,
    TreeNodeCache& treeNodeCache,
    MissingNodeHandler missing_node_handler)
    : m_fullBelowCache (fullBelowCache)
    , mSeq (1)
    , mLedgerSeq (0)
    , mTreeNodeCache (treeNodeCache)
    , mState (smsSynching)
    , mType (t)
    , mBacked (true)
    , m_missing_node_handler (missing_node_handler)
{
    root = std::make_shared<SHAMapTreeNode> (mSeq);
    root->makeInner ();
}

SHAMap::~SHAMap ()
{
    mState = smsInvalid;

    if (root)
    {
        logTimedDestroy <SHAMap> (root,
            beast::String ("root node"));
    }
}

void SHAMapNodeID::setMHash () const
{
    using namespace std;

    std::size_t h = HashMaps::getInstance ().getNonce <std::size_t> ()
                    + (mDepth * HashMaps::goldenRatio);

    const unsigned int* ptr = reinterpret_cast <const unsigned int*> (mNodeID.begin ());

    for (int i = (mDepth + 7) / 8; i != 0; --i)
        h = (h * HashMaps::goldenRatio) ^ *ptr++;

    mHash = h;
}

SHAMap::pointer SHAMap::snapShot (bool isMutable)
{
    SHAMap::pointer ret = std::make_shared<SHAMap> (mType,
        m_fullBelowCache, mTreeNodeCache);
    SHAMap& newMap = *ret;

    if (!isMutable)
        newMap.mState = smsImmutable;

    newMap.mSeq = mSeq + 1;
    newMap.root = root;

    if ((mState != smsImmutable) || !isMutable)
    {
        // If either map may change, they cannot share nodes
        newMap.flushDirty (false, hotUNKNOWN, 0);
    }

    return ret;
}

SHAMap::SharedPtrNodeStack
SHAMap::getStack (uint256 const& id, bool include_nonmatching_leaf)
{
    // Walk the tree as far as possible to the specified identifier
    // produce a stack of nodes along the way, with the terminal node at the top
    SharedPtrNodeStack stack;

    SHAMapTreeNode::pointer node = root;
    SHAMapNodeID nodeID;

    while (!node->isLeaf ())
    {
        stack.push ({node, nodeID});

        int branch = nodeID.selectBranch (id);
        assert (branch >= 0);

        if (node->isEmptyBranch (branch))
            return stack;

        node = descendThrow (node, nodeID, branch);
    }

    if (include_nonmatching_leaf || (node->peekItem ()->getTag () == id))
        stack.push ({node, nodeID});

    return stack;
}

void
SHAMap::dirtyUp (SharedPtrNodeStack& stack,
                 uint256 const& target, SHAMapTreeNode::pointer child)
{
    // walk the tree up from through the inner nodes to the root
    // update hashes and links
    // stack is a path of inner nodes up to, but not including, child
    // child can be an inner node or a leaf

    assert ((mState != smsSynching) && (mState != smsImmutable));
    assert (child && (child->getSeq() == mSeq));

    while (!stack.empty ())
    {
        SHAMapTreeNode::pointer node = stack.top ().first;
        SHAMapNodeID nodeID = stack.top ().second;
        stack.pop ();
        assert (node->isInnerNode ());

        int branch = nodeID.selectBranch (target);
        assert (branch >= 0);

        unshareNode (node, nodeID);

        if (! node->setChild (branch, child->getNodeHash(), child))
        {
            WriteLog (lsFATAL, SHAMap) << "dirtyUp terminates early";
            assert (false);
            return;
        }

#ifdef ST_DEBUG
        WriteLog (lsTRACE, SHAMap) << "dirtyUp sets branch " << branch << " to " << prevHash;
#endif
        child = std::move (node);
    }
}

SHAMapTreeNode* SHAMap::walkToPointer (uint256 const& id)
{
    SHAMapTreeNode* inNode = root.get ();
    SHAMapNodeID nodeID;
    uint256 nodeHash;

    while (inNode->isInner ())
    {
        int branch = nodeID.selectBranch (id);

        if (inNode->isEmptyBranch (branch))
            return nullptr;

        inNode = descendThrow (inNode, nodeID, branch);
        assert (inNode);
    }

    return (inNode->getTag () == id) ? inNode : nullptr;
}

SHAMapTreeNode::pointer SHAMap::fetchNodeFromDB (uint256 const& hash)
{
    SHAMapTreeNode::pointer node;

    if (mBacked && getApp().running ())
    {
        NodeObject::pointer obj = getApp().getNodeStore().fetch (hash);
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
                WriteLog (lsWARNING, SHAMap) << "Invalid DB node " << hash;
                return SHAMapTreeNode::pointer ();
            }
        }
        else if (mLedgerSeq != 0)
        {
            m_missing_node_handler (mLedgerSeq);
            mLedgerSeq = 0;
        }
    }

    return node;
}

// See if a sync filter has a node
SHAMapTreeNode::pointer SHAMap::checkFilter (
    uint256 const& hash,
    SHAMapNodeID const& id,
    SHAMapSyncFilter* filter)
{
    SHAMapTreeNode::pointer node;
    Blob nodeData;

    if (filter->haveNode (id, hash, nodeData))
    {
        node = std::make_shared <SHAMapTreeNode> (
            nodeData, 0, snfPREFIX, hash, true);

       filter->gotNode (true, id, hash, nodeData, node->getType ());

       if (mBacked)
           canonicalize (hash, node);
    }

    return node;
}

// Get a node without throwing
// Used on maps where missing nodes are expected
SHAMapTreeNode::pointer SHAMap::fetchNodeNT(
    SHAMapNodeID const& id,
    uint256 const& hash,
    SHAMapSyncFilter* filter)
{
    SHAMapTreeNode::pointer node = getCache (hash);
    if (node)
        return node;

    if (mBacked)
    {
        node = fetchNodeFromDB (hash);
        if (node)
        {
            canonicalize (hash, node);
            return node;
        }
    }

    if (!node && filter)
        node = checkFilter (hash, id, filter);

    return node;
}

// Throw if the node is missing
SHAMapTreeNode::pointer SHAMap::fetchNode (uint256 const& hash)
{
    SHAMapTreeNode::pointer node = getCache (hash);

    if (!node && mBacked)
        node = fetchNodeFromDB (hash);

    if (!node)
        throw SHAMapMissingNode (mType, hash);

    return node;
}

SHAMapTreeNode* SHAMap::descendThrow (SHAMapTreeNode* parent, int branch)
{
    SHAMapTreeNode* ret = descend (parent, branch);
    if (!ret)
    {
        assert (!parent->isEmptyBranch (branch));
        throw SHAMapMissingNode (mType, parent->getChildHash (branch));
    }
    return ret;
}

SHAMapTreeNode::pointer SHAMap::descendThrow (
    SHAMapTreeNode::pointer parent, SHAMapNodeID& nodeID, int branch)
{
    SHAMapTreeNode::pointer ret = descend (parent, nodeID, branch);

    if (!ret)
    {
        assert (!parent->isEmptyBranch (branch));
        throw SHAMapMissingNode (mType, parent->getChildHash (branch));
    }

    return ret;
}

SHAMapTreeNode* SHAMap::descendThrow (SHAMapTreeNode* parent, SHAMapNodeID& nodeID, int branch)
{
    nodeID = nodeID.getChildNodeID (branch);
    return descendThrow (parent, branch);
}

SHAMapTreeNode* SHAMap::descend (SHAMapTreeNode* parent, int branch)
{
    SHAMapTreeNode* ret = parent->getChildPointer (branch);
    if (ret || !mBacked)
        return ret;

    SHAMapTreeNode::pointer node = fetchNode (parent->getChildHash (branch));
    if (!node)
        return nullptr;

    parent->canonicalizeChild (branch, node);
    return node.get ();
}

SHAMapTreeNode::pointer SHAMap::descend (SHAMapTreeNode::ref parent, SHAMapNodeID& nodeID, int branch)
{
    nodeID = nodeID.getChildNodeID (branch);

    SHAMapTreeNode::pointer node = parent->getChild (branch);
    if (node || !mBacked)
        return node;

    node = fetchNode (parent->getChildHash (branch));
    if (node)
        parent->canonicalizeChild (branch, node);

    return node;
}

// Gets the node that would be hooked to this branch,
// but doesn't hook it up.
SHAMapTreeNode::pointer SHAMap::descendNoStore (SHAMapTreeNode::ref parent, int branch)
{
    SHAMapTreeNode::pointer ret = parent->getChild (branch);
    if (!ret && mBacked)
        ret = fetchNode (parent->getChildHash (branch));
    return ret;
}

std::pair <SHAMapTreeNode*, SHAMapNodeID>
SHAMap::descend (SHAMapTreeNode * parent, SHAMapNodeID const& parentID,
    int branch, SHAMapSyncFilter * filter)
{
    assert (parent->isInner ());
    assert ((branch >= 0) && (branch < 16));
    assert (!parent->isEmptyBranch (branch));

    SHAMapNodeID childID = parentID.getChildNodeID (branch);
    SHAMapTreeNode* child = parent->getChildPointer (branch);
    uint256 const& childHash = parent->getChildHash (branch);

    if (!child)
    {
        SHAMapTreeNode::pointer childNode = fetchNodeNT (childID, childHash, filter);

        if (childNode)
        {
            parent->canonicalizeChild (branch, childNode);
            child = childNode.get ();
        }
    }

    return std::make_pair (child, childID);
}

SHAMapTreeNode* SHAMap::descendAsync (SHAMapTreeNode* parent, int branch,
    SHAMapNodeID const& childID, SHAMapSyncFilter * filter, bool & pending)
{
    pending = false;

    SHAMapTreeNode* ret = parent->getChildPointer (branch);
    if (ret)
        return ret;

    uint256 const& hash = parent->getChildHash (branch);

    SHAMapTreeNode::pointer ptr = getCache (hash);
    if (!ptr)
    {
        if (filter)
            ptr = checkFilter (hash, childID, filter);

        if (!ptr && mBacked)
        {
            NodeObject::pointer obj;
            if (!getApp().getNodeStore().asyncFetch (hash, obj))
            {
                pending = true;
                return nullptr;
            }
            if (!obj)
                return nullptr;

            ptr = std::make_shared <SHAMapTreeNode> (obj->getData(), 0, snfPREFIX, hash, true);

            if (mBacked)
                canonicalize (hash, ptr);
        }
    }

    if (ptr)
        parent->canonicalizeChild (branch, ptr);

    return ptr.get ();
}

void
SHAMap::unshareNode (SHAMapTreeNode::pointer& node, SHAMapNodeID const& nodeID)
{
    // make sure the node is suitable for the intended operation (copy on write)
    assert (node->isValid ());
    assert (node->getSeq () <= mSeq);

    if (node->getSeq () != mSeq)
    {
        // have a CoW
        assert (mState != smsImmutable);

        node = std::make_shared<SHAMapTreeNode> (*node, mSeq); // here's to the new node, same as the old node
        assert (node->isValid ());

        if (nodeID.isRoot ())
            root = node;
    }
}

SHAMapTreeNode*
SHAMap::firstBelow (SHAMapTreeNode* node)
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
SHAMap::lastBelow (SHAMapTreeNode* node)
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

SHAMapItem::pointer
SHAMap::onlyBelow (SHAMapTreeNode* node)
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
                    return SHAMapItem::pointer ();

                nextNode = descendThrow (node, i);
            }
        }

        if (!nextNode)
        {
            assert (false);
            return SHAMapItem::pointer ();
        }

        node = nextNode;
    }

    // An inner node must have at least one leaf
    // below it, unless it's the root
    assert (node->hasItem () || (node == root.get ()));

    return node->peekItem ();
}

static const SHAMapItem::pointer no_item;

SHAMapItem::pointer SHAMap::peekFirstItem ()
{
    SHAMapTreeNode* node = firstBelow (root.get ());

    if (!node)
        return no_item;

    return node->peekItem ();
}

SHAMapItem::pointer SHAMap::peekFirstItem (SHAMapTreeNode::TNType& type)
{
    SHAMapTreeNode* node = firstBelow (root.get ());

    if (!node)
        return no_item;

    type = node->getType ();
    return node->peekItem ();
}

SHAMapItem::pointer SHAMap::peekLastItem ()
{
    SHAMapTreeNode* node = lastBelow (root.get ());

    if (!node)
        return no_item;

    return node->peekItem ();
}

SHAMapItem::pointer SHAMap::peekNextItem (uint256 const& id)
{
    SHAMapTreeNode::TNType type;
    return peekNextItem (id, type);
}

SHAMapItem::pointer SHAMap::peekNextItem (uint256 const& id, SHAMapTreeNode::TNType& type)
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
SHAMapItem::pointer SHAMap::peekPrevItem (uint256 const& id)
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

SHAMapItem::pointer SHAMap::peekItem (uint256 const& id)
{
    SHAMapTreeNode* leaf = walkToPointer (id);

    if (!leaf)
        return no_item;

    return leaf->peekItem ();
}

SHAMapItem::pointer SHAMap::peekItem (uint256 const& id, SHAMapTreeNode::TNType& type)
{
    SHAMapTreeNode* leaf = walkToPointer (id);

    if (!leaf)
        return no_item;

    type = leaf->getType ();
    return leaf->peekItem ();
}

SHAMapItem::pointer SHAMap::peekItem (uint256 const& id, uint256& hash)
{
    SHAMapTreeNode* leaf = walkToPointer (id);

    if (!leaf)
        return no_item;

    hash = leaf->getNodeHash ();
    return leaf->peekItem ();
}


bool SHAMap::hasItem (uint256 const& id)
{
    // does the tree have an item with this ID
    SHAMapTreeNode* leaf = walkToPointer (id);
    return (leaf != nullptr);
}

bool SHAMap::delItem (uint256 const& id)
{
    // delete the item with this ID
    assert (mState != smsImmutable);

    auto stack = getStack (id, true);

    if (stack.empty ())
        throw (std::runtime_error ("missing node"));

    SHAMapTreeNode::pointer leaf = stack.top ().first;
    SHAMapNodeID leafID = stack.top ().second;
    stack.pop ();

    if (!leaf || !leaf->hasItem () || (leaf->peekItem ()->getTag () != id))
        return false;

    SHAMapTreeNode::TNType type = leaf->getType ();

    // What gets attached to the end of the chain
    // (For now, nothing, since we deleted the leaf)
    uint256 prevHash;
    SHAMapTreeNode::pointer prevNode;

    while (!stack.empty ())
    {
        SHAMapTreeNode::pointer node = stack.top ().first;
        SHAMapNodeID nodeID = stack.top ().second;
        stack.pop ();

        assert (node->isInner ());

        unshareNode (node, nodeID);
        if (! node->setChild (nodeID.selectBranch (id), prevHash, prevNode))
        {
            assert (false);
            return true;
        }

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
                SHAMapItem::pointer item = onlyBelow (node.get ());

                if (item)
                {
                    for (int i = 0; i < 16; ++i)
                    {
                        if (!node->isEmptyBranch (i))
                        {
                            if (! node->setChild (i, uint256(), nullptr))
                            {
                                assert (false);
                            }
                            break;
                        }
                    }
                    node->setItem (item, type);
                }

                prevHash = node->getNodeHash ();
                prevNode = std::move (node);
                assert (prevHash.isNonZero ());
            }
            else
            {
                // This node is now the end of the branch
                prevHash = node->getNodeHash ();
                prevNode = std::move (node);
                assert (prevHash.isNonZero ());
            }
        }
    }

    return true;
}

bool SHAMap::addGiveItem (SHAMapItem::ref item, bool isTransaction, bool hasMeta)
{
    // add the specified item, does not update
    uint256 tag = item->getTag ();
    SHAMapTreeNode::TNType type = !isTransaction ? SHAMapTreeNode::tnACCOUNT_STATE :
        (hasMeta ? SHAMapTreeNode::tnTRANSACTION_MD : SHAMapTreeNode::tnTRANSACTION_NM);

    assert (mState != smsImmutable);

    auto stack = getStack (tag, true);

    if (stack.empty ())
        throw (std::runtime_error ("missing node"));

    SHAMapTreeNode::pointer node = stack.top ().first;
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
        SHAMapTreeNode::pointer newNode =
            std::make_shared<SHAMapTreeNode> (item, type, mSeq);
        if (! node->setChild (branch, newNode->getNodeHash (), newNode))
        {
            assert (false);
        }
    }
    else
    {
        // this is a leaf node that has to be made an inner node holding two items
        SHAMapItem::pointer otherItem = node->peekItem ();
        assert (otherItem && (tag != otherItem->getTag ()));

        node->makeInner ();

        int b1, b2;

        while ((b1 = nodeID.selectBranch (tag)) ==
               (b2 = nodeID.selectBranch (otherItem->getTag ())))
        {
            stack.push ({node, nodeID});

            // we need a new inner node, since both go on same branch at this level
            nodeID = nodeID.getChildNodeID (b1);
            node = std::make_shared<SHAMapTreeNode> (mSeq);
            node->makeInner ();
        }

        // we can add the two leaf nodes here
        assert (node->isInner ());

        SHAMapTreeNode::pointer newNode =
            std::make_shared<SHAMapTreeNode> (item, type, mSeq);
        assert (newNode->isValid () && newNode->isLeaf ());
        if (!node->setChild (b1, newNode->getNodeHash (), newNode))
        {
            assert (false);
        }

        newNode = std::make_shared<SHAMapTreeNode> (otherItem, type, mSeq);
        assert (newNode->isValid () && newNode->isLeaf ());
        if (!node->setChild (b2, newNode->getNodeHash (), newNode))
        {
            assert (false);
        }
    }

    dirtyUp (stack, tag, node);
    return true;
}

bool SHAMap::addItem (const SHAMapItem& i, bool isTransaction, bool hasMetaData)
{
    return addGiveItem (std::make_shared<SHAMapItem> (i), isTransaction, hasMetaData);
}

bool SHAMap::updateGiveItem (SHAMapItem::ref item, bool isTransaction, bool hasMeta)
{
    // can't change the tag but can change the hash
    uint256 tag = item->getTag ();

    assert (mState != smsImmutable);

    auto stack = getStack (tag, true);

    if (stack.empty ())
        throw (std::runtime_error ("missing node"));

    SHAMapTreeNode::pointer node = stack.top ().first;
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
        WriteLog (lsWARNING, SHAMap) << "SHAMap setItem, no change";
        return true;
    }

    dirtyUp (stack, tag, node);
    return true;
}

void SHAMapItem::dump ()
{
    WriteLog (lsINFO, SHAMap) << "SHAMapItem(" << mTag << ") " << mData.size () << "bytes";
}

bool SHAMap::fetchRoot (uint256 const& hash, SHAMapSyncFilter* filter)
{
    if (hash == root->getNodeHash ())
        return true;

    if (ShouldLog (lsTRACE, SHAMap))
    {
        if (mType == smtTRANSACTION)
            WriteLog (lsTRACE, SHAMap) << "Fetch root TXN node " << hash;
        else if (mType == smtSTATE)
            WriteLog (lsTRACE, SHAMap) << "Fetch root STATE node " << hash;
        else
            WriteLog (lsTRACE, SHAMap) << "Fetch root SHAMap node " << hash;
    }

    SHAMapTreeNode::pointer newRoot = fetchNodeNT (SHAMapNodeID(), hash, filter);

    if (newRoot)
    {
        root = newRoot;
    }

    assert (root->getNodeHash () == hash);
    return true;
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
void SHAMap::flushNode (bool doWrite,
    NodeObjectType t, std::uint32_t seq, SHAMapTreeNode::pointer& node)
{
    // Node is ours, so we can just make it shareable
    assert (node->getSeq() == mSeq);
    node->setSeq (0);

    if (mBacked)
    {
        canonicalize (node->getNodeHash(), node);

         if (doWrite)
         {
             Serializer s;
             node->addRaw (s, snfPREFIX);
             getApp().getNodeStore().store (t, seq,
                 std::move (s.modData ()), node->getNodeHash ());
         }
    }
}

// We can't modify am inner node someone else might have a
// pointer to because flushing modifies inner nodes -- it
// makes them point to canonical/shared nodes.
void SHAMap::preFlushNode (SHAMapTreeNode::pointer& node)
{
    // A shared node should never need to be flushed
    // because that would imply someone modified it
    assert (node->getSeq() != 0);

    if (node->getSeq() != mSeq)
    {
        // Node is not uniquely ours, so unshare it before
        // possibly modifying it
        node = std::make_shared <SHAMapTreeNode> (*node, mSeq);
    }
}

/** Convert all modified nodes to shared nodes */
// If requested, write them to the node store
int SHAMap::flushDirty (bool doWrite, NodeObjectType t, std::uint32_t seq)
{
    int flushed = 0;
    Serializer s;

    if (!root || (root->getSeq() == 0) || root->isEmpty ())
        return flushed;

    if (root->isLeaf())
    { // special case -- root is leaf
        preFlushNode (root);
        flushNode (doWrite, t, seq, root);
        return 1;
    }

    // Stack of {parent,index,child} pointers representing
    // inner nodes we are in the process of flushing
    std::stack <std::pair <SHAMapTreeNode::pointer, int> > stack;

    SHAMapTreeNode::pointer node = root;
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
                SHAMapTreeNode::pointer child = node->getChild (pos++);

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
                        flushNode (doWrite, t, seq, child);

                        assert (node->getSeq() == mSeq);
                        node->shareChild (branch, child);
                    }
                }
            }
        }

        // This inner node can now be shared
        flushNode (doWrite, t, seq, node);
        ++flushed;

        if (stack.empty ())
           break;

        SHAMapTreeNode::pointer parent = std::move (stack.top().first);
        pos = stack.top().second;
        stack.pop();

        // Hook this inner node to its parent
        assert (parent->getSeq() == mSeq);
        parent->shareChild (pos, node);

        // Continue with parent's next child, if any
        node = std::move (parent);
        ++pos;
    }

    // Last inner node is the new root
    root = std::move (node);

    return flushed;
}

bool SHAMap::getPath (uint256 const& index, std::vector< Blob >& nodes, SHANodeFormat format)
{
    // Return the path of nodes to the specified index in the specified format
    // Return value: true = node present, false = node not present

    SHAMapTreeNode* inNode = root.get ();
    SHAMapNodeID nodeID;

    while (inNode->isInner ())
    {
        Serializer s;
        inNode->addRaw (s, format);
        nodes.push_back (s.peekData ());

        int branch = nodeID.selectBranch (index);
        if (inNode->isEmptyBranch (branch))
            return false;

        inNode = descendThrow (inNode, nodeID, branch);
        assert (inNode);
    }

    if (inNode->getTag () != index) // path leads to different leaf
        return false;

    // path leads to the requested leaf
    Serializer s;
    inNode->addRaw (s, format);
    nodes.push_back (std::move(s.peekData ()));
    return true;
}

void SHAMap::dump (bool hash)
{
    int leafCount = 0;
    WriteLog (lsINFO, SHAMap) << " MAP Contains";

    std::stack <std::pair <SHAMapTreeNode*, SHAMapNodeID> > stack;
    stack.push ({root.get (), SHAMapNodeID ()});

    do
    {
        SHAMapTreeNode* node = stack.top().first;
        SHAMapNodeID nodeID = stack.top().second;
        stack.pop();

        WriteLog (lsINFO, SHAMap) << node->getString (nodeID);
        if (hash)
        {
           WriteLog (lsINFO, SHAMap) << "Hash: " << node->getNodeHash();
        }

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

    WriteLog (lsINFO, SHAMap) << leafCount << " resident leaves";
}

SHAMapTreeNode::pointer SHAMap::getCache (uint256 const& hash)
{
    SHAMapTreeNode::pointer ret = mTreeNodeCache.fetch (hash);
    assert (!ret || !ret->getSeq());
    return ret;
}

void SHAMap::canonicalize (uint256 const& hash, SHAMapTreeNode::pointer& node)
{
    assert (mBacked);
    assert (node->getSeq() == 0);

    mTreeNodeCache.canonicalize (hash, node);

}

//------------------------------------------------------------------------------

class SHAMap_test : public beast::unit_test::suite
{
public:
    // VFALCO TODO Rename this to createFilledVector and pass an unsigned char, tidy up
    //
    static Blob IntToVUC (int v)
    {
        Blob vuc;

        for (int i = 0; i < 32; ++i)
            vuc.push_back (static_cast<unsigned char> (v));

        return vuc;
    }

    void run ()
    {
        testcase ("add/traverse");

        beast::manual_clock <std::chrono::seconds> clock;  // manual advance clock
        beast::Journal const j;                            // debug journal

        FullBelowCache fullBelowCache ("test.full_below", clock);
        TreeNodeCache treeNodeCache ("test.tree_node_cache", 65536, 60, clock, j);

        // h3 and h4 differ only in the leaf, same terminal node (level 19)
        uint256 h1, h2, h3, h4, h5;
        h1.SetHex ("092891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");
        h2.SetHex ("436ccbac3347baa1f1e53baeef1f43334da88f1f6d70d963b833afd6dfa289fe");
        h3.SetHex ("b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
        h4.SetHex ("b92891fe4ef6cee585fdc6fda2e09eb4d386363158ec3321b8123e5a772c6ca8");
        h5.SetHex ("a92891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");

        SHAMap sMap (smtFREE, fullBelowCache, treeNodeCache);
        SHAMapItem i1 (h1, IntToVUC (1)), i2 (h2, IntToVUC (2)), i3 (h3, IntToVUC (3)), i4 (h4, IntToVUC (4)), i5 (h5, IntToVUC (5));

        unexpected (!sMap.addItem (i2, true, false), "no add");

        unexpected (!sMap.addItem (i1, true, false), "no add");

        SHAMapItem::pointer i;

        i = sMap.peekFirstItem ();

        unexpected (!i || (*i != i1), "bad traverse");

        i = sMap.peekNextItem (i->getTag ());

        unexpected (!i || (*i != i2), "bad traverse");

        i = sMap.peekNextItem (i->getTag ());

        unexpected (i, "bad traverse");

        sMap.addItem (i4, true, false);

        sMap.delItem (i2.getTag ());

        sMap.addItem (i3, true, false);

        i = sMap.peekFirstItem ();

        unexpected (!i || (*i != i1), "bad traverse");

        i = sMap.peekNextItem (i->getTag ());

        unexpected (!i || (*i != i3), "bad traverse");

        i = sMap.peekNextItem (i->getTag ());

        unexpected (!i || (*i != i4), "bad traverse");

        i = sMap.peekNextItem (i->getTag ());

        unexpected (i, "bad traverse");



        testcase ("snapshot");

        uint256 mapHash = sMap.getHash ();
        SHAMap::pointer map2 = sMap.snapShot (false);

        unexpected (sMap.getHash () != mapHash, "bad snapshot");

        unexpected (map2->getHash () != mapHash, "bad snapshot");

        unexpected (!sMap.delItem (sMap.peekFirstItem ()->getTag ()), "bad mod");

        unexpected (sMap.getHash () == mapHash, "bad snapshot");

        unexpected (map2->getHash () != mapHash, "bad snapshot");
    }
};

BEAST_DEFINE_TESTSUITE(SHAMap,ripple_app,ripple);

} // ripple
