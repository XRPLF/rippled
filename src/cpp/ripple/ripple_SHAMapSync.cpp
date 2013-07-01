//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// VFALCO TODO tidy up this global

static const uint256 uZero;

KeyCache <uint256, UptimeTimerAdapter> SHAMap::fullBelowCache ("fullBelowCache", 524288, 240);

void SHAMap::getMissingNodes (std::vector<SHAMapNode>& nodeIDs, std::vector<uint256>& hashes, int max,
                              SHAMapSyncFilter* filter)
{
    boost::recursive_mutex::scoped_lock sl (mLock);

    assert (root->isValid ());

    if (root->isFullBelow ())
    {
        clearSynching ();
        return;
    }

    if (!root->isInner ())
    {
        WriteLog (lsWARNING, SHAMap) << "synching empty tree";
        return;
    }

    std::stack<SHAMapTreeNode*> stack;
    stack.push (root.get ());

    while (!stack.empty ())
    {
        SHAMapTreeNode* node = stack.top ();
        stack.pop ();

        int base = rand () % 256;
        bool have_all = true;

        for (int ii = 0; ii < 16; ++ii)
        {
            // traverse in semi-random order
            int branch = (base + ii) % 16;

            if (!node->isEmptyBranch (branch))
            {
                uint256 const& childHash = node->getChildHash (branch);

                if (!fullBelowCache.isPresent (childHash))
                {
                    SHAMapNode childID = node->getChildNodeID (branch);
                    SHAMapTreeNode* d = getNodePointerNT (childID, childHash, filter);

                    if (!d)
                    {
                        // node is not in the map
                        nodeIDs.push_back (childID);
                        hashes.push_back (childHash);

                        if (--max <= 0)
                            return;

                        have_all = false;
                    }
                    else if (d->isInner () && !d->isFullBelow ())
                    {
                        have_all = false;
                        stack.push (d);
                    }
                }
            }
        }

        if (have_all)
        {
            node->setFullBelow ();

            if (mType == smtSTATE)
            {
                fullBelowCache.add (node->getNodeHash ());
                dropBelow (node);
            }
        }
    }

    if (nodeIDs.empty ())
        clearSynching ();
}

std::vector<uint256> SHAMap::getNeededHashes (int max, SHAMapSyncFilter* filter)
{
    std::vector<uint256> ret;
    boost::recursive_mutex::scoped_lock sl (mLock);

    assert (root->isValid ());

    if (root->isFullBelow () || !root->isInner ())
    {
        clearSynching ();
        return ret;
    }

    std::stack<SHAMapTreeNode*> stack;
    stack.push (root.get ());

    while (!stack.empty ())
    {
        SHAMapTreeNode* node = stack.top ();
        stack.pop ();

        int base = rand () % 256;
        bool have_all = true;

        for (int ii = 0; ii < 16; ++ii)
        {
            // traverse in semi-random order
            int branch = (base + ii) % 16;

            if (!node->isEmptyBranch (branch))
            {
                uint256 const& childHash = node->getChildHash (branch);

                if (!fullBelowCache.isPresent (childHash))
                {
                    SHAMapNode childID = node->getChildNodeID (branch);
                    SHAMapTreeNode* d = getNodePointerNT (childID, childHash, filter);

                    if (!d)
                    {
                        have_all = false;
                        ret.push_back (childHash);

                        if (--max <= 0)
                            return ret;
                    }
                    else if (d->isInner () && !d->isFullBelow ())
                    {
                        have_all = false;
                        stack.push (d);
                    }
                }
            }
        }

        if (have_all)
        {
            node->setFullBelow ();

            if (mType == smtSTATE)
            {
                fullBelowCache.add (node->getNodeHash ());
                dropBelow (node);
            }
        }
    }

    if (ret.empty ())
        clearSynching ();

    return ret;
}

