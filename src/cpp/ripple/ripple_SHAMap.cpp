//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef STATE_MAP_BUCKETS
#define STATE_MAP_BUCKETS 1024
#endif

SETUP_LOG (SHAMap)

void SHAMapNode::setMHash () const
{
    using namespace std;

    std::size_t h = HashMaps::getInstance ().getNonce <std::size_t> ()
                    + (mDepth * HashMaps::goldenRatio);

    const unsigned int* ptr = reinterpret_cast <const unsigned int*> (mNodeID.begin ());

    for (int i = (mDepth + 7) / 8; i != 0; --i)
        h = (h * HashMaps::goldenRatio) ^ *ptr++;

    mHash = h;
}

std::size_t hash_value (const SHAMapNode& mn)
{
    return mn.getMHash ();
}


SHAMap::SHAMap (SHAMapType t, uint32 seq) : mSeq (seq), mLedgerSeq (0), mState (smsModifying), mType (t)
{
    if (t == smtSTATE)
        mTNByID.rehash (STATE_MAP_BUCKETS);

    root = boost::make_shared<SHAMapTreeNode> (mSeq, SHAMapNode (0, uint256 ()));
    root->makeInner ();
    mTNByID[*root] = root;
}

SHAMap::SHAMap (SHAMapType t, uint256 const& hash) : mSeq (1), mLedgerSeq (0), mState (smsSynching), mType (t)
{
    // FIXME: Need to acquire root node
    if (t == smtSTATE)
        mTNByID.rehash (STATE_MAP_BUCKETS);

    root = boost::make_shared<SHAMapTreeNode> (mSeq, SHAMapNode (0, uint256 ()));
    root->makeInner ();
    mTNByID[*root] = root;
}

SHAMap::pointer SHAMap::snapShot (bool isMutable)
{
    // Return a new SHAMap that is an immutable snapshot of this one
    // Initially nodes are shared, but CoW is forced on both ledgers
    boost::recursive_mutex::scoped_lock sl (mLock);
    SHAMap::pointer ret = boost::make_shared<SHAMap> (mType);
    SHAMap& newMap = *ret;
    newMap.mSeq = ++mSeq;
    newMap.mTNByID = mTNByID;
    newMap.root = root;

    if (!isMutable)
        newMap.mState = smsImmutable;

    return ret;
}

std::stack<SHAMapTreeNode::pointer> SHAMap::getStack (uint256 const& id, bool include_nonmatching_leaf, bool partialOk)
{
    // Walk the tree as far as possible to the specified identifier
    // produce a stack of nodes along the way, with the terminal node at the top
    std::stack<SHAMapTreeNode::pointer> stack;
    SHAMapTreeNode::pointer node = root;

    while (!node->isLeaf ())
    {
        stack.push (node);

        int branch = node->selectBranch (id);
        assert (branch >= 0);

        if (node->isEmptyBranch (branch))
            return stack;

        try
        {
            node = getNode (node->getChildNodeID (branch), node->getChildHash (branch), false);
        }
        catch (SHAMapMissingNode& mn)
        {
            if (partialOk)
                return stack;

            mn.setTargetNode (id);
            throw;
        }
    }

    if (include_nonmatching_leaf || (node->peekItem ()->getTag () == id))
        stack.push (node);

    return stack;
}

void SHAMap::dirtyUp (std::stack<SHAMapTreeNode::pointer>& stack, uint256 const& target, uint256 prevHash)
{
    // walk the tree up from through the inner nodes to the root
    // update linking hashes and add nodes to dirty list

    assert ((mState != smsSynching) && (mState != smsImmutable));

    while (!stack.empty ())
    {
        SHAMapTreeNode::pointer node = stack.top ();
        stack.pop ();
        assert (node->isInnerNode ());

        int branch = node->selectBranch (target);
        assert (branch >= 0);

        returnNode (node, true);

        if (!node->setChildHash (branch, prevHash))
        {
            WriteLog (lsFATAL, SHAMap) << "dirtyUp terminates early";
            assert (false);
            return;
        }

#ifdef ST_DEBUG
        WriteLog (lsTRACE, SHAMap) << "dirtyUp sets branch " << branch << " to " << prevHash;
#endif
        prevHash = node->getNodeHash ();
        assert (prevHash.isNonZero ());
    }
}

