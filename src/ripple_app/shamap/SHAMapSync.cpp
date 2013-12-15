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

// VFALCO TODO tidy up this global

static const uint256 uZero;

KeyCache <uint256, UptimeTimerAdapter> SHAMap::fullBelowCache ("fullBelowCache", 524288, 240);

void SHAMap::visitLeaves (FUNCTION_TYPE<void (SHAMapItem::ref item)> function)
{
    SHAMap::pointer snap;
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);
        snap = snapShot (false);
    }
    snap->visitLeavesInternal(function);
}

void SHAMap::visitLeavesInternal (FUNCTION_TYPE<void (SHAMapItem::ref item)>& function)
{
    assert (root->isValid ());

    if (!root || root->isEmpty ())
        return;

    if (!root->isInner ())
    {
        function (root->peekItem ());
        return;
    }

    typedef std::pair<int, SHAMapTreeNode*> posPair;

    std::stack<posPair> stack;
    SHAMapTreeNode* node = root.get ();
    int pos = 0;

    while (1)
    {
        while (pos < 16)
        {
            if (node->isEmptyBranch (pos))
            {
                ++pos; // move to next position
            }
            else
            {
                SHAMapTreeNode* child = getNodePointer (node->getChildNodeID (pos), node->getChildHash (pos));
                if (child->isLeaf ())
                {
                    function (child->peekItem ());
                    mTNByID.erase (*child); // don't need this leaf anymore
                    ++pos;
                }
                else
                {
                    if (pos != 15)
                        stack.push (posPair (pos + 1, node)); // save next position to resume at
                    else
                        mTNByID.erase (*node); // don't need this inner node anymore

                    // descend to the child's first position
                    node = child;
                    pos = 0;
                }
            }
        }

        // We are done with this inner node
        mTNByID.erase (*node);

        if (stack.empty ())
            break;

        pos = stack.top ().first;
        node = stack.top ().second;
        stack.pop ();
    }
}

void SHAMap::getMissingNodes (std::vector<SHAMapNode>& nodeIDs, std::vector<uint256>& hashes, int max,
                              SHAMapSyncFilter* filter)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    assert (root->isValid ());
    assert (root->getNodeHash().isNonZero ());

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
                        // node is not in the database
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
                if (getConfig().NODE_SIZE <= 3)
                    dropBelow(node);
            }
        }
    }

    if (nodeIDs.empty ())
        clearSynching ();
}

std::vector<uint256> SHAMap::getNeededHashes (int max, SHAMapSyncFilter* filter)
{
    std::vector<uint256> nodeHashes;
    nodeHashes.reserve(max);

    std::vector<SHAMapNode> nodeIDs;
    nodeIDs.reserve(max);

    getMissingNodes(nodeIDs, nodeHashes, max, filter);
    return nodeHashes;
}

bool SHAMap::getNodeFat (const SHAMapNode& wanted, std::vector<SHAMapNode>& nodeIDs,
                         std::list<Blob >& rawNodes, bool fatRoot, bool fatLeaves)
{
    // Gets a node and some of its children
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    SHAMapTreeNode* node = getNodePointer(wanted);

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

    int count;
    do
    {

        Serializer s;
        node->addRaw (s, snfWIRE);
        nodeIDs.push_back(*node);
        rawNodes.push_back (s.peekData ());

        if ((!fatRoot && node->isRoot ()) || node->isLeaf ()) // don't get a fat root, can't get a fat leaf
            return true;

        SHAMapTreeNode* nextNode = NULL;

        count = 0;
        for (int i = 0; i < 16; ++i)
            if (!node->isEmptyBranch (i))
            {
                nextNode = getNodePointer (node->getChildNodeID (i), node->getChildHash (i));
                ++count;
                if (fatLeaves || nextNode->isInner ())
                {
                    Serializer s;
                    nextNode->addRaw (s, snfWIRE);
                    nodeIDs.push_back (*nextNode);
                    rawNodes.push_back (s.peekData ());
                }
            }

        node = nextNode;

    // So long as there's exactly one inner node, we take it
    } while ((count == 1) && node->isInner());

    return true;
}

