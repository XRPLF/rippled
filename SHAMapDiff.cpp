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

bool SHAMap::compare(SHAMap::pointer otherMap, SHAMapDiff& differences, int maxCount)
{ // compare two hash trees, add up to maxCount differences to the difference table
  // return value: true=complete table of differences given, false=too many differences
  // throws on corrupt tables or missing nodes

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
			 		SHAMapItem::pointer otherItem=otherNode->firstItem();
			 		while(ourItem || otherItem)
			 		{
			 			if(!otherItem)
			 			{ // we have items, other tree does not
			 				differences.insert(std::make_pair(ourItem->getTag(),
			 					std::make_pair(ourItem, otherItem)));
							if((--maxCount)<=0) return false;
							otherItem=otherNode->nextItem(otherItem->getTag());
			 			}
			 			else if(!ourItem)
			 			{ // we have no items, other tree does
			 				differences.insert(std::make_pair(otherItem->getTag(),
			 					std::make_pair(ourItem, otherItem)));
							if((--maxCount)<=0) return false;
							otherItem=thisNode->nextItem(otherItem->getTag());
			 			}
			 			else if(ourItem->getTag()==otherItem->getTag())
			 			{ // we have items with the same tag
			 				if(ourItem->getData()!=otherItem->getData())
			 				{ // different data
			 					differences.insert(std::make_pair(ourItem->getTag(),
			 						std::make_pair(ourItem, otherItem)));
								if((--maxCount)<=0) return false;
			 				}
							ourItem=thisNode->nextItem(ourItem->getTag());
							otherItem=otherNode->nextItem(otherItem->getTag());
			 			}
			 			else if(ourItem->getTag()<otherItem->getTag())
			 			{ // our item comes first
			 				differences.insert(std::make_pair(ourItem->getTag(),
			 					std::make_pair(ourItem, SHAMapItem::pointer())));
							if((--maxCount)<=0) return false;
							ourItem=thisNode->nextItem(ourItem->getTag());
			 			}
			 			else
			 			{ // other item comes first
			 				differences.insert(std::make_pair(otherItem->getTag(),
			 					std::make_pair(SHAMapItem::pointer(), otherItem)));
							if((--maxCount)<=0) return false;
							otherItem=otherNode->nextItem(otherItem->getTag());
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
	return true;
}