bool SHAMap::getNodeFat (const SHAMapNode& wanted, std::vector<SHAMapNode>& nodeIDs,
                         std::list<Blob >& rawNodes, bool fatRoot, bool fatLeaves)
{
    // Gets a node and some of its children
    boost::recursive_mutex::scoped_lock sl (mLock);

    SHAMapTreeNode::pointer node = getNode (wanted);

    if (!node)
    {
        WriteLog (lsWARNING, SHAMap) << "peer requested node that is not in the map: " << wanted;
        throw std::runtime_error ("Peer requested node not in map");
    }

    if (node->isInner () && node->isEmpty ())
    {
        WriteLog (lsWARNING, SHAMap) << "peer requests empty node";
        return false;
    }

    nodeIDs.push_back (*node);
    Serializer s;
    node->addRaw (s, snfWIRE);
    rawNodes.push_back (s.peekData ());

    if ((!fatRoot && node->isRoot ()) || node->isLeaf ()) // don't get a fat root, can't get a fat leaf
        return true;

    for (int i = 0; i < 16; ++i)
        if (!node->isEmptyBranch (i))
        {
            SHAMapTreeNode::pointer nextNode = getNode (node->getChildNodeID (i), node->getChildHash (i), false);
            assert (nextNode);

            if (nextNode && (fatLeaves || !nextNode->isLeaf ()))
            {
                nodeIDs.push_back (*nextNode);
                Serializer s;
                nextNode->addRaw (s, snfWIRE);
                rawNodes.push_back (s.peekData ());
            }
        }

    return true;
}

bool SHAMap::getRootNode (Serializer& s, SHANodeFormat format)
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    root->addRaw (s, format);
    return true;
}

SHAMapAddNode SHAMap::addRootNode (Blob const& rootNode, SHANodeFormat format,
                                   SHAMapSyncFilter* filter)
{
    boost::recursive_mutex::scoped_lock sl (mLock);

    // we already have a root node
    if (root->getNodeHash ().isNonZero ())
    {
        WriteLog (lsTRACE, SHAMap) << "got root node, already have one";
        return SHAMapAddNode::okay ();
    }

    assert (mSeq >= 1);
    SHAMapTreeNode::pointer node =
        boost::make_shared<SHAMapTreeNode> (SHAMapNode (), rootNode, mSeq - 1, format, uZero, false);

    if (!node)
        return SHAMapAddNode::invalid ();

#ifdef BEAST_DEBUG
    node->dump ();
#endif

    root = node;
    mTNByID[*root] = root;

    if (root->getNodeHash ().isZero ())
    {
        root->setFullBelow ();
        clearSynching ();
    }
    else if (filter)
    {
        Serializer s;
        root->addRaw (s, snfPREFIX);
        filter->gotNode (false, *root, root->getNodeHash (), s.peekData (), root->getType ());
    }

    return SHAMapAddNode::useful ();
}

SHAMapAddNode SHAMap::addRootNode (uint256 const& hash, Blob const& rootNode, SHANodeFormat format,
                                   SHAMapSyncFilter* filter)
{
    boost::recursive_mutex::scoped_lock sl (mLock);

    // we already have a root node
    if (root->getNodeHash ().isNonZero ())
    {
        WriteLog (lsTRACE, SHAMap) << "got root node, already have one";
        assert (root->getNodeHash () == hash);
        return SHAMapAddNode::okay ();
    }

    assert (mSeq >= 1);
    SHAMapTreeNode::pointer node =
        boost::make_shared<SHAMapTreeNode> (SHAMapNode (), rootNode, mSeq - 1, format, uZero, false);

    if (!node || node->getNodeHash () != hash)
        return SHAMapAddNode::invalid ();

    root = node;
    mTNByID[*root] = root;

    if (root->getNodeHash ().isZero ())
    {
        root->setFullBelow ();
        clearSynching ();
    }
    else if (filter)
    {
        Serializer s;
        root->addRaw (s, snfPREFIX);
        filter->gotNode (false, *root, root->getNodeHash (), s.peekData (), root->getType ());
    }

    return SHAMapAddNode::useful ();
}

