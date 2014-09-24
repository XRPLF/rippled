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

namespace ripple {

// VFALCO TODO tidy up this global

static const uint256 uZero;

static void visitLeavesHelper (
    std::function <void (SHAMapItem::ref)> const& function,
    SHAMapTreeNode& node)
{
    // Adapt visitNodes to visitLeaves
    if (!node.isInner ())
        function (node.peekItem ());
}

void SHAMap::visitLeaves (std::function<void (SHAMapItem::ref item)> const& leafFunction)
{
    visitNodes (std::bind (visitLeavesHelper,
            std::cref (leafFunction), std::placeholders::_1));
}

void SHAMap::visitNodes(std::function<void (SHAMapTreeNode&)> const& function)
{
    // Visit every node in a SHAMap
    assert (root->isValid ());

    if (!root || root->isEmpty ())
        return;

    function (*root);

    if (!root->isInner ())
        return;

    std::stack <std::pair <int, SHAMapTreeNode::pointer> > stack;
    SHAMapTreeNode::pointer node = root;
    int pos = 0;

    while (1)
    {
        while (pos < 16)
        {
            uint256 childHash;
            if (!node->isEmptyBranch (pos))
            {
                SHAMapTreeNode::pointer child = descendNoStore (node, pos);
                function (*child);

                if (child->isLeaf ())
                    ++pos;
                else
                {
                    // If there are no more children, don't push this node
                    while ((pos != 15) && (node->isEmptyBranch (pos + 1)))
                           ++pos;

                    if (pos != 15)
                    {
                        // save next position to resume at
                        stack.push (std::make_pair(pos + 1, std::move (node)));
                    }

                    // descend to the child's first position
                    node = child;
                    pos = 0;
                }
            }
            else
            {
                ++pos; // move to next position
            }
        }

        if (stack.empty ())
            break;

        std::tie(pos, node) = stack.top ();
        stack.pop ();
    }
}

/** Get a list of node IDs and hashes for nodes that are part of this SHAMap
    but not available locally.  The filter can hold alternate sources of
    nodes that are not permanently stored locally
*/
void SHAMap::getMissingNodes (std::vector<SHAMapNodeID>& nodeIDs, std::vector<uint256>& hashes, int max,
                              SHAMapSyncFilter* filter)
{
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

    int const maxDefer = getApp().getNodeStore().getDesiredAsyncReadCount ();

    // Track the missing hashes we have found so far
    std::set <uint256> missingHashes;


    while (1)
    {
        std::vector <std::tuple <SHAMapTreeNode*, int, SHAMapNodeID>> deferredReads;
        deferredReads.reserve (maxDefer + 16);

        std::stack <std::tuple<SHAMapTreeNode*, SHAMapNodeID, int, int, bool>>
                                                                          stack;
        // Traverse the map without blocking

        SHAMapTreeNode *node = root.get ();
        SHAMapNodeID nodeID;

        // The firstChild value is selected randomly so if multiple threads
        // are traversing the map, each thread will start at a different
        // (randomly selected) inner node.  This increases the likelihood
        // that the two threads will produce different request sets (which is
        // more efficient than sending identical requests).
        int firstChild = rand() % 256;
        int currentChild = 0;
        bool fullBelow = true;

        do
        {
            while (currentChild < 16)
            {
                int branch = (firstChild + currentChild++) % 16;
                if (!node->isEmptyBranch (branch))
                {
                    uint256 const& childHash = node->getChildHash (branch);

                    if (! mBacked || ! m_fullBelowCache.touch_if_exists (childHash))
                    {
                        SHAMapNodeID childID = nodeID.getChildNodeID (branch);
                        bool pending = false;
                        SHAMapTreeNode* d = descendAsync (node, branch, childID, filter, pending);

                        if (!d)
                        {
                            if (!pending)
                            { // node is not in the database
                                if (missingHashes.insert (childHash).second)
                                {
                                    nodeIDs.push_back (childID);
                                    hashes.push_back (childHash);

                                    if (--max <= 0)
                                        return;
                                }
                            }
                            else
                            {
                                // read is deferred
                                deferredReads.emplace_back (node, branch, childID);
                            }

                            fullBelow = false; // This node is not known full below
                        }
                        else if (d->isInner () && !d->isFullBelow ())
                        {
                            stack.push (std::make_tuple (node, nodeID,
                                          firstChild, currentChild, fullBelow));

                            // Switch to processing the child node
                            node = d;
                            nodeID = childID;
                            firstChild = rand() % 256;
                            currentChild = 0;
                            fullBelow = true;
                        }
                    }
                }
            }

            // We are done with this inner node (and thus all of its children)

            if (fullBelow)
            { // No partial node encountered below this node
                node->setFullBelow ();
                if (mBacked)
                    m_fullBelowCache.insert (node->getNodeHash ());
            }

            if (stack.empty ())
                node = nullptr; // Finished processing the last node, we are done
            else
            { // Pick up where we left off (above this node)
                bool was;
                std::tie(node, nodeID, firstChild, currentChild, was) = stack.top ();
                fullBelow = was && fullBelow; // was and still is
                stack.pop ();
            }

        }
        while ((node != nullptr) && (deferredReads.size () <= maxDefer));

        // If we didn't defer any reads, we're done
        if (deferredReads.empty ())
            break;

        getApp().getNodeStore().waitReads();

        // Process all deferred reads
        for (auto const& node : deferredReads)
        {
            auto parent = std::get<0>(node);
            auto branch = std::get<1>(node);
            auto const& nodeID = std::get<2>(node);
            auto const& nodeHash = parent->getChildHash (branch);

            SHAMapTreeNode::pointer nodePtr = fetchNodeNT (nodeID, nodeHash, filter);
            if (nodePtr)
            {
                if (mBacked)
                    canonicalize (nodeHash, nodePtr);
                parent->canonicalizeChild (branch, nodePtr);
            }
            else if (missingHashes.insert (nodeHash).second)
            {
                nodeIDs.push_back (nodeID);
                hashes.push_back (nodeHash);

                if (--max <= 0)
                    return;
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

    std::vector<SHAMapNodeID> nodeIDs;
    nodeIDs.reserve(max);

    getMissingNodes(nodeIDs, nodeHashes, max, filter);
    return nodeHashes;
}

bool SHAMap::getNodeFat (SHAMapNodeID wanted, std::vector<SHAMapNodeID>& nodeIDs,
                         std::list<Blob >& rawNodes, bool fatRoot, bool fatLeaves)
{
    // Gets a node and some of its children

    SHAMapTreeNode* node = root.get ();

    SHAMapNodeID nodeID;

    while (node && node->isInner () && (nodeID.getDepth() < wanted.getDepth()))
        node = descendThrow (node, nodeID, nodeID.selectBranch (wanted.getNodeID()));

    if (!node || (nodeID != wanted))
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
    bool skipNode = false;
    do
    {

        if (skipNode)
            skipNode = false;
        else
        {
            Serializer s;
            node->addRaw (s, snfWIRE);
            nodeIDs.push_back (wanted);
            rawNodes.push_back (std::move (s.peekData ()));
        }

        if ((!fatRoot && wanted.isRoot ()) || node->isLeaf ()) // don't get a fat root, can't get a fat leaf
            return true;

        SHAMapTreeNode* nextNode = nullptr;
        SHAMapNodeID nextNodeID;

        count = 0;
        for (int i = 0; i < 16; ++i)
        {
            if (!node->isEmptyBranch (i))
            {
                SHAMapNodeID nextNodeID = wanted.getChildNodeID (i);
                nextNode = descendThrow (node, i);
                ++count;
                if (fatLeaves || nextNode->isInner ())
                {
                    Serializer s;
                    nextNode->addRaw (s, snfWIRE);
                    nodeIDs.push_back (nextNodeID);
                    rawNodes.push_back (std::move (s.peekData ()));
                    skipNode = true; // Don't add this node again if we loop
                }
            }
        }

        node = nextNode;
        wanted = nextNodeID;

    // So long as there's exactly one inner node, we take it
    } while ((count == 1) && node->isInner());

    return true;
}

bool SHAMap::getRootNode (Serializer& s, SHANodeFormat format)
{
    root->addRaw (s, format);
    return true;
}

SHAMapAddNode SHAMap::addRootNode (Blob const& rootNode, SHANodeFormat format,
                                   SHAMapSyncFilter* filter)
{
    // we already have a root node
    if (root->getNodeHash ().isNonZero ())
    {
        WriteLog (lsTRACE, SHAMap) << "got root node, already have one";
        return SHAMapAddNode::duplicate ();
    }

    assert (mSeq >= 1);
    SHAMapTreeNode::pointer node =
        std::make_shared<SHAMapTreeNode> (rootNode, mSeq,
                                          format, uZero, false);

    if (!node)
        return SHAMapAddNode::invalid ();

#ifdef BEAST_DEBUG
    node->dump (SHAMapNodeID ());
#endif

    if (mBacked)
        canonicalize (node->getNodeHash (), node);

    root = node;

    if (root->isLeaf())
        clearSynching ();

    if (filter)
    {
        Serializer s;
        root->addRaw (s, snfPREFIX);
        filter->gotNode (false, SHAMapNodeID{}, root->getNodeHash (),
                         s.modData (), root->getType ());
    }

    return SHAMapAddNode::useful ();
}

SHAMapAddNode SHAMap::addRootNode (uint256 const& hash, Blob const& rootNode, SHANodeFormat format,
                                   SHAMapSyncFilter* filter)
{
    // we already have a root node
    if (root->getNodeHash ().isNonZero ())
    {
        WriteLog (lsTRACE, SHAMap) << "got root node, already have one";
        assert (root->getNodeHash () == hash);
        return SHAMapAddNode::duplicate ();
    }

    assert (mSeq >= 1);
    SHAMapTreeNode::pointer node =
        std::make_shared<SHAMapTreeNode> (rootNode, mSeq,
                                          format, uZero, false);

    if (!node || node->getNodeHash () != hash)
        return SHAMapAddNode::invalid ();

    if (mBacked)
        canonicalize (hash, node);

    root = node;

    if (root->isLeaf())
        clearSynching ();

    if (filter)
    {
        Serializer s;
        root->addRaw (s, snfPREFIX);
        filter->gotNode (false, SHAMapNodeID{}, root->getNodeHash (), s.modData (),
                         root->getType ());
    }

    return SHAMapAddNode::useful ();
}

SHAMapAddNode
SHAMap::addKnownNode (const SHAMapNodeID& node, Blob const& rawNode,
                      SHAMapSyncFilter* filter)
{
    // return value: true=okay, false=error
    assert (!node.isRoot ());

    if (!isSynching ())
    {
        WriteLog (lsTRACE, SHAMap) << "AddKnownNode while not synching";
        return SHAMapAddNode::duplicate ();
    }

    SHAMapNodeID iNodeID;
    SHAMapTreeNode* iNode = root.get ();

    while (iNode->isInner () && !iNode->isFullBelow () &&
           (iNodeID.getDepth () < node.getDepth ()))
    {
        int branch = iNodeID.selectBranch (node.getNodeID ());
        assert (branch >= 0);

        if (iNode->isEmptyBranch (branch))
        {
            WriteLog (lsWARNING, SHAMap) << "Add known node for empty branch"
                                         << node;
            return SHAMapAddNode::invalid ();
        }

        uint256 childHash = iNode->getChildHash (branch);
        if (m_fullBelowCache.touch_if_exists (childHash))
            return SHAMapAddNode::duplicate ();

        SHAMapTreeNode* prevNode = iNode;
        std::tie (iNode, iNodeID) = descend (iNode, iNodeID, branch, filter);

        if (!iNode)
        {
            if (iNodeID != node)
            {
                // Either this node is broken or we didn't request it (yet)
                WriteLog (lsWARNING, SHAMap) << "unable to hook node " << node;
                WriteLog (lsINFO, SHAMap) << " stuck at " << iNodeID;
                WriteLog (lsINFO, SHAMap) << "got depth=" << node.getDepth ()
                                          << ", walked to= "
                                          << iNodeID.getDepth ();
                return SHAMapAddNode::invalid ();
            }

            SHAMapTreeNode::pointer newNode =
                std::make_shared<SHAMapTreeNode> (rawNode, 0, snfWIRE,
                                                  uZero, false);

            if (!newNode->isInBounds (iNodeID))
            {
                // Map is provably invalid
                mState = smsInvalid;
                return SHAMapAddNode::useful ();
            }

            if (childHash != newNode->getNodeHash ())
            {
                WriteLog (lsWARNING, SHAMap) << "Corrupt node received";
                return SHAMapAddNode::invalid ();
            }

            if (mBacked)
                canonicalize (childHash, newNode);

            prevNode->canonicalizeChild (branch, newNode);

            if (filter)
            {
                Serializer s;
                newNode->addRaw (s, snfPREFIX);
                filter->gotNode (false, node, childHash,
                                 s.modData (), newNode->getType ());
            }

            return SHAMapAddNode::useful ();
        }
    }

    WriteLog (lsTRACE, SHAMap) << "got node, already had it (late)";
    return SHAMapAddNode::duplicate ();
}

bool SHAMap::deepCompare (SHAMap& other)
{
    // Intended for debug/test only
    std::stack <std::pair <SHAMapTreeNode*, SHAMapTreeNode*> > stack;

    stack.push ({root.get(), other.root.get()});

    while (!stack.empty ())
    {
        SHAMapTreeNode *node, *otherNode;
        std::tie(node, otherNode) = stack.top ();
        stack.pop ();

        if (!node || !otherNode)
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

        if (node->isLeaf ())
        {
            if (!otherNode->isLeaf ())
                 return false;
            auto nodePeek = node->peekItem();
            auto otherNodePeek = otherNode->peekItem();
            if (nodePeek->getTag() != otherNodePeek->getTag())
                return false;
            if (nodePeek->peekData() != otherNodePeek->peekData())
                return false;
        }
        else if (node->isInner ())
        {
            if (!otherNode->isInner ())
                return false;

            for (int i = 0; i < 16; ++i)
            {
                if (node->isEmptyBranch (i))
                {
                    if (!otherNode->isEmptyBranch (i))
                        return false;
                }
                else
                {
                    if (otherNode->isEmptyBranch (i))
                       return false;

                    SHAMapTreeNode *next = descend (node, i);
                    SHAMapTreeNode *otherNext = other.descend (otherNode, i);
                    if (!next || !otherNext)
                    {
                        WriteLog (lsWARNING, SHAMap) << "unable to fetch inner node";
                        return false;
                    }
                    stack.push ({next, otherNext});
                }
            }
        }
    }

    return true;
}

/** Does this map have this inner node?
*/
bool
SHAMap::hasInnerNode (SHAMapNodeID const& targetNodeID,
                      uint256 const& targetNodeHash)
{
    SHAMapTreeNode* node = root.get ();
    SHAMapNodeID nodeID;

    while (node->isInner () && (nodeID.getDepth () < targetNodeID.getDepth ()))
    {
        int branch = nodeID.selectBranch (targetNodeID.getNodeID ());

        if (node->isEmptyBranch (branch))
            return false;

        node = descendThrow (node, nodeID, branch);
    }

    return (node->isInner()) && (node->getNodeHash() == targetNodeHash);
}

/** Does this map have this leaf node?
*/
bool SHAMap::hasLeafNode (uint256 const& tag, uint256 const& targetNodeHash)
{
    SHAMapTreeNode* node = root.get ();
    SHAMapNodeID nodeID;

    if (!node->isInner()) // only one leaf node in the tree
        return node->getNodeHash() == targetNodeHash;

    do
    {
        int branch = nodeID.selectBranch (tag);

        if (node->isEmptyBranch (branch))
            return false;   // Dead end, node must not be here

        if (node->getChildHash (branch) == targetNodeHash) // Matching leaf, no need to retrieve it
            return true;

        node = descendThrow (node, nodeID, branch);
    }
    while (node->isInner());

    return false; // If this was a matching leaf, we would have caught it already
}

/**
@param have A pointer to the map that the recipient already has (if any).
@param includeLeaves True if leaf nodes should be included.
@param max The maximum number of nodes to return.
@param func The functor to call for each node added to the FetchPack.

Note: a caller should set includeLeaves to false for transaction trees.
There's no point in including the leaves of transaction trees.
*/
void SHAMap::getFetchPack (SHAMap* have, bool includeLeaves, int max,
                           std::function<void (uint256 const&, const Blob&)> func)
{
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
            func (std::cref(root->getNodeHash ()), std::cref(s.peekData ()));
            --max;
        }

        return;
    }
    // contains unexplored non-matching inner node entries
    std::stack<std::pair<SHAMapTreeNode*, SHAMapNodeID>> stack;
    stack.push ({root.get(), SHAMapNodeID{}});

    while (!stack.empty() && (max > 0))
    {
        SHAMapTreeNode* node;
        SHAMapNodeID nodeID;
        std::tie (node, nodeID) = stack.top ();
        stack.pop ();

        // 1) Add this node to the pack
        Serializer s;
        node->addRaw (s, snfPREFIX);
        func (std::cref(node->getNodeHash ()), std::cref(s.peekData ()));
        --max;

        // 2) push non-matching child inner nodes
        for (int i = 0; i < 16; ++i)
        {
            if (!node->isEmptyBranch (i))
            {
                uint256 const& childHash = node->getChildHash (i);
                SHAMapNodeID childID = nodeID.getChildNodeID (i);
                SHAMapTreeNode* next = descendThrow (node, i);

                if (next->isInner ())
                {
                    if (!have || !have->hasInnerNode (childID, childHash))
                        stack.push ({next, childID});
                }
                else if (includeLeaves && (!have || !have->hasLeafNode (next->getTag(), childHash)))
                {
                    Serializer s;
                    next->addRaw (s, snfPREFIX);
                    func (std::cref(childHash), std::cref(s.peekData ()));
                    --max;
                }
            }
        }
    }
}

std::list<Blob > SHAMap::getTrustedPath (uint256 const& index)
{
    std::stack<std::pair<SHAMapTreeNode::pointer, SHAMapNodeID>> stack =
                                                        getStack (index, false);
    if (stack.empty () || !stack.top ().first->isLeaf ())
        throw std::runtime_error ("requested leaf not present");

    std::list< Blob > path;
    Serializer s;

    while (!stack.empty ())
    {
        stack.top ().first->addRaw (s, snfWIRE);
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

class SHAMapSync_test : public beast::unit_test::suite
{
public:
    static SHAMapItem::pointer makeRandomAS ()
    {
        Serializer s;

        for (int d = 0; d < 3; ++d) s.add32 (rand ());

        return std::make_shared<SHAMapItem> (to256 (s.getRIPEMD160 ()), s.peekData ());
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
                log <<
                    "Unable to add item to map";
                assert (false);
                return false;
            }
        }

        for (std::list<uint256>::iterator it = items.begin (); it != items.end (); ++it)
        {
            if (!map.delItem (*it))
            {
                log <<
                    "Unable to remove item from map";
                assert (false);
                return false;
            }
        }

        if (beforeHash != map.getHash ())
        {
            log <<
                "Hashes do not match " << beforeHash << " " << map.getHash ();
            assert (false);
            return false;
        }

        return true;
    }

    void run ()
    {
        unsigned int seed;

        // VFALCO TODO Replace this with beast::Random
        RAND_pseudo_bytes (reinterpret_cast<unsigned char*> (&seed), sizeof (seed));
        srand (seed);

        beast::manual_clock <std::chrono::seconds> clock;  // manual advance clock
        beast::Journal const j;                            // debug journal

        FullBelowCache fullBelowCache ("test.full_below", clock);
        TreeNodeCache treeNodeCache ("test.tree_node_cache", 65536, 60, clock, j);

        SHAMap source (smtFREE, fullBelowCache, treeNodeCache);
        SHAMap destination (smtFREE, fullBelowCache, treeNodeCache);

        int items = 10000;
        for (int i = 0; i < items; ++i)
            source.addItem (*makeRandomAS (), false, false);

        unexpected (!confuseMap (source, 500), "ConfuseMap");

        source.setImmutable ();

        std::vector<SHAMapNodeID> nodeIDs, gotNodeIDs;
        std::list< Blob > gotNodes;
        std::vector<uint256> hashes;

        std::vector<SHAMapNodeID>::iterator nodeIDIterator;
        std::list< Blob >::iterator rawNodeIterator;

        int passes = 0;
        int nodes = 0;

        destination.setSynching ();

        unexpected (!source.getNodeFat (SHAMapNodeID (), nodeIDs, gotNodes, (rand () % 2) == 0, (rand () % 2) == 0),
            "GetNodeFat");

        unexpected (gotNodes.size () < 1, "NodeSize");

        unexpected (!destination.addRootNode (*gotNodes.begin (), snfWIRE, nullptr).isGood(), "AddRootNode");

        nodeIDs.clear ();
        gotNodes.clear ();

#ifdef SMS_DEBUG
        int bytes = 0;
#endif

        do
        {
            ++clock;
            ++passes;
            hashes.clear ();

            // get the list of nodes we know we need
            destination.getMissingNodes (nodeIDs, hashes, 2048, nullptr);

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

                if (!destination.addKnownNode (*nodeIDIterator, *rawNodeIterator, nullptr).isGood ())
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

BEAST_DEFINE_TESTSUITE(SHAMapSync,ripple_app,ripple);

} // ripple
