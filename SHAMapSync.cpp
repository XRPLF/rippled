
#include <stack>

#include <boost/make_shared.hpp>

#include <openssl/rand.h>

#include "SHAMap.h"

void SHAMap::getMissingNodes(std::vector<SHAMapNode>& nodeIDs, std::vector<uint256>& hashes, int max)
{
	boost::recursive_mutex::scoped_lock sl(mLock);

	assert(root->isValid());
	
	if(root->isFullBelow())
	{
#ifdef GMN_DEBUG
		std::cerr << "getMissingNodes: root is full below" << std::endl;
#endif
		return;
	}

	if(!root->isInner())
	{
		std::cerr << "synching empty tree" << std::endl;
		return;
	}

	std::stack<SHAMapTreeNode::pointer> stack;
	stack.push(root);

	while( (max>0) && (!stack.empty()) )
	{
		SHAMapTreeNode::pointer node=stack.top();
		stack.pop();

#ifdef GMN_DEBUG
		std::cerr << "gMN: popped " << node->getString() << std::endl;
#endif

		for(int i=0; i<16; i++)
			if(!node->isEmptyBranch(i))
			{
#ifdef GMN_DEBUG
				std::cerr << "gMN: " << node->getString() << " has non-empty branch " << i << std::endl;
#endif
				SHAMapTreeNode::pointer desc=getNode(node->getChildNodeID(i), node->getChildHash(i), false);
				if(desc)
				{
					if(desc->isInner() && !desc->isFullBelow())
						stack.push(desc);
				}
				else if(max-- > 0)
				{
#ifdef GMN_DEBUG
					std::cerr << "gMN: need " << node->getChildNodeID(i).getString() << std::endl;
#endif
					nodeIDs.push_back(node->getChildNodeID(i));
				}
			}
	}
}

bool SHAMap::getNodeFat(const SHAMapNode& wanted, std::vector<SHAMapNode>& nodeIDs,
	std::list<std::vector<unsigned char> >& rawNodes)
{
	boost::recursive_mutex::scoped_lock sl(mLock);

	SHAMapTreeNode::pointer node=getNode(wanted);
	if(!node)
	{
		assert(false); // Remove for release, this can happen if we get a bogus request
		return false;
	}

	nodeIDs.push_back(*node);
	Serializer s;
	node->addRaw(s);
	rawNodes.push_back(s.peekData());

	if(wanted.isRoot()) // don't get a fat root
		return true;

	for(int i=0; i<16; i++)
		if(!node->isEmptyBranch(i))
		{
			SHAMapTreeNode::pointer nextNode=getNode(node->getChildNodeID(i), node->getChildHash(i), false);
			assert(nextNode);
			if(nextNode)
			{
				nodeIDs.push_back(*nextNode);
				Serializer s;
				nextNode->addRaw(s);
				rawNodes.push_back(s.peekData());
		 	}
		}

		return true;
}

bool SHAMap::addRootNode(const std::vector<unsigned char>& rootNode)
{
	boost::recursive_mutex::scoped_lock sl(mLock);

	// we already have a root node
	if(!!root->getNodeHash())
	{
#ifdef DEBUG
		std::cerr << "got root node, already have one" << std::endl;
#endif
		return true;
	}

	SHAMapTreeNode::pointer node=boost::make_shared<SHAMapTreeNode>(SHAMapNode(), rootNode, 0);
	if(!node) return false;

#ifdef DEBUG
	node->dump();
#endif

	returnNode(root, true);

	root=node;
	mTNByID[*root]=root;
	if(!root->getNodeHash()) root->setFullBelow();

	return true;
}

bool SHAMap::addRootNode(const uint256& hash, const std::vector<unsigned char>& rootNode)
{
	boost::recursive_mutex::scoped_lock sl(mLock);

	// we already have a root node
	if(!!root->getNodeHash())
	{
#ifdef DEBUG
		std::cerr << "got root node, already have one" << std::endl;
#endif
		assert(root->getNodeHash()==hash);
		return true;
	}

	SHAMapTreeNode::pointer node=boost::make_shared<SHAMapTreeNode>(SHAMapNode(), rootNode, 0);
	if(!node) return false;
	if(node->getNodeHash()!=hash) return false;

	returnNode(root, true);
	root=node;
	mTNByID[*root]=root;
	if(!root->getNodeHash()) root->setFullBelow();

	return true;
}