SHAMapTreeNode::pointer SHAMap::checkCacheNode (const SHAMapNode& iNode)
{
    boost::unordered_map<SHAMapNode, SHAMapTreeNode::pointer>::iterator it = mTNByID.find (iNode);

    if (it == mTNByID.end ())
        return SHAMapTreeNode::pointer ();

    it->second->touch (mSeq);
    return it->second;
}

SHAMapTreeNode::pointer SHAMap::walkTo (uint256 const& id, bool modify)
{
    // walk down to the terminal node for this ID

    SHAMapTreeNode::pointer inNode = root;

    while (!inNode->isLeaf ())
    {
        int branch = inNode->selectBranch (id);

        if (inNode->isEmptyBranch (branch))
            return inNode;

        try
        {
            inNode = getNode (inNode->getChildNodeID (branch), inNode->getChildHash (branch), false);
        }
        catch (SHAMapMissingNode& mn)
        {
            mn.setTargetNode (id);
            throw;
        }
    }

    if (inNode->getTag () != id)
        return SHAMapTreeNode::pointer ();

    if (modify)
        returnNode (inNode, true);

    return inNode;
}

SHAMapTreeNode* SHAMap::walkToPointer (uint256 const& id)
{
    SHAMapTreeNode* inNode = root.get ();

    while (!inNode->isLeaf ())
    {
        int branch = inNode->selectBranch (id);

        if (inNode->isEmptyBranch (branch))
            return NULL;

        inNode = getNodePointer (inNode->getChildNodeID (branch), inNode->getChildHash (branch));
        assert (inNode);
    }

    return (inNode->getTag () == id) ? inNode : NULL;
}

SHAMapTreeNode::pointer SHAMap::getNode (const SHAMapNode& id, uint256 const& hash, bool modify)
{
    // retrieve a node whose node hash is known
    SHAMapTreeNode::pointer node = checkCacheNode (id);

    if (node)
    {
#ifdef BEAST_DEBUG

        if (node->getNodeHash () != hash)
        {
            WriteLog (lsFATAL, SHAMap) << "Attempt to get node, hash not in tree";
            WriteLog (lsFATAL, SHAMap) << "ID: " << id;
            WriteLog (lsFATAL, SHAMap) << "TgtHash " << hash;
            WriteLog (lsFATAL, SHAMap) << "NodHash " << node->getNodeHash ();
            throw std::runtime_error ("invalid node");
        }

#endif
        returnNode (node, modify);
        return node;
    }

    return fetchNodeExternal (id, hash);
}

SHAMapTreeNode* SHAMap::getNodePointer (const SHAMapNode& id, uint256 const& hash)
{
    // fast, but you do not hold a reference
    SHAMapTreeNode* ret = getNodePointerNT (id, hash);

    if (!ret)
        throw SHAMapMissingNode (mType, id, hash);

    return ret;
}

SHAMapTreeNode* SHAMap::getNodePointerNT (const SHAMapNode& id, uint256 const& hash)
{
    // fast, but you do not hold a reference
    boost::unordered_map<SHAMapNode, SHAMapTreeNode::pointer>::iterator it = mTNByID.find (id);

    if (it != mTNByID.end ())
        return it->second.get ();

    return fetchNodeExternalNT (id, hash).get ();
}

SHAMapTreeNode* SHAMap::getNodePointer (const SHAMapNode& id, uint256 const& hash, SHAMapSyncFilter* filter)
{
    SHAMapTreeNode* ret = getNodePointerNT (id, hash, filter);

    if (!ret)
        throw SHAMapMissingNode (mType, id, hash);

    return ret;
}

SHAMapTreeNode* SHAMap::getNodePointerNT (const SHAMapNode& id, uint256 const& hash, SHAMapSyncFilter* filter)
{
    SHAMapTreeNode* node = getNodePointerNT (id, hash);

    if (!node && filter)
    {
        Blob nodeData;

        if (filter->haveNode (id, hash, nodeData))
        {
            SHAMapTreeNode::pointer node = boost::make_shared<SHAMapTreeNode> (
                                               boost::cref (id), boost::cref (nodeData), mSeq - 1, snfPREFIX, boost::cref (hash), true);
            mTNByID[id] = node;
            filter->gotNode (true, id, hash, nodeData, node->getType ());
            return node.get ();
        }
    }

    return node;
}


