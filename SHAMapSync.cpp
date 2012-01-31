
#include <stack>

#include "SHAMap.h"

void SHAMap::getMissingNodes(std::vector<SHAMapNode>& nodeIDs, std::vector<uint256>& hashes, int max)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	
	if(root->isFullBelow()) return;

	std::stack<SHAMapInnerNode::pointer> stack;
	stack.push(root);
	while( (max>0) && (!stack.empty()) )
	{
		SHAMapInnerNode::pointer node=stack.top();
		stack.pop();
		bool all_leaves=true;
		for(int i=0; i<32; i++)
		{
			if(!node->isEmptyBranch(i))
			{
				if(node->isChildLeaf())
				{ // do we have this leaf node?
					SHAMapLeafNode::pointer leaf=getLeaf(node->getChildNodeID(i), node->getChildHash(i), false);
					if(!leaf)
					{
						if(max-->0)
						{
							nodeIDs.push_back(node->getChildNodeID(i));
							hashes.push_back(node->getChildHash(i));
						}
						all_leaves=false;
					}
				}
				else
				{ // do we have this inner node?
					SHAMapInnerNode::pointer desc=getInner(node->getChildNodeID(i), node->getChildHash(i), false);
					if(!desc)
					{
						if(max-->0)
						{
							nodeIDs.push_back(node->getChildNodeID(i));
							hashes.push_back(node->getChildHash(i));
						}
						all_leaves=false;
					}
					else if(!desc->isFullBelow()) stack.push(desc);
				}
			}
		}
		if(all_leaves) node->setFullBelow();
	}
}

bool SHAMap::getNodeFat(const SHAMapNode& wanted, std::vector<SHAMapNode>& nodeIDs,
	std::list<std::vector<unsigned char> >& rawNodes)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	if(wanted.isLeaf())
	{ // no fat way to get a leaf
		SHAMapLeafNode::pointer leaf=getLeafNode(wanted);
		if(!leaf) return false;
		nodeIDs.push_back(*leaf);
		Serializer s;
		leaf->addRaw(s);
		rawNodes.push_back(s.peekData());
		return true;
	}

	SHAMapInnerNode::pointer node=getInnerNode(wanted);
	if(!node) return false;

	bool ret=true;
	for(int i=0; i<32; i++)
	{
		if(!node->isEmptyBranch(i))
		{
		 	if(node->isChildLeaf())
		 	{
		 		SHAMapLeafNode::pointer leaf=getLeaf(node->getChildNodeID(i), node->getChildHash(i), false);
		 		if(!leaf) ret=false;
		 		else
		 		{
		 			nodeIDs.push_back(*leaf);
		 			Serializer s;
		 			leaf->addRaw(s);
		 			rawNodes.push_back(s.peekData());
		 		}
		 	}
		 	else
		 	{
		 		SHAMapInnerNode::pointer ino=getInner(node->getChildNodeID(i), node->getChildHash(i), false);
		 		if(!ino) ret=false;
		 		else
		 		{
		 			nodeIDs.push_back(*ino);
		 			Serializer s;
		 			ino->addRaw(s);
		 			rawNodes.push_back(s.peekData());
		 		}
		 	}
		}
	}
	return ret;
}

bool SHAMap::addKnownNode(const SHAMapNode& node, const std::vector<unsigned char>& rawNode)
{
	// WRITEME
	return true;
}