bool SHAMap::addKnownNode(const SHAMapNode& node, const std::vector<unsigned char>& rawNode)
{ // return value: true=okay, false=error
	assert(!node.isRoot());
	assert(mSynching);

	boost::recursive_mutex::scoped_lock sl(mLock);

	if(checkCacheNode(node)) return true;

	std::stack<SHAMapTreeNode::pointer> stack=getStack(node.getNodeID(), true);
	if(stack.empty()) return false;

	SHAMapTreeNode::pointer iNode=stack.top();
	if(!iNode)
	{	// we should always have a root
		assert(false);
		return true;
	}

	if(iNode->isLeaf() || (iNode->getDepth()==node.getDepth()))
	{
#ifdef DEBUG
		std::cerr << "got inner node, already had it (late)" << std::endl;
#endif
		return true;
	}

	if(iNode->getDepth()!=(node.getDepth()-1))
	{ // Either this node is broken or we didn't request it
#ifdef DEBUG
		std::cerr << "unable to hook node " << node.getString() << std::endl;
		std::cerr << " stuck at " << iNode->getString() << std::endl;
		std::cerr << "got depth=" << node.getDepth() << ", walked to= " << iNode->getDepth() << std::endl;
#endif
		return false;
	}

	int branch=iNode->selectBranch(node.getNodeID());
	if(branch<0)
	{
		assert(false);
		return false;
	}
	uint256 hash=iNode->getChildHash(branch);
	if(!hash) return false;

	SHAMapTreeNode::pointer newNode=boost::make_shared<SHAMapTreeNode>(node, rawNode, mSeq);
	if(hash!=newNode->getNodeHash()) // these aren't the droids we're looking for
		return false;

	mTNByID[*newNode]=newNode;
	if(!newNode->isLeaf())
		return true; // only a leaf can fill a branch

	// did this new leaf cause its parents to fill up
	do
	{
		iNode=stack.top();
		stack.pop();
		assert(iNode->isInner());
		for(int i=0; i<16; i++)
			if(!iNode->isEmptyBranch(i))
			{
				SHAMapTreeNode::pointer nextNode=getNode(iNode->getChildNodeID(i), iNode->getChildHash(i), false);
				if(!nextNode) return true;
				if(nextNode->isInner() && !nextNode->isFullBelow()) return true;
			}
		iNode->setFullBelow();
	} while(!stack.empty());
	return true;
}

bool SHAMap::deepCompare(SHAMap& other)
{ // Intended for debug/test only
	std::stack<SHAMapTreeNode::pointer> stack;
	boost::recursive_mutex::scoped_lock sl(mLock);

	stack.push(root);
	while(!stack.empty())
	{
		SHAMapTreeNode::pointer node=stack.top();
		stack.pop();

		SHAMapTreeNode::pointer otherNode;
		if(node->isRoot()) otherNode=other.root;
		else otherNode=other.getNode(*node, node->getNodeHash(), false);

		if(!otherNode)
		{
			std::cerr << "unable to fetch node" << std::endl;
			return false;
		}
		else if(otherNode->getNodeHash()!=node->getNodeHash())
		{
			std::cerr << "node hash mismatch" << std::endl;
			return false;
		}

#ifdef DC_DEBUG
		std::cerr << "Comparing inner nodes " << node->getString() << std::endl;
#endif

		if(node->getNodeHash() != otherNode->getNodeHash())
			return false;
		if(node->isLeaf())
		{
			if(!otherNode->isLeaf())
				return false;
			if(node->peekItem()->getTag()!=otherNode->peekItem()->getTag())
				return false;
			if(node->peekItem()->getData()!=otherNode->peekItem()->getData())
				return false;
		}
		else if(node->isInner())
		{
			if(!otherNode->isInner())
				return false;
			for(int i=0; i<16; i++)
			{
				if(node->isEmptyBranch(i))
				{
					if(!otherNode->isEmptyBranch(i))
						return false;
				}
				else
				{
					SHAMapTreeNode::pointer next=getNode(node->getChildNodeID(i), node->getChildHash(i), false);
					if(!next)
					{
						std::cerr << "unable to fetch inner node" << std::endl;
						return false;
					}
					stack.push(next);
				}
			}
		}
	}
	return true;
}

#ifdef DEBUG
#define SMS_DEBUG
#endif

static SHAMapItem::pointer makeRandomAS()
{
		Serializer s;
		for(int d=0; d<8; d++)
			s.add32(rand());
		return boost::make_shared<SHAMapItem>(s.getRIPEMD160(), s.peekData());
}