void SHAMap::returnNode (SHAMapTreeNode::pointer& node, bool modify)
{
    // make sure the node is suitable for the intended operation (copy on write)
    assert (node->isValid ());
    assert (node->getSeq () <= mSeq);

    if (node && modify && (node->getSeq () != mSeq))
    {
        // have a CoW
        assert (node->getSeq () < mSeq);

        node = boost::make_shared<SHAMapTreeNode> (*node, mSeq); // here's to the new node, same as the old node
        assert (node->isValid ());

        mTNByID[*node] = node;

        if (node->isRoot ())
            root = node;

        if (mDirtyNodes)
            (*mDirtyNodes)[*node] = node;
    }
}

void SHAMap::trackNewNode (SHAMapTreeNode::pointer& node)
{
    if (mDirtyNodes)
        (*mDirtyNodes)[*node] = node;
}

SHAMapTreeNode* SHAMap::firstBelow (SHAMapTreeNode* node)
{
    // Return the first item below this node
    do
    {
        // Walk down the tree
        if (node->hasItem ()) return node;

        bool foundNode = false;

        for (int i = 0; i < 16; ++i)
            if (!node->isEmptyBranch (i))
            {
                node = getNodePointer (node->getChildNodeID (i), node->getChildHash (i));
                foundNode = true;
                break;
            }

        if (!foundNode)
            return NULL;
    }
    while (true);
}

SHAMapTreeNode* SHAMap::lastBelow (SHAMapTreeNode* node)
{
    do
    {
        // Walk down the tree
        if (node->hasItem ())
            return node;

        bool foundNode = false;

        for (int i = 15; i >= 0; ++i)
            if (!node->isEmptyBranch (i))
            {
                node = getNodePointer (node->getChildNodeID (i), node->getChildHash (i));
                foundNode = true;
                break;
            }

        if (!foundNode)
            return NULL;
    }
    while (true);
}

SHAMapItem::pointer SHAMap::onlyBelow (SHAMapTreeNode* node)
{
    // If there is only one item below this node, return it
    while (!node->isLeaf ())
    {
        SHAMapTreeNode* nextNode = NULL;

        for (int i = 0; i < 16; ++i)
            if (!node->isEmptyBranch (i))
            {
                if (nextNode)
                    return SHAMapItem::pointer (); // two leaves below

                nextNode = getNodePointer (node->getChildNodeID (i), node->getChildHash (i));
            }

        if (!nextNode)
        {
            WriteLog (lsFATAL, SHAMap) << *node;
            assert (false);
            return SHAMapItem::pointer ();
        }

        node = nextNode;
    }

    assert (node->hasItem ());
    return node->peekItem ();
}

void SHAMap::eraseChildren (SHAMapTreeNode::pointer node)
{
    // this node has only one item below it, erase its children
    bool erase = false;

    while (node->isInner ())
    {
        for (int i = 0; i < 16; ++i)
            if (!node->isEmptyBranch (i))
            {
                SHAMapTreeNode::pointer nextNode = getNode (node->getChildNodeID (i), node->getChildHash (i), false);

                if (erase)
                {
                    returnNode (node, true);

                    if (mTNByID.erase (*node))
                        assert (false);
                }

                erase = true;
                node = nextNode;
                break;
            }
    }

    returnNode (node, true);

    if (mTNByID.erase (*node) == 0)
        assert (false);

    return;
}

static const SHAMapItem::pointer no_item;

SHAMapItem::pointer SHAMap::peekFirstItem ()
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    SHAMapTreeNode* node = firstBelow (root.get ());

    if (!node)
        return no_item;

    return node->peekItem ();
}

SHAMapItem::pointer SHAMap::peekFirstItem (SHAMapTreeNode::TNType& type)
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    SHAMapTreeNode* node = firstBelow (root.get ());

    if (!node)
        return no_item;

    type = node->getType ();
    return node->peekItem ();
}