SHAMapAddNode SHAMap::addKnownNode (const SHAMapNode& node, Blob const& rawNode, SHAMapSyncFilter* filter)
{
    // return value: true=okay, false=error
    assert (!node.isRoot ());

    if (!isSynching ())
    {
        WriteLog (lsTRACE, SHAMap) << "AddKnownNode while not synching";
        return SHAMapAddNode::okay ();
    }

    boost::recursive_mutex::scoped_lock sl (mLock);

    if (checkCacheNode (node)) // Do we already have this node?
        return SHAMapAddNode::okay ();

    SHAMapTreeNode::pointer parent = checkCacheNode(node.getParentNodeID());
    SHAMapTreeNode* iNode = parent ? parent.get() : root.get ();

    while (!iNode->isLeaf () && !iNode->isFullBelow () && (iNode->getDepth () < node.getDepth ()))
    {
        int branch = iNode->selectBranch (node.getNodeID ());
        assert (branch >= 0);

        if (iNode->isEmptyBranch (branch))
        {
            WriteLog (lsWARNING, SHAMap) << "Add known node for empty branch" << node;
            return SHAMapAddNode::invalid ();
        }

        if (fullBelowCache.isPresent (iNode->getChildHash (branch)))
            return SHAMapAddNode::okay ();

        SHAMapTreeNode *nextNode = getNodePointerNT (iNode->getChildNodeID (branch), iNode->getChildHash (branch), filter);
        if (!nextNode)
        {
            if (iNode->getDepth () != (node.getDepth () - 1))
            {
                // Either this node is broken or we didn't request it (yet)
                WriteLog (lsWARNING, SHAMap) << "unable to hook node " << node;
                WriteLog (lsINFO, SHAMap) << " stuck at " << *iNode;
                WriteLog (lsINFO, SHAMap) << "got depth=" << node.getDepth () << ", walked to= " << iNode->getDepth ();
                return SHAMapAddNode::invalid ();
            }

            SHAMapTreeNode::pointer newNode =
                boost::make_shared<SHAMapTreeNode> (node, rawNode, mSeq - 1, snfWIRE, uZero, false);

            if (iNode->getChildHash (branch) != newNode->getNodeHash ())
            {
                WriteLog (lsWARNING, SHAMap) << "Corrupt node recevied";
                return SHAMapAddNode::invalid ();
            }

            if (filter)
            {
                Serializer s;
                newNode->addRaw (s, snfPREFIX);
                filter->gotNode (false, node, iNode->getChildHash (branch), s.peekData (), newNode->getType ());
            }

            mTNByID[node] = newNode;
            return SHAMapAddNode::useful ();
        }
        iNode = nextNode;
    }

    WriteLog (lsTRACE, SHAMap) << "got node, already had it (late)";
    return SHAMapAddNode::okay ();
}

bool SHAMap::deepCompare (SHAMap& other)
{
    // Intended for debug/test only
    std::stack<SHAMapTreeNode::pointer> stack;
    boost::recursive_mutex::scoped_lock sl (mLock);

    stack.push (root);

    while (!stack.empty ())
    {
        SHAMapTreeNode::pointer node = stack.top ();
        stack.pop ();

        SHAMapTreeNode::pointer otherNode;

        if (node->isRoot ()) otherNode = other.root;
        else otherNode = other.getNode (*node, node->getNodeHash (), false);

        if (!otherNode)
        {
            WriteLog (lsINFO, SHAMap) << "unable to fetch node";
            return false;
        }
        else if (otherNode->getNodeHash () != node->getNodeHash ())
        {
            WriteLog (lsWARNING, SHAMap) << "node hash mismatch";
            return false;
        }

        //      WriteLog (lsTRACE) << "Comparing inner nodes " << *node;

        if (node->getNodeHash () != otherNode->getNodeHash ())
            return false;

        if (node->isLeaf ())
        {
            if (!otherNode->isLeaf ()) return false;

            if (node->peekItem ()->getTag () != otherNode->peekItem ()->getTag ()) return false;

            if (node->peekItem ()->getData () != otherNode->peekItem ()->getData ()) return false;
        }
        else if (node->isInner ())
        {
            if (!otherNode->isInner ())
                return false;

            for (int i = 0; i < 16; ++i)
            {
                if (node->isEmptyBranch (i))
                {
                    if (!otherNode->isEmptyBranch (i)) return false;
                }
                else
                {
                    SHAMapTreeNode::pointer next = getNode (node->getChildNodeID (i), node->getChildHash (i), false);

                    if (!next)
                    {
                        WriteLog (lsWARNING, SHAMap) << "unable to fetch inner node";
                        return false;
                    }

                    stack.push (next);
                }
            }
        }
    }

    return true;
}

