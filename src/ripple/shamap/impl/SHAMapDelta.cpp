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
    
namespace ripple {

// This code is used to compare another node's transaction tree
// to our own. It returns a map containing all items that are different
// between two SHA maps. It is optimized not to descend down tree
// branches with the same branch hash. A limit can be passed so
// that we will abort early if a node sends a map to us that
// makes no sense at all. (And our sync algorithm will avoid
// synchronizing matching branches too.)

bool SHAMap::walkBranch (SHAMapTreeNode* node,
                         std::shared_ptr<SHAMapItem> const& otherMapItem, bool isFirstMap,
                         Delta& differences, int& maxCount) const
{
    // Walk a branch of a SHAMap that's matched by an empty branch or single item in the other map
    std::stack <SHAMapTreeNode*, std::vector<SHAMapTreeNode*>> nodeStack;
    nodeStack.push ({node});

    bool emptyBranch = !otherMapItem;

    while (!nodeStack.empty ())
    {
        node = nodeStack.top ();
        nodeStack.pop ();

        if (node->isInner ())
        {
            // This is an inner node, add all non-empty branches
            for (int i = 0; i < 16; ++i)
                if (!node->isEmptyBranch (i))
                    nodeStack.push ({descendThrow (node, i)});
        }
        else
        {
            // This is a leaf node, process its item
            std::shared_ptr<SHAMapItem> item = node->peekItem ();

            if (emptyBranch || (item->getTag () != otherMapItem->getTag ()))
            {
                // unmatched
                if (isFirstMap)
                    differences.insert (std::make_pair (item->getTag (),
                                      DeltaRef (item, std::shared_ptr<SHAMapItem> ())));
                else
                    differences.insert (std::make_pair (item->getTag (),
                                      DeltaRef (std::shared_ptr<SHAMapItem> (), item)));

                if (--maxCount <= 0)
                    return false;
            }
            else if (item->peekData () != otherMapItem->peekData ())
            {
                // non-matching items with same tag
                if (isFirstMap)
                    differences.insert (std::make_pair (item->getTag (),
                                                DeltaRef (item, otherMapItem)));
                else
                    differences.insert (std::make_pair (item->getTag (),
                                                DeltaRef (otherMapItem, item)));

                if (--maxCount <= 0)
                    return false;

                emptyBranch = true;
            }
            else
            {
                // exact match
                emptyBranch = true;
            }
        }
    }

    if (!emptyBranch)
    {
        // otherMapItem was unmatched, must add
        if (isFirstMap) // this is first map, so other item is from second
            differences.insert (std::make_pair (otherMapItem->getTag (),
                                                DeltaRef (std::shared_ptr<SHAMapItem>(),
                                                          otherMapItem)));
        else
            differences.insert (std::make_pair (otherMapItem->getTag (),
                                                DeltaRef (otherMapItem,
                                                      std::shared_ptr<SHAMapItem> ())));

        if (--maxCount <= 0)
            return false;
    }

    return true;
}

bool
SHAMap::compare (std::shared_ptr<SHAMap> const& otherMap,
                 Delta& differences, int maxCount) const
{
    // compare two hash trees, add up to maxCount differences to the difference table
    // return value: true=complete table of differences given, false=too many differences
    // throws on corrupt tables or missing nodes
    // CAUTION: otherMap is not locked and must be immutable

    assert (isValid () && otherMap && otherMap->isValid ());

    using StackEntry = std::pair <SHAMapTreeNode*, SHAMapTreeNode*>;
    std::stack <StackEntry, std::vector<StackEntry>> nodeStack; // track nodes we've pushed

    if (getHash () == otherMap->getHash ())
        return true;

    nodeStack.push ({root_.get(), otherMap->root_.get()});
    while (!nodeStack.empty ())
    {
        SHAMapTreeNode* ourNode = nodeStack.top().first;
        SHAMapTreeNode* otherNode = nodeStack.top().second;
        nodeStack.pop ();

        if (!ourNode || !otherNode)
        {
            assert (false);
            throw SHAMapMissingNode (type_, uint256 ());
        }

        if (ourNode->isLeaf () && otherNode->isLeaf ())
        {
            // two leaves
            if (ourNode->peekItem()->getTag () == otherNode->peekItem()->getTag ())
            {
                if (ourNode->peekItem()->peekData () != otherNode->peekItem()->peekData ())
                {
                    differences.insert (std::make_pair (ourNode->peekItem()->getTag (),
                                                 DeltaRef (ourNode->peekItem (),
                                                 otherNode->peekItem ())));
                    if (--maxCount <= 0)
                        return false;
                }
            }
            else
            {
                differences.insert (std::make_pair(ourNode->peekItem()->getTag (),
                                                   DeltaRef(ourNode->peekItem(),
                                                   std::shared_ptr<SHAMapItem> ())));
                if (--maxCount <= 0)
                    return false;

                differences.insert(std::make_pair(otherNode->peekItem()->getTag (),
                                                  DeltaRef(std::shared_ptr<SHAMapItem>(),
                                                  otherNode->peekItem ())));
                if (--maxCount <= 0)
                    return false;
            }
        }
        else if (ourNode->isInner () && otherNode->isLeaf ())
        {
            if (!walkBranch (ourNode, otherNode->peekItem (),
                    true, differences, maxCount))
                return false;
        }
        else if (ourNode->isLeaf () && otherNode->isInner ())
        {
            if (!otherMap->walkBranch (otherNode, ourNode->peekItem (),
                                       false, differences, maxCount))
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
                        SHAMapTreeNode* iNode = descendThrow (ourNode, i);
                        if (!walkBranch (iNode,
                                         std::shared_ptr<SHAMapItem> (), true,
                                         differences, maxCount))
                            return false;
                    }
                    else if (ourNode->isEmptyBranch (i))
                    {
                        // The other tree has a branch, we do not
                        SHAMapTreeNode* iNode =
                            otherMap->descendThrow(otherNode, i);
                        if (!otherMap->walkBranch (iNode,
                                                   std::shared_ptr<SHAMapItem>(),
                                                   false, differences, maxCount))
                            return false;
                    }
                    else // The two trees have different non-empty branches
                        nodeStack.push ({descendThrow (ourNode, i),
                                        otherMap->descendThrow (otherNode, i)});
                }
        }
        else
            assert (false);
    }

    return true;
}

void SHAMap::walkMap (std::vector<SHAMapMissingNode>& missingNodes, int maxMissing) const
{
    std::stack <std::shared_ptr<SHAMapTreeNode>,
        std::vector <std::shared_ptr<SHAMapTreeNode>>> nodeStack;

    if (!root_->isInner ())  // root_ is only node, and we have it
        return;

    nodeStack.push (root_);

    while (!nodeStack.empty ())
    {
        std::shared_ptr<SHAMapTreeNode> node = std::move (nodeStack.top());
        nodeStack.pop ();

        for (int i = 0; i < 16; ++i)
        {
            if (!node->isEmptyBranch (i))
            {
                std::shared_ptr<SHAMapTreeNode> nextNode = descendNoStore (node, i);

                if (nextNode)
                {
                    if (nextNode->isInner ())
                        nodeStack.push (std::move (nextNode));
                }
                else
                {
                    missingNodes.emplace_back (type_, node->getChildHash (i));
                    if (--maxMissing <= 0)
                        return;
                }
            }
        }
    }
}

} // ripple