SHAMapItem::pointer SHAMap::peekLastItem ()
{
    boost::recursive_mutex::scoped_lock sl (mLock);
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
    // Get a pointer to the next item in the tree after a given item - item must be in tree
    boost::recursive_mutex::scoped_lock sl (mLock);

    std::stack<SHAMapTreeNode::pointer> stack = getStack (id, true, false);

    while (!stack.empty ())
    {
        SHAMapTreeNode::pointer node = stack.top ();
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
            for (int i = node->selectBranch (id) + 1; i < 16; ++i)
                if (!node->isEmptyBranch (i))
                {
                    SHAMapTreeNode* firstNode = getNodePointer (node->getChildNodeID (i), node->getChildHash (i));
                    assert (firstNode);
                    firstNode = firstBelow (firstNode);

                    if (!firstNode)
                        throw std::runtime_error ("missing node");

                    type = firstNode->getType ();
                    return firstNode->peekItem ();
                }
    }

    // must be last item
    return no_item;
}

// Get a pointer to the previous item in the tree after a given item - item must be in tree
SHAMapItem::pointer SHAMap::peekPrevItem (uint256 const& id)
{
    boost::recursive_mutex::scoped_lock sl (mLock);

    std::stack<SHAMapTreeNode::pointer> stack = getStack (id, true, false);

    while (!stack.empty ())
    {
        SHAMapTreeNode::pointer node = stack.top ();
        stack.pop ();

        if (node->isLeaf ())
        {
            if (node->peekItem ()->getTag () < id)
                return node->peekItem ();
        }
        else
        {
            for (int i = node->selectBranch (id) - 1; i >= 0; --i)
            {
                if (!node->isEmptyBranch (i))
                {
                    node = getNode (node->getChildNodeID (i), node->getChildHash (i), false);
                    SHAMapTreeNode* item = firstBelow (node.get ());

                    if (!item)
                        throw std::runtime_error ("missing node");

                    return item->peekItem ();
                }
            }
        }
    }

    // must be last item
    return no_item;
}

SHAMapItem::pointer SHAMap::peekItem (uint256 const& id)
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    SHAMapTreeNode* leaf = walkToPointer (id);

    if (!leaf)
        return no_item;

    return leaf->peekItem ();
}

SHAMapItem::pointer SHAMap::peekItem (uint256 const& id, SHAMapTreeNode::TNType& type)
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    SHAMapTreeNode* leaf = walkToPointer (id);

    if (!leaf)
        return no_item;

    type = leaf->getType ();
    return leaf->peekItem ();
}

SHAMapItem::pointer SHAMap::peekItem (uint256 const& id, uint256& hash)
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    SHAMapTreeNode* leaf = walkToPointer (id);

    if (!leaf)
        return no_item;

    hash = leaf->getNodeHash ();
    return leaf->peekItem ();
}


bool SHAMap::hasItem (uint256 const& id)
{
    // does the tree have an item with this ID
    boost::recursive_mutex::scoped_lock sl (mLock);

    SHAMapTreeNode* leaf = walkToPointer (id);
    return (leaf != NULL);
}

bool SHAMap::delItem (uint256 const& id)
{
    // delete the item with this ID
    boost::recursive_mutex::scoped_lock sl (mLock);
    assert (mState != smsImmutable);

    std::stack<SHAMapTreeNode::pointer> stack = getStack (id, true, false);

    if (stack.empty ())
        throw std::runtime_error ("missing node");

    SHAMapTreeNode::pointer leaf = stack.top ();
    stack.pop ();

    if (!leaf || !leaf->hasItem () || (leaf->peekItem ()->getTag () != id))
        return false;

    SHAMapTreeNode::TNType type = leaf->getType ();
    returnNode (leaf, true);

    if (mTNByID.erase (*leaf) == 0)
        assert (false);

    uint256 prevHash;

    while (!stack.empty ())
    {
        SHAMapTreeNode::pointer node = stack.top ();
        stack.pop ();
        returnNode (node, true);
        assert (node->isInner ());

        if (!node->setChildHash (node->selectBranch (id), prevHash))
        {
            assert (false);
            return true;
        }

        if (!node->isRoot ())
        {
            // we may have made this a node with 1 or 0 children
            int bc = node->getBranchCount ();

            if (bc == 0)
            {
                prevHash = uint256 ();

                if (!mTNByID.erase (*node))
                    assert (false);
            }
            else if (bc == 1)
            {
                // pull up on the thread
                SHAMapItem::pointer item = onlyBelow (node.get ());

                if (item)
                {
                    returnNode (node, true);
                    eraseChildren (node);
                    node->setItem (item, type);
                }

                prevHash = node->getNodeHash ();
                assert (prevHash.isNonZero ());
            }
            else
            {
                prevHash = node->getNodeHash ();
                assert (prevHash.isNonZero ());
            }
        }
        else assert (stack.empty ());
    }

    return true;
}