bool SHAMap::getRootNode (Serializer& s, SHANodeFormat format)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    root->addRaw (s, format);
    return true;
}

SHAMapAddNode SHAMap::addRootNode (Blob const& rootNode, SHANodeFormat format,
                                   SHAMapSyncFilter* filter)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    // we already have a root node
    if (root->getNodeHash ().isNonZero ())
    {
        WriteLog (lsTRACE, SHAMap) << "got root node, already have one";
        return SHAMapAddNode::duplicate ();
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

    if (root->isLeaf())
        clearSynching ();

    if (filter)
    {
        Serializer s;
        root->addRaw (s, snfPREFIX);
        filter->gotNode (false, *root, root->getNodeHash (), s.modData (), root->getType ());
    }

    return SHAMapAddNode::useful ();
}

SHAMapAddNode SHAMap::addRootNode (uint256 const& hash, Blob const& rootNode, SHANodeFormat format,
                                   SHAMapSyncFilter* filter)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    // we already have a root node
    if (root->getNodeHash ().isNonZero ())
    {
        WriteLog (lsTRACE, SHAMap) << "got root node, already have one";
        assert (root->getNodeHash () == hash);
        return SHAMapAddNode::duplicate ();
    }

    assert (mSeq >= 1);
    SHAMapTreeNode::pointer node =
        boost::make_shared<SHAMapTreeNode> (SHAMapNode (), rootNode, mSeq - 1, format, uZero, false);

    if (!node || node->getNodeHash () != hash)
        return SHAMapAddNode::invalid ();

    root = node;
    mTNByID[*root] = root;

    if (root->isLeaf())
        clearSynching ();

    if (filter)
    {
        Serializer s;
        root->addRaw (s, snfPREFIX);
        filter->gotNode (false, *root, root->getNodeHash (), s.modData (), root->getType ());
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
        return SHAMapAddNode::duplicate ();
    }

    ScopedLockType sl (mLock, __FILE__, __LINE__);

    if (checkCacheNode (node)) // Do we already have this node?
        return SHAMapAddNode::duplicate ();

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
            return SHAMapAddNode::duplicate ();

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
                boost::make_shared<SHAMapTreeNode> (node, rawNode, 0, snfWIRE, uZero, false);

            if (iNode->getChildHash (branch) != newNode->getNodeHash ())
            {
                WriteLog (lsWARNING, SHAMap) << "Corrupt node recevied";
                return SHAMapAddNode::invalid ();
            }

            canonicalize (iNode->getChildHash (branch), newNode);

            if (filter)
            {
                Serializer s;
                newNode->addRaw (s, snfPREFIX);
                filter->gotNode (false, node, iNode->getChildHash (branch), s.modData (), newNode->getType ());
            }

            mTNByID[node] = newNode;
            return SHAMapAddNode::useful ();
        }
        iNode = nextNode;
    }

    WriteLog (lsTRACE, SHAMap) << "got node, already had it (late)";
    return SHAMapAddNode::duplicate ();
}

bool SHAMap::deepCompare (SHAMap& other)
{
    // Intended for debug/test only
    std::stack<SHAMapTreeNode::pointer> stack;
    ScopedLockType sl (mLock, __FILE__, __LINE__);

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
            WriteLog (lsWARNING, SHAMap) << "node hash mismatch " << *node;
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
    boost::unordered_map<SHAMapNode, SHAMapTreeNode::pointer>::iterator it = mTNByID.find (nodeID);
    if (it != mTNByID.end())
        return it->second->getNodeHash() == nodeHash;

    SHAMapTreeNode* node = root.get ();

    while (node->isInner () && (node->getDepth () < nodeID.getDepth ()))
    {
        int branch = node->selectBranch (nodeID.getNodeID ());

        if (node->isEmptyBranch (branch))
            return false;

        node = getNodePointer (node->getChildNodeID (branch), node->getChildHash (branch));
    }

    return node->getNodeHash () == nodeHash;
}

