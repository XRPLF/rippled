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

namespace ripple {

// This code is used to compare another node's transaction tree
// to our own. It returns a map containing all items that are different
// between two SHA maps. It is optimized not to descend down tree
// branches with the same branch hash. A limit can be passed so
// that we will abort early if a node sends a map to us that
// makes no sense at all. (And our sync algorithm will avoid
// synchronizing matching brances too.)

class SHAMapDeltaNode
{
public:
    SHAMapNode mNodeID;
    uint256 mOurHash, mOtherHash;

    SHAMapDeltaNode (const SHAMapNode& id, uint256 const& ourHash, uint256 const& otherHash) :
        mNodeID (id), mOurHash (ourHash), mOtherHash (otherHash)
    {
        ;
    }
};

bool SHAMap::walkBranch (SHAMapTreeNode* node, SHAMapItem::ref otherMapItem, bool isFirstMap,
                         Delta& differences, int& maxCount)
{
    // Walk a branch of a SHAMap that's matched by an empty branch or single item in the other map
    std::stack<SHAMapTreeNode*> nodeStack;
    nodeStack.push (node);

    bool emptyBranch = !otherMapItem;

    while (!nodeStack.empty ())
    {
        SHAMapTreeNode* node = nodeStack.top ();
        nodeStack.pop ();

        if (node->isInner ())
        {
            // This is an inner node, add all non-empty branches
            for (int i = 0; i < 16; ++i)
                if (!node->isEmptyBranch (i))
                    nodeStack.push (getNodePointer (node->getChildNodeID (i), node->getChildHash (i)));
        }
        else
        {
            // This is a leaf node, process its item
            SHAMapItem::pointer item = node->peekItem ();

            if (!emptyBranch && (otherMapItem->getTag () < item->getTag ()))
            {
                // this item comes after the item from the other map, so add the other item
                if (isFirstMap) // this is first map, so other item is from second
                    differences.insert (std::make_pair (otherMapItem->getTag (),
                                                        DeltaRef (SHAMapItem::pointer (), otherMapItem)));
                else
                    differences.insert (std::make_pair (otherMapItem->getTag (),
                                                        DeltaRef (otherMapItem, SHAMapItem::pointer ())));

                if (--maxCount <= 0)
                    return false;

                emptyBranch = true;
            }

            if (emptyBranch || (item->getTag () != otherMapItem->getTag ()))
            {
                // unmatched
                if (isFirstMap)
                    differences.insert (std::make_pair (item->getTag (), DeltaRef (item, SHAMapItem::pointer ())));
                else
                    differences.insert (std::make_pair (item->getTag (), DeltaRef (SHAMapItem::pointer (), item)));

                if (--maxCount <= 0)
                    return false;
            }
            else
            {
                if (item->peekData () != otherMapItem->peekData ())
                {
                    // non-matching items
                    if (isFirstMap)
                        differences.insert (std::make_pair (otherMapItem->getTag (), DeltaRef (item, otherMapItem)));
                    else
                        differences.insert (std::make_pair (otherMapItem->getTag (), DeltaRef (otherMapItem, item)));

                    if (--maxCount <= 0)
                        return false;
                }

                emptyBranch = true;
            }
        }
    }

    if (!emptyBranch)
    {
        // otherMapItem was unmatched, must add
        if (isFirstMap) // this is first map, so other item is from second
            differences.insert (std::make_pair (otherMapItem->getTag (),
                                                DeltaRef (SHAMapItem::pointer (), otherMapItem)));
        else
            differences.insert (std::make_pair (otherMapItem->getTag (),
                                                DeltaRef (otherMapItem, SHAMapItem::pointer ())));

        if (--maxCount <= 0)
            return false;
    }

    return true;
}

bool SHAMap::compare (SHAMap::ref otherMap, Delta& differences, int maxCount)
{
    // compare two hash trees, add up to maxCount differences to the difference table
    // return value: true=complete table of differences given, false=too many differences
    // throws on corrupt tables or missing nodes
    // CAUTION: otherMap is not locked and must be immutable

    assert (isValid () && otherMap && otherMap->isValid ());

    std::stack<SHAMapDeltaNode> nodeStack; // track nodes we've pushed

    ScopedReadLockType sl (mLock);

    if (getHash () == otherMap->getHash ())
        return true;

    nodeStack.push (SHAMapDeltaNode (SHAMapNode (), getHash (), otherMap->getHash ()));

    while (!nodeStack.empty ())
    {
        SHAMapDeltaNode dNode (nodeStack.top ());
        nodeStack.pop ();

        SHAMapTreeNode* ourNode = getNodePointer (dNode.mNodeID, dNode.mOurHash);
        SHAMapTreeNode* otherNode = otherMap->getNodePointer (dNode.mNodeID, dNode.mOtherHash);

        if (!ourNode || !otherNode)
        {
            assert (false);
            throw SHAMapMissingNode (mType, dNode.mNodeID, uint256 ());
        }

        if (ourNode->isLeaf () && otherNode->isLeaf ())
        {
            // two leaves
            if (ourNode->getTag () == otherNode->getTag ())
            {
                if (ourNode->peekData () != otherNode->peekData ())
                {
                    differences.insert (std::make_pair (ourNode->getTag (),
                                                        DeltaRef (ourNode->peekItem (), otherNode->peekItem ())));

                    if (--maxCount <= 0)
                        return false;
                }
            }
            else
            {
                differences.insert (std::make_pair (ourNode->getTag (),
                                                    DeltaRef (ourNode->peekItem (), SHAMapItem::pointer ())));

                if (--maxCount <= 0)
                    return false;

                differences.insert (std::make_pair (otherNode->getTag (),
                                                    DeltaRef (SHAMapItem::pointer (), otherNode->peekItem ())));

                if (--maxCount <= 0)
                    return false;
            }
        }
        else if (ourNode->isInner () && otherNode->isLeaf ())
        {
            if (!walkBranch (ourNode, otherNode->peekItem (), true, differences, maxCount))
                return false;
        }
        else if (ourNode->isLeaf () && otherNode->isInner ())
        {
            if (!otherMap->walkBranch (otherNode, ourNode->peekItem (), false, differences, maxCount))
                return false;
        }
        else if (ourNode->isInner () && otherNode->isInner ())
        {
            for (int i = 0; i < 16; ++i)
                if (ourNode->getChildHash (i) != otherNode->getChildHash (i))
                {
                    if (otherNode->isEmptyBranch (i))
                    {
                        // We have a branch, the other tree does not
                        SHAMapTreeNode* iNode = getNodePointer (ourNode->getChildNodeID (i), ourNode->getChildHash (i));

                        if (!walkBranch (iNode, SHAMapItem::pointer (), true, differences, maxCount))
                            return false;
                    }
                    else if (ourNode->isEmptyBranch (i))
                    {
                        // The other tree has a branch, we do not
                        SHAMapTreeNode* iNode =
                            otherMap->getNodePointer (otherNode->getChildNodeID (i), otherNode->getChildHash (i));

                        if (!otherMap->walkBranch (iNode, SHAMapItem::pointer (), false, differences, maxCount))
                            return false;
                    }
                    else // The two trees have different non-empty branches
                        nodeStack.push (SHAMapDeltaNode (ourNode->getChildNodeID (i),
                                                         ourNode->getChildHash (i), otherNode->getChildHash (i)));
                }
        }
        else
            assert (false);
    }

    return true;
}

void SHAMap::walkMap (std::vector<SHAMapMissingNode>& missingNodes, int maxMissing)
{
    std::stack<SHAMapTreeNode::pointer> nodeStack;

    ScopedReadLockType sl (mLock);

    if (!root->isInner ())  // root is only node, and we have it
        return;

    nodeStack.push (root);

    while (!nodeStack.empty ())
    {
        SHAMapTreeNode::pointer node = nodeStack.top ();
        nodeStack.pop ();

        for (int i = 0; i < 16; ++i)
            if (!node->isEmptyBranch (i))
            {
                try
                {
                    SHAMapTreeNode::pointer d = getNode (node->getChildNodeID (i), node->getChildHash (i), false);

                    if (d->isInner ())
                        nodeStack.push (d);
                }
                catch (SHAMapMissingNode& n)
                {
                    missingNodes.push_back (n);

                    if (--maxMissing <= 0)
                        return;
                }
            }
    }
}

} // ripple