bool SHAMap::addGiveItem (SHAMapItem::ref item, bool isTransaction, bool hasMeta)
{
    // add the specified item, does not update
    uint256 tag = item->getTag ();
    SHAMapTreeNode::TNType type = !isTransaction ? SHAMapTreeNode::tnACCOUNT_STATE :
                                  (hasMeta ? SHAMapTreeNode::tnTRANSACTION_MD : SHAMapTreeNode::tnTRANSACTION_NM);

    boost::recursive_mutex::scoped_lock sl (mLock);
    assert (mState != smsImmutable);

    std::stack<SHAMapTreeNode::pointer> stack = getStack (tag, true, false);

    if (stack.empty ())
        throw std::runtime_error ("missing node");

    SHAMapTreeNode::pointer node = stack.top ();
    stack.pop ();

    if (node->isLeaf () && (node->peekItem ()->getTag () == tag))
        return false;

    uint256 prevHash;
    returnNode (node, true);

    if (node->isInner ())
    {
        // easy case, we end on an inner node
        int branch = node->selectBranch (tag);
        assert (node->isEmptyBranch (branch));
        SHAMapTreeNode::pointer newNode =
            boost::make_shared<SHAMapTreeNode> (node->getChildNodeID (branch), item, type, mSeq);

        if (!mTNByID.emplace (SHAMapNode (*newNode), newNode).second)
        {
            WriteLog (lsFATAL, SHAMap) << "Node: " << *node;
            WriteLog (lsFATAL, SHAMap) << "NewNode: " << *newNode;
            dump ();
            assert (false);
            throw std::runtime_error ("invalid inner node");
        }

        trackNewNode (newNode);
        node->setChildHash (branch, newNode->getNodeHash ());
    }
    else
    {
        // this is a leaf node that has to be made an inner node holding two items
        SHAMapItem::pointer otherItem = node->peekItem ();
        assert (otherItem && (tag != otherItem->getTag ()));

        node->makeInner ();

        int b1, b2;

        while ((b1 = node->selectBranch (tag)) == (b2 = node->selectBranch (otherItem->getTag ())))
        {
            // we need a new inner node, since both go on same branch at this level
            SHAMapTreeNode::pointer newNode =
                boost::make_shared<SHAMapTreeNode> (mSeq, node->getChildNodeID (b1));
            newNode->makeInner ();

            if (!mTNByID.emplace (SHAMapNode (*newNode), newNode).second)
                assert (false);

            stack.push (node);
            node = newNode;
            trackNewNode (node);
        }

        // we can add the two leaf nodes here
        assert (node->isInner ());
        SHAMapTreeNode::pointer newNode =
            boost::make_shared<SHAMapTreeNode> (node->getChildNodeID (b1), item, type, mSeq);
        assert (newNode->isValid () && newNode->isLeaf ());

        if (!mTNByID.emplace (SHAMapNode (*newNode), newNode).second)
            assert (false);

        node->setChildHash (b1, newNode->getNodeHash ()); // OPTIMIZEME hash op not needed
        trackNewNode (newNode);

        newNode = boost::make_shared<SHAMapTreeNode> (node->getChildNodeID (b2), otherItem, type, mSeq);
        assert (newNode->isValid () && newNode->isLeaf ());

        if (!mTNByID.emplace (SHAMapNode (*newNode), newNode).second)
            assert (false);

        node->setChildHash (b2, newNode->getNodeHash ());
        trackNewNode (newNode);
    }

    dirtyUp (stack, tag, node->getNodeHash ());
    return true;
}