bool SHAMap::hasInnerNode (const SHAMapNode& nodeID, uint256 const& nodeHash)
{
    SHAMapTreeNode* node = root.get ();

    while (node->isInner () && (node->getDepth () < nodeID.getDepth ()))
    {
        int branch = node->selectBranch (nodeID.getNodeID ());

        if (node->isEmptyBranch (branch))
            break;

        node = getNodePointer (node->getChildNodeID (branch), node->getChildHash (branch));
    }

    return node->getNodeHash () == nodeHash;
}

bool SHAMap::hasLeafNode (uint256 const& tag, uint256 const& nodeHash)
{
    SHAMapTreeNode* node = root.get ();

    while (node->isInner ())
    {
        int branch = node->selectBranch (tag);

        if (node->isEmptyBranch (branch))
            return false;

        const uint256& nextHash = node->getChildHash (branch);

        if (nextHash == nodeHash)
            return true;

        node = getNodePointer (node->getChildNodeID (branch), nextHash);
    }

    return false;
}

static void addFPtoList (std::list<SHAMap::fetchPackEntry_t>& list, const uint256& hash, const Blob& blob)
{
    list.push_back (SHAMap::fetchPackEntry_t (hash, blob));
}

std::list<SHAMap::fetchPackEntry_t> SHAMap::getFetchPack (SHAMap* have, bool includeLeaves, int max)
{
    std::list<fetchPackEntry_t> ret;
    getFetchPack (have, includeLeaves, max, BIND_TYPE (addFPtoList, boost::ref (ret), P_1, P_2));
    return ret;
}

void SHAMap::getFetchPack (SHAMap* have, bool includeLeaves, int max,
                           FUNCTION_TYPE<void (const uint256&, const Blob&)> func)
{
    boost::recursive_mutex::scoped_lock ul1 (mLock);

    boost::shared_ptr< boost::unique_lock<boost::recursive_mutex> > ul2;

    if (have)
    {
        ul2 = boost::make_shared< boost::unique_lock<boost::recursive_mutex> >
              (boost::ref (have->mLock), boost::try_to_lock);

        if (! (*ul2))
        {
            WriteLog (lsINFO, SHAMap) << "Unable to create pack due to lock";
            return;
        }
    }


    if (root->isLeaf ())
    {
        if (includeLeaves && !root->getNodeHash ().isZero () &&
                (!have || !have->hasLeafNode (root->getTag (), root->getNodeHash ())))
        {
            Serializer s;
            root->addRaw (s, snfPREFIX);
            func (root->getNodeHash (), s.peekData ());
        }

        return;
    }

    if (root->getNodeHash ().isZero ())
        return;

    if (have && (root->getNodeHash () == have->root->getNodeHash ()))
        return;

    std::stack<SHAMapTreeNode*> stack; // contains unexplored non-matching inner node entries
    stack.push (root.get ());

    while (!stack.empty ())
    {
        SHAMapTreeNode* node = stack.top ();
        stack.pop ();

        // 1) Add this node to the pack
        Serializer s;
        node->addRaw (s, snfPREFIX);
        func (node->getNodeHash (), s.peekData ());
        --max;

        // 2) push non-matching child inner nodes
        for (int i = 0; i < 16; ++i)
        {
            if (!node->isEmptyBranch (i))
            {
                uint256 const& childHash = node->getChildHash (i);
                SHAMapNode childID = node->getChildNodeID (i);

                SHAMapTreeNode* next = getNodePointer (childID, childHash);

                if (next->isInner ())
                {
                    if (!have || !have->hasInnerNode (*next, childHash))
                        stack.push (next);
                }
                else if (includeLeaves && (!have || !have->hasLeafNode (next->getTag (), childHash)))
                {
                    Serializer s;
                    node->addRaw (s, snfPREFIX);
                    func (node->getNodeHash (), s.peekData ());
                    --max;
                }
            }
        }

        if (max <= 0)
            break;
    }
}

std::list<Blob > SHAMap::getTrustedPath (uint256 const& index)
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    std::stack<SHAMapTreeNode::pointer> stack = SHAMap::getStack (index, false, false);

    if (stack.empty () || !stack.top ()->isLeaf ())
        throw std::runtime_error ("requested leaf not present");

    std::list< Blob > path;
    Serializer s;

    while (!stack.empty ())
    {
        stack.top ()->addRaw (s, snfWIRE);
        path.push_back (s.getData ());
        s.erase ();
        stack.pop ();
    }

    return path;
}