bool SHAMap::hasLeafNode (uint256 const& tag, uint256 const& nodeHash)
{
    SHAMapTreeNode* node = root.get ();

    if (!node->isInner()) // only one leaf node in the tree
        return node->getNodeHash() == nodeHash;

    do
    {
        int branch = node->selectBranch (tag);

        if (node->isEmptyBranch (branch)) // Dead end, node must not be here
            return false;

        const uint256& nextHash = node->getChildHash (branch);

        if (nextHash == nodeHash) // Matching leaf, no need to retrieve it
            return true;

        node = getNodePointer (node->getChildNodeID (branch), nextHash);
    }
    while (node->isInner());

    return false; // If this was a matching leaf, we would have caught it already
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
    ScopedLockType ul1 (mLock, __FILE__, __LINE__);

    ScopedPointer <LockType::ScopedTryLockType> ul2;

    if (have)
    {
        ul2 = new LockType::ScopedTryLockType (have->mLock, __FILE__, __LINE__);

        if (! ul2->owns_lock ())
        {
            WriteLog (lsINFO, SHAMap) << "Unable to create pack due to lock";
            return;
        }
    }


    if (root->getNodeHash ().isZero ())
        return;

    if (have && (root->getNodeHash () == have->root->getNodeHash ()))
        return;

    if (root->isLeaf ())
    {
        if (includeLeaves &&
                (!have || !have->hasLeafNode (root->getTag (), root->getNodeHash ())))
        {
            Serializer s;
            root->addRaw (s, snfPREFIX);
            func (boost::cref(root->getNodeHash ()), boost::cref(s.peekData ()));
        }

        return;
    }

    std::stack<SHAMapTreeNode*> stack; // contains unexplored non-matching inner node entries
    stack.push (root.get());

    while (!stack.empty() && (max > 0))
    {
        SHAMapTreeNode* node = stack.top ();
        stack.pop ();

        // 1) Add this node to the pack
        Serializer s;
        node->addRaw (s, snfPREFIX);
        func (boost::cref(node->getNodeHash ()), boost::cref(s.peekData ()));
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
                else if (includeLeaves && (!have || !have->hasLeafNode (next->getTag(), childHash)))
                {
                    Serializer s;
                    next->addRaw (s, snfPREFIX);
                    func (boost::cref(childHash), boost::cref(s.peekData ()));
                    --max;
                }
            }
        }
    }
}

std::list<Blob > SHAMap::getTrustedPath (uint256 const& index)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    std::stack<SHAMapTreeNode::pointer> stack = SHAMap::getStack (index, false);

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

//------------------------------------------------------------------------------

#ifdef BEAST_DEBUG
//#define SMS_DEBUG
#endif

class SHAMapSyncTests : public UnitTest
{
public:
    SHAMapSyncTests () : UnitTest ("SHAMapSync", "ripple")
    {
    }

    static SHAMapItem::pointer makeRandomAS ()
    {
        Serializer s;

        for (int d = 0; d < 3; ++d) s.add32 (rand ());

        return boost::make_shared<SHAMapItem> (s.getRIPEMD160 ().to256 (), s.peekData ());
    }

    bool confuseMap (SHAMap& map, int count)
    {
        // add a bunch of random states to a map, then remove them
        // map should be the same
        uint256 beforeHash = map.getHash ();

        std::list<uint256> items;

        for (int i = 0; i < count; ++i)
        {
            SHAMapItem::pointer item = makeRandomAS ();
            items.push_back (item->getTag ());

            if (!map.addItem (*item, false, false))
            {
                journal().fatal <<
                    "Unable to add item to map";
                return false;
            }
        }

        for (std::list<uint256>::iterator it = items.begin (); it != items.end (); ++it)
        {
            if (!map.delItem (*it))
            {
                journal().fatal <<
                    "Unable to remove item from map";
                return false;
            }
        }

        if (beforeHash != map.getHash ())
        {
            journal().fatal <<
                "Hashes do not match";
            return false;
        }

        return true;
    }