bool SHAMap::addItem (const SHAMapItem& i, bool isTransaction, bool hasMetaData)
{
    return addGiveItem (boost::make_shared<SHAMapItem> (i), isTransaction, hasMetaData);
}

bool SHAMap::updateGiveItem (SHAMapItem::ref item, bool isTransaction, bool hasMeta)
{
    // can't change the tag but can change the hash
    uint256 tag = item->getTag ();

    boost::recursive_mutex::scoped_lock sl (mLock);
    assert (mState != smsImmutable);

    std::stack<SHAMapTreeNode::pointer> stack = getStack (tag, true, false);

    if (stack.empty ())
        throw std::runtime_error ("missing node");

    SHAMapTreeNode::pointer node = stack.top ();
    stack.pop ();

    if (!node->isLeaf () || (node->peekItem ()->getTag () != tag))
    {
        assert (false);
        return false;
    }

    returnNode (node, true);

    if (!node->setItem (item, !isTransaction ? SHAMapTreeNode::tnACCOUNT_STATE :
                        (hasMeta ? SHAMapTreeNode::tnTRANSACTION_MD : SHAMapTreeNode::tnTRANSACTION_NM)))
    {
        WriteLog (lsWARNING, SHAMap) << "SHAMap setItem, no change";
        return true;
    }

    dirtyUp (stack, tag, node->getNodeHash ());
    return true;
}

void SHAMapItem::dump ()
{
    WriteLog (lsINFO, SHAMap) << "SHAMapItem(" << mTag << ") " << mData.size () << "bytes";
}

SHAMapTreeNode::pointer SHAMap::fetchNodeExternal (const SHAMapNode& id, uint256 const& hash)
{
    SHAMapTreeNode::pointer ret = fetchNodeExternalNT (id, hash);

    if (!ret)
        throw SHAMapMissingNode (mType, id, hash);

    return ret;
}

SHAMapTreeNode::pointer SHAMap::fetchNodeExternalNT (const SHAMapNode& id, uint256 const& hash)
{
    SHAMapTreeNode::pointer ret;

    if (!getApp().running ())
        return ret;

    HashedObject::pointer obj (getApp().getHashedObjectStore ().retrieve (hash));

    if (!obj)
    {
        //      WriteLog (lsTRACE, SHAMap) << "fetchNodeExternal: missing " << hash;
        if (mLedgerSeq != 0)
        {
            getApp().getOPs ().missingNodeInLedger (mLedgerSeq);
            mLedgerSeq = 0;
        }

        return ret;
    }

    try
    {
        ret = boost::make_shared<SHAMapTreeNode> (id, obj->getData (), mSeq, snfPREFIX, hash, true);

        if (id != *ret)
        {
            WriteLog (lsFATAL, SHAMap) << "id:" << id << ", got:" << *ret;
            assert (false);
            return SHAMapTreeNode::pointer ();
        }

        if (ret->getNodeHash () != hash)
        {
            WriteLog (lsFATAL, SHAMap) << "Hashes don't match";
            assert (false);
            return SHAMapTreeNode::pointer ();
        }

        if (id.isRoot ())
            mTNByID[id] = ret;
        else if (!mTNByID.emplace (id, ret).second)
            assert (false);

        trackNewNode (ret);
        return ret;
    }
    catch (...)
    {
        WriteLog (lsWARNING, SHAMap) << "fetchNodeExternal gets an invalid node: " << hash;
        return SHAMapTreeNode::pointer ();
    }
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

    SHAMapTreeNode::pointer newRoot = fetchNodeExternalNT(SHAMapNode(), hash);
    if (newRoot)
    	root = newRoot;
	else
    {
        Blob nodeData;

        if (!filter || !filter->haveNode (SHAMapNode (), hash, nodeData))
            return false;

        root = boost::make_shared<SHAMapTreeNode> (SHAMapNode (), nodeData,
                mSeq - 1, snfPREFIX, hash, true);
        mTNByID[*root] = root;
        filter->gotNode (true, SHAMapNode (), hash, nodeData, root->getType ());
    }

    assert (root->getNodeHash () == hash);
    return true;
}

