#include "SHAMap.h"

#include <stack>

// This code is used to compare another node's transaction tree
// to our own. It returns a map containing all items that are different
// between two SHA maps. It is optimized not to descend down tree
// branches with the same branch hash. A limit can be passed so
// that we will abort early if a node sends a map to us that
// makes no sense at all. (And our sync algorithm will avoid
// synchronizing matching brances too.)

class SHAMapDiffNode
{
	public:
	SHAMapNode mNodeID;
	uint256 mOurHash, mOtherHash;
	
	SHAMapDiffNode(SHAMapNode id, const uint256& ourHash, const uint256& otherHash) :
		mNodeID(id), mOurHash(ourHash), mOtherHash(otherHash) { ; }
};

bool SHAMap::walkBranch(SHAMapTreeNode::pointer node, SHAMapItem::pointer otherMapItem, bool isFirstMap,
	SHAMapDiff& differences, int& maxCount)
{
	// Walk a branch of a SHAMap that's matched by an empty branch or single item in the other map
	std::stack<SHAMapTreeNode::pointer> nodeStack;
	nodeStack.push(node);
	
	while(!nodeStack.empty())
	{
		SHAMapTreeNode::pointer node=nodeStack.top();
		nodeStack.pop();
		if(node->isInner())
		{ // This is an inner node, add all non-empty branches
			for(int i=0; i<16; i++)
				if(!node->isEmptyBranch(i))
				{
					SHAMapTreeNode::pointer newNode=getNode(node->getChildNodeID(i), node->getChildHash(i), false);
					if(!newNode) throw SHAMapException(MissingNode);
					nodeStack.push(newNode);
				}
		}
		else
		{ // This is a leaf node, process its item
			SHAMapItem::pointer item=node->peekItem();

			if(otherMapItem && otherMapItem->getTag()<item->getTag())
			{ // this item comes after the item from the other map, so add the other item
				if(isFirstMap) // this is first map, so other item is from second
					differences.insert(std::make_pair(otherMapItem->getTag(),
						std::make_pair(SHAMapItem::pointer(), otherMapItem)));
				else
					differences.insert(std::make_pair(otherMapItem->getTag(),
						std::make_pair(otherMapItem, SHAMapItem::pointer())));
				if((--maxCount)<=0) return false;
				otherMapItem=SHAMapItem::pointer();
			}

			if( (!otherMapItem) || (item->getTag()<otherMapItem->getTag()) )
			{ // unmatched
				if(isFirstMap)
					differences.insert(std::make_pair(item->getTag(), std::make_pair(item, SHAMapItem::pointer())));
				else
					differences.insert(std::make_pair(item->getTag(), std::make_pair(SHAMapItem::pointer(), item)));
				if((--maxCount)<=0) return false;
			}
			else if(item->getTag()==otherMapItem->getTag())
			{
				if(item->getData()!=otherMapItem->getData())
				{ // non-matching items
					if(isFirstMap)
						differences.insert(std::make_pair(otherMapItem->getTag(),
							std::make_pair(item, otherMapItem)));
					else
						differences.insert(std::make_pair(otherMapItem->getTag(),
							std::make_pair(otherMapItem, item)));
					if((--maxCount)<=0) return false;
					item=SHAMapItem::pointer();
				}
			}
			else assert(false);
		}
	}
	
	if(otherMapItem)
	{ // otherMapItem was unmatched, must add
			if(isFirstMap) // this is first map, so other item is from second
				differences.insert(std::make_pair(otherMapItem->getTag(),
					std::make_pair(SHAMapItem::pointer(), otherMapItem)));
			else
				differences.insert(std::make_pair(otherMapItem->getTag(),
					std::make_pair(otherMapItem, SHAMapItem::pointer())));
			if((--maxCount)<=0) return false;
	}
	
	return true;
}