    void runTest ()
    {
        unsigned int seed;

        // VFALCO TODO Replace this with beast::Random
        RAND_pseudo_bytes (reinterpret_cast<unsigned char*> (&seed), sizeof (seed));
        srand (seed);

        SHAMap source (smtFREE), destination (smtFREE);

        int items = 10000;
        for (int i = 0; i < items; ++i)
            source.addItem (*makeRandomAS (), false, false);



        beginTestCase ("add/remove");

        unexpected (!confuseMap (source, 500), "ConfuseMap");

        source.setImmutable ();

        std::vector<SHAMapNode> nodeIDs, gotNodeIDs;
        std::list< Blob > gotNodes;
        std::vector<uint256> hashes;

        std::vector<SHAMapNode>::iterator nodeIDIterator;
        std::list< Blob >::iterator rawNodeIterator;

        int passes = 0;
        int nodes = 0;

        destination.setSynching ();

        unexpected (!source.getNodeFat (SHAMapNode (), nodeIDs, gotNodes, (rand () % 2) == 0, (rand () % 2) == 0),
            "GetNodeFat");

        unexpected (gotNodes.size () < 1, "NodeSize");

        unexpected (!destination.addRootNode (*gotNodes.begin (), snfWIRE, NULL).isGood(), "AddRootNode");

        nodeIDs.clear ();
        gotNodes.clear ();

#ifdef SMS_DEBUG
        int bytes = 0;
#endif

        do
        {
            ++passes;
            hashes.clear ();

            // get the list of nodes we know we need
            destination.getMissingNodes (nodeIDs, hashes, 2048, NULL);

            if (nodeIDs.empty ()) break;

            // get as many nodes as possible based on this information
            for (nodeIDIterator = nodeIDs.begin (); nodeIDIterator != nodeIDs.end (); ++nodeIDIterator)
            {
                if (!source.getNodeFat (*nodeIDIterator, gotNodeIDs, gotNodes, (rand () % 2) == 0, (rand () % 2) == 0))
                {
                    WriteLog (lsFATAL, SHAMap) << "GetNodeFat fails";
                    fail ("GetNodeFat");
                }
                else
                {
                    pass ();
                }
            }

            assert (gotNodeIDs.size () == gotNodes.size ());
            nodeIDs.clear ();
            hashes.clear ();

            if (gotNodeIDs.empty ())
            {
                fail ("Got Node ID");
            }
            else
            {
                pass ();
            }

            for (nodeIDIterator = gotNodeIDs.begin (), rawNodeIterator = gotNodes.begin ();
                    nodeIDIterator != gotNodeIDs.end (); ++nodeIDIterator, ++rawNodeIterator)
            {
                ++nodes;
#ifdef SMS_DEBUG
                bytes += rawNodeIterator->size ();
#endif

                if (!destination.addKnownNode (*nodeIDIterator, *rawNodeIterator, NULL).isGood ())
                {
                    WriteLog (lsTRACE, SHAMap) << "AddKnownNode fails";
                    fail ("AddKnownNode");
                }
                else
                {
                    pass ();
                }
            }

            gotNodeIDs.clear ();
            gotNodes.clear ();
        }
        while (true);

        destination.clearSynching ();

#ifdef SMS_DEBUG
        WriteLog (lsINFO, SHAMap) << "SYNCHING COMPLETE " << items << " items, " << nodes << " nodes, " <<
                                  bytes / 1024 << " KB";
#endif

        if (!source.deepCompare (destination))
        {
            fail ("Deep Compare");
        }
        else
        {
            pass ();
        }

#ifdef SMS_DEBUG
        WriteLog (lsINFO, SHAMap) << "SHAMapSync test passed: " << items << " items, " <<
                                  passes << " passes, " << nodes << " nodes";
#endif
    }
};

static SHAMapSyncTests shaMapSyncTests;