int SHAMap::armDirty ()
{
    // begin saving dirty nodes
    mDirtyNodes = boost::make_shared< boost::unordered_map<SHAMapNode, SHAMapTreeNode::pointer> > ();
    return ++mSeq;
}

int SHAMap::flushDirty (DirtyMap& map, int maxNodes, HashedObjectType t, uint32 seq)
{
    int flushed = 0;
    Serializer s;

    for (DirtyMap::iterator it = map.begin (); it != map.end (); it = map.erase (it))
    {
        //      tLog(t == hotTRANSACTION_NODE, lsDEBUG) << "TX node write " << it->first;
        //      tLog(t == hotACCOUNT_NODE, lsDEBUG) << "STATE node write " << it->first;
        s.erase ();
        it->second->addRaw (s, snfPREFIX);

#ifdef BEAST_DEBUG

        if (s.getSHA512Half () != it->second->getNodeHash ())
        {
            WriteLog (lsFATAL, SHAMap) << * (it->second);
            WriteLog (lsFATAL, SHAMap) << lexical_cast_i (s.getDataLength ());
            WriteLog (lsFATAL, SHAMap) << s.getSHA512Half () << " != " << it->second->getNodeHash ();
            assert (false);
        }

#endif

        getApp().getHashedObjectStore ().store (t, seq, s.peekData (), it->second->getNodeHash ());

        if (flushed++ >= maxNodes)
            return flushed;
    }

    return flushed;
}

boost::shared_ptr<SHAMap::DirtyMap> SHAMap::disarmDirty ()
{
    // stop saving dirty nodes
    boost::recursive_mutex::scoped_lock sl (mLock);

    boost::shared_ptr<DirtyMap> ret;
    ret.swap (mDirtyNodes);
    return ret;
}

SHAMapTreeNode::pointer SHAMap::getNode (const SHAMapNode& nodeID)
{

    SHAMapTreeNode::pointer node = checkCacheNode (nodeID);

    if (node)
        return node;

    node = root;

    while (nodeID != *node)
    {
        int branch = node->selectBranch (nodeID.getNodeID ());
        assert (branch >= 0);

        if ((branch < 0) || node->isEmptyBranch (branch))
            return SHAMapTreeNode::pointer ();

        node = getNode (node->getChildNodeID (branch), node->getChildHash (branch), false);
        assert (node);
    }

    return node;
}

bool SHAMap::getPath (uint256 const& index, std::vector< Blob >& nodes, SHANodeFormat format)
{
    // Return the path of nodes to the specified index in the specified format
    // Return value: true = node present, false = node not present

    boost::recursive_mutex::scoped_lock sl (mLock);
    SHAMapTreeNode* inNode = root.get ();

    while (!inNode->isLeaf ())
    {
        Serializer s;
        inNode->addRaw (s, format);
        nodes.push_back (s.peekData ());

        int branch = inNode->selectBranch (index);

        if (inNode->isEmptyBranch (branch)) // paths leads to empty branch
            return false;

        inNode = getNodePointer (inNode->getChildNodeID (branch), inNode->getChildHash (branch));
        assert (inNode);
    }

    if (inNode->getTag () != index) // path leads to different leaf
        return false;

    // path lead to the requested leaf
    Serializer s;
    inNode->addRaw (s, format);
    nodes.push_back (s.peekData ());
    return true;
}

void SHAMap::dropCache ()
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    assert (mState == smsImmutable);

    mTNByID.clear ();

    if (root)
        mTNByID[*root] = root;
}

void SHAMap::dropBelow (SHAMapTreeNode* d)
{
    if (d->isInner ())
        for (int i = 0 ; i < 16; ++i)
            if (!d->isEmptyBranch (i))
                mTNByID.erase (d->getChildNodeID (i));
}

void SHAMap::dump (bool hash)
{
    WriteLog (lsINFO, SHAMap) << " MAP Contains";
    boost::recursive_mutex::scoped_lock sl (mLock);

    for (boost::unordered_map<SHAMapNode, SHAMapTreeNode::pointer>::iterator it = mTNByID.begin ();
            it != mTNByID.end (); ++it)
    {
        WriteLog (lsINFO, SHAMap) << it->second->getString ();
        CondLog (hash, lsINFO, SHAMap) << it->second->getNodeHash ();
    }

}