bool SHAMap::compare(SHAMap::pointer otherMap, SHAMapDiff& differences, int maxCount)
{   // compare two hash trees, add up to maxCount differences to the difference table
	// return value: true=complete table of differences given, false=too many differences
	// throws on corrupt tables or missing nodes



#if 0
// FIXME: Temporarily disabled

	std::stack<SHAMapDiffNode> nodeStack; // track nodes we've pushed
	nodeStack.push(SHAMapDiffNode(SHAMapNode(), getHash(), otherMap->getHash()));

	ScopedLock sl(Lock());
 	while(!nodeStack.empty())
 	{
 		SHAMapDiffNode node(nodeStack.top());
 		nodeStack.pop();
 		if(node.mOurHash!=node.mOtherHash)
 		{
		 	if(node.mNodeID.isLeaf())
		 	{
		 		if(!node.mOurHash)
		 		{ // leaf only in our tree
			 		SHAMapLeafNode::pointer thisNode=getLeaf(node.mNodeID, node.mOurHash, false);
			 		for(SHAMapItem::pointer item=thisNode->firstItem(); item; item=thisNode->nextItem(item->getTag()))
			 		{ // items in leaf only in our tree
			 			differences.insert(std::make_pair(item->getTag(),
			 				std::make_pair(item, SHAMapItem::pointer())));
						if((--maxCount)<=0) return false;
			 		}
		 		}
		 		else if(!node.mOtherHash)
		 		{ // leaf only in other tree
			 		SHAMapLeafNode::pointer otherNode=otherMap->getLeaf(node.mNodeID, node.mOtherHash, false);
			 		for(SHAMapItem::pointer item=otherNode->firstItem(); item; item=otherNode->nextItem(item->getTag()))
			 		{ // items in leaf only in our tree
			 			differences.insert(std::make_pair(item->getTag(),
			 				std::make_pair(SHAMapItem::pointer(), item)));
						if((--maxCount)<=0) return false;
			 		}
		 		}
		 		else
		 		{ // leaf in both trees, but differs
			 		SHAMapLeafNode::pointer thisNode=getLeaf(node.mNodeID, node.mOurHash, false);
			 		SHAMapLeafNode::pointer otherNode=otherMap->getLeaf(node.mNodeID, node.mOtherHash, false);
			 		SHAMapItem::pointer ourItem=thisNode->firstItem();
			 		SHAMapItem::pointer otherMapItem=otherNode->firstItem();
			 		while(ourItem || otherMapItem)
			 		{
			 			if(!otherMapItem)
			 			{ // we have items, other tree does not
			 				differences.insert(std::make_pair(ourItem->getTag(),
			 					std::make_pair(ourItem, otherMapItem)));
							if((--maxCount)<=0) return false;
							otherMapItem=otherNode->nextItem(otherMapItem->getTag());
			 			}
			 			else if(!ourItem)
			 			{ // we have no items, other tree does
			 				differences.insert(std::make_pair(otherMapItem->getTag(),
			 					std::make_pair(ourItem, otherMapItem)));
							if((--maxCount)<=0) return false;
							otherMapItem=thisNode->nextItem(otherMapItem->getTag());
			 			}
			 			else if(ourItem->getTag()==otherMapItem->getTag())
			 			{ // we have items with the same tag
			 				if(ourItem->getData()!=otherMapItem->getData())
			 				{ // different data
			 					differences.insert(std::make_pair(ourItem->getTag(),
			 						std::make_pair(ourItem, otherMapItem)));
								if((--maxCount)<=0) return false;
			 				}
							ourItem=thisNode->nextItem(ourItem->getTag());
							otherMapItem=otherNode->nextItem(otherMapItem->getTag());
			 			}
			 			else if(ourItem->getTag()<otherMapItem->getTag())
			 			{ // our item comes first
			 				differences.insert(std::make_pair(ourItem->getTag(),
			 					std::make_pair(ourItem, SHAMapItem::pointer())));
							if((--maxCount)<=0) return false;
							ourItem=thisNode->nextItem(ourItem->getTag());
			 			}
			 			else
			 			{ // other item comes first
			 				differences.insert(std::make_pair(otherMapItem->getTag(),
			 					std::make_pair(SHAMapItem::pointer(), otherMapItem)));
							if((--maxCount)<=0) return false;
							otherMapItem=otherNode->nextItem(otherMapItem->getTag());
			 			}
			 		}
				}
			}
		 	else
		 	{ // inner node different in two trees
		 		if(!node.mOurHash)
		 		{ // node only exist in our tree
			 		SHAMapInnerNode::pointer thisNode=getInner(node.mNodeID, node.mOurHash, false);
			 		for(int i=0; i<32; i++)
			 		{ // push all existing branches onto the stack
			 			if(!!thisNode->getChildHash(i))
			 				nodeStack.push(SHAMapDiffNode(thisNode->getChildNodeID(i),
			 					thisNode->getChildHash(i), uint256()));
			 		}
		 		}
		 		else if(!node.mOtherHash)
		 		{ // node only exists in other tree
			 		SHAMapInnerNode::pointer otherNode=otherMap->getInner(node.mNodeID, node.mOtherHash, false);
			 		for(int i=0; i<32; i++)
			 		{ // push all existing branches onto the stack
			 			if(!!otherNode->getChildHash(i))
			 				nodeStack.push(SHAMapDiffNode(otherNode->getChildNodeID(i),
			 					uint256(), otherNode->getChildHash(i)));
			 		}
		 		}
		 		else
		 		{ // node in both trees, but differs
			 		SHAMapInnerNode::pointer thisNode=getInner(node.mNodeID, node.mOurHash, false);
			 		SHAMapInnerNode::pointer otherNode=otherMap->getInner(node.mNodeID, node.mOtherHash, false);
			 		for(int i=0; i<32; i++)
			 		{ // push all differing branches onto the stack
			 			if(thisNode->getChildHash(i)!=otherNode->getChildHash(i))
			 				nodeStack.push(SHAMapDiffNode(thisNode->getChildNodeID(i),
			 					thisNode->getChildHash(i), otherNode->getChildHash(i)));
			 		}
				}
			}
		}
	}

#endif

	return true;
}