static bool confuseMap(SHAMap &map, int count)
{
	// add a bunch of random states to a map, then remove them
	// map should be the same
	uint256 beforeHash=map.getHash();

	std::list<uint256> items;

	for(int i=0; i<count; i++)
	{
		SHAMapItem::pointer item=makeRandomAS();
		items.push_back(item->getTag());
		if(!map.addItem(*item, false))
		{
			std::cerr << "Unable to add item to map" << std::endl;
			return false;
		}
	}

	for(std::list<uint256>::iterator it=items.begin(); it!=items.end(); ++it)
	{
		if(!map.delItem(*it))
		{
			std::cerr << "Unable to remove item from map" << std::endl;
			return false;
		}
	}

	if(beforeHash!=map.getHash())
	{
		std::cerr << "Hashes do not match" << std::endl;
		return false;
	}

	return true;
}

bool SHAMap::syncTest()
{
	unsigned int seed;
	RAND_pseudo_bytes(reinterpret_cast<unsigned char *>(&seed), sizeof(seed));
	srand(seed);

	SHAMap source, destination;


	// add random data to the source map
	int items=10000;
	for(int i=0; i<items; i++)
		source.addItem(*makeRandomAS(), false);

	if(!confuseMap(source, 350))
		return false;

	source.setImmutable();

#ifdef DEBUG
	std::cerr << "SOURCE COMPLETE, SYNCHING" << std::endl;
#endif

	std::vector<SHAMapNode> nodeIDs, gotNodeIDs;
	std::list<std::vector<unsigned char> > gotNodes;
	std::vector<uint256> hashes;

	std::vector<SHAMapNode>::iterator nodeIDIterator;
	std::list<std::vector<unsigned char> >::iterator rawNodeIterator;

	int passes=0;
	int nodes=0;

	destination.setSynching();

	if(!source.getNodeFat(SHAMapNode(), nodeIDs, gotNodes))
	{
		std::cerr << "GetNodeFat(root) fails" << std::endl;
		assert(false);
		return false;
	}
	if(gotNodes.size()!=1)
	{
		std::cerr << "Didn't get root node " << gotNodes.size() << std::endl;
		assert(false);
		return false;
	}
	if(!destination.addRootNode(*gotNodes.begin()))
	{
		std::cerr << "AddRootNode fails" << std::endl;
		assert(false);
		return false;
	}
	nodeIDs.clear();
	gotNodes.clear();

#ifdef DEBUG
	std::cerr << "ROOT COMPLETE, INNER SYNCHING" << std::endl;
#endif

	do
	{
		passes++;
		hashes.clear();

		// get the list of nodes we know we need
		destination.getMissingNodes(nodeIDs, hashes, 2048);
		if(!nodeIDs.size()) break;

#ifdef SMS_DEBUG
		std::cerr << nodeIDs.size() << " needed nodes" << std::endl;
#endif
		
		// get as many nodes as possible based on this information
		for(nodeIDIterator=nodeIDs.begin(); nodeIDIterator!=nodeIDs.end(); ++nodeIDIterator)
			if(!source.getNodeFat(*nodeIDIterator, gotNodeIDs, gotNodes))
			{
				std::cerr << "GetNodeFat fails" << std::endl;
				assert(false);
				return false;
			}
		assert(gotNodeIDs.size() == gotNodes.size());
		nodeIDs.clear();
		hashes.clear();

		if(!gotNodeIDs.size())
		{
			std::cerr << "No nodes gotten" << std::endl;
			assert(false);
			return false;
		}

#ifdef SMS_DEBUG
		std::cerr << gotNodeIDs.size() << " found nodes" << std::endl;
#endif
		for(nodeIDIterator=gotNodeIDs.begin(), rawNodeIterator=gotNodes.begin();
				nodeIDIterator!=gotNodeIDs.end(); ++nodeIDIterator, ++rawNodeIterator)
		{
			nodes++;
			if(!destination.addKnownNode(*nodeIDIterator, *rawNodeIterator))
			{
				std::cerr << "AddKnownNode fails" << std::endl;
				assert(false);
				return false;
			}
		}
		gotNodeIDs.clear();
		gotNodes.clear();


	} while(1);
	destination.clearSynching();

#ifdef SMS_DEBUG
	std::cerr << "SYNCHING COMPLETE " << items << " items, " << nodes << " nodes" << std::endl;
#endif

	if(!source.deepCompare(destination))
	{
		std::cerr << "DeepCompare fails" << std::endl;
		assert(false);
		return false;
	}

#ifdef SMS_DEBUG
	std::cerr << "SHAMapSync test passed: " << items << " items, " <<
		passes << " passes, " << nodes << " nodes" << std::endl;
#endif
	
	return true;
}
