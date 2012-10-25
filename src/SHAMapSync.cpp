
#include "SHAMap.h"

#include <stack>
#include <iostream>

#include <boost/make_shared.hpp>
#include <boost/test/unit_test.hpp>

#include <openssl/rand.h>

#include "Log.h"

SETUP_LOG();

void SHAMap::getMissingNodes(std::vector<SHAMapNode>& nodeIDs, std::vector<uint256>& hashes, int max,
	SHAMapSyncFilter* filter)
{
	boost::recursive_mutex::scoped_lock sl(mLock);

	assert(root->isValid());
	
	if (root->isFullBelow())
	{
		clearSynching();
		return;
	}

	if (!root->isInner())
	{
		cLog(lsWARNING) << "synching empty tree";
		return;
	}

	std::stack<SHAMapTreeNode::pointer> stack;
	stack.push(root);

	while (!stack.empty())
	{
		SHAMapTreeNode::pointer node = stack.top();
		stack.pop();

		int base = rand() % 256;
		for (int ii = 0; ii < 16; ++ii)
		{ // traverse in semi-random order
			int branch = (base + ii) % 16;
			if (!node->isEmptyBranch(branch))
			{
				SHAMapNode childID = node->getChildNodeID(branch);
				const uint256& childHash = node->getChildHash(branch);
				SHAMapTreeNode::pointer d;
				try
				{
					d = getNode(childID, childHash, false);
				}
				catch (SHAMapMissingNode&)
				{ // node is not in the map
					if (filter != NULL)
					{
						std::vector<unsigned char> nodeData;
						if (filter->haveNode(childID, childHash, nodeData))
						{
							d = boost::make_shared<SHAMapTreeNode>(childID, nodeData, mSeq, snfPREFIX);
							if (childHash != d->getNodeHash())
							{
								cLog(lsERROR) << "Wrong hash from cached object";
								d = SHAMapTreeNode::pointer();
							}
							else
							{
								cLog(lsTRACE) << "Got sync node from cache: " << *d;
								mTNByID[*d] = d;
							}
						}
					}
				}
				if (!d)
				{ // we need this node
					nodeIDs.push_back(node->getChildNodeID(branch));
					if (--max <= 0)
						return;
				}
				else if (d->isInner() && !d->isFullBelow()) // we might need children of this node
					stack.push(d);
			}
		}
	}
}

bool SHAMap::getNodeFat(const SHAMapNode& wanted, std::vector<SHAMapNode>& nodeIDs,
	std::list<std::vector<unsigned char> >& rawNodes, bool fatRoot, bool fatLeaves)
{ // Gets a node and some of its children
	boost::recursive_mutex::scoped_lock sl(mLock);

	SHAMapTreeNode::pointer node = getNode(wanted);
	if (!node)
	{
		assert(false); // FIXME Remove for release, this can happen if we get a bogus request
		return false;
	}

	nodeIDs.push_back(*node);
	Serializer s;
	node->addRaw(s, snfWIRE);
	rawNodes.push_back(s.peekData());

	if ((!fatRoot && node->isRoot()) || node->isLeaf()) // don't get a fat root, can't get a fat leaf
		return true;

	for (int i = 0; i < 16; ++i)
		if (!node->isEmptyBranch(i))
		{
			SHAMapTreeNode::pointer nextNode = getNode(node->getChildNodeID(i), node->getChildHash(i), false);
			assert(nextNode);
			if (nextNode && (fatLeaves || !nextNode->isLeaf()))
			{
				nodeIDs.push_back(*nextNode);
				Serializer s;
				nextNode->addRaw(s, snfWIRE);
				rawNodes.push_back(s.peekData());
		 	}
		}

		return true;
}

bool SHAMap::getRootNode(Serializer& s, SHANodeFormat format)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	root->addRaw(s, format);
	return true;
}

bool SHAMap::addRootNode(const std::vector<unsigned char>& rootNode, SHANodeFormat format, SHAMapSyncFilter* filter)
{
	boost::recursive_mutex::scoped_lock sl(mLock);

	// we already have a root node
	if (root->getNodeHash().isNonZero())
	{
		cLog(lsTRACE) << "got root node, already have one";
		return true;
	}

	SHAMapTreeNode::pointer node = boost::make_shared<SHAMapTreeNode>(SHAMapNode(), rootNode, 0, format);
	if (!node)
		return false;

#ifdef DEBUG
	node->dump();
#endif

	returnNode(root, true);

	root = node;
	mTNByID[*root] = root;
	if (root->getNodeHash().isZero())
	{
		root->setFullBelow();
		clearSynching();
	}
	else if (filter)
	{
		Serializer s;
		root->addRaw(s, snfPREFIX);
		filter->gotNode(*root, root->getNodeHash(), s.peekData(), root->getType());
	}

	return true;
}

bool SHAMap::addRootNode(const uint256& hash, const std::vector<unsigned char>& rootNode, SHANodeFormat format,
	SHAMapSyncFilter* filter)
{
	boost::recursive_mutex::scoped_lock sl(mLock);

	// we already have a root node
	if (root->getNodeHash().isNonZero())
	{
		cLog(lsTRACE) << "got root node, already have one";
		assert(root->getNodeHash() == hash);
		return true;
	}

	SHAMapTreeNode::pointer node = boost::make_shared<SHAMapTreeNode>(SHAMapNode(), rootNode, 0, format);
	if (!node)
		return false;
	if (node->getNodeHash() != hash)
		return false;

	returnNode(root, true);
	root = node;
	mTNByID[*root] = root;
	if (root->getNodeHash().isZero())
	{
		root->setFullBelow();
		clearSynching();
	}
	else if (filter)
	{
		Serializer s;
		root->addRaw(s, snfPREFIX);
		filter->gotNode(*root, root->getNodeHash(), s.peekData(), root->getType());
	}

	return true;
}

bool SHAMap::addKnownNode(const SHAMapNode& node, const std::vector<unsigned char>& rawNode,
	SHAMapSyncFilter* filter)
{ // return value: true=okay, false=error
	assert(!node.isRoot());
	if (!isSynching())
		return false;

	boost::recursive_mutex::scoped_lock sl(mLock);

	if (checkCacheNode(node))
		return true;

	std::stack<SHAMapTreeNode::pointer> stack = getStack(node.getNodeID(), true, true);
	if (stack.empty())
		return false;

	SHAMapTreeNode::pointer iNode = stack.top();
	if (!iNode)
	{	// we should always have a root
		assert(false);
		return true;
	}

	if (iNode->isLeaf() || (iNode->getDepth() >= node.getDepth()))
	{
		cLog(lsTRACE) << "got inner node, already had it (late)";
		return true;
	}

	if (iNode->getDepth() != (node.getDepth() - 1))
	{ // Either this node is broken or we didn't request it (yet)
		cLog(lsINFO) << "unable to hook node " << node;
		cLog(lsINFO) << " stuck at " << *iNode;
		cLog(lsINFO) << "got depth=" << node.getDepth() << ", walked to= " << iNode->getDepth();
		return false;
	}

	int branch = iNode->selectBranch(node.getNodeID());
	if (branch < 0)
	{
		assert(false);
		return false;
	}
	uint256 hash = iNode->getChildHash(branch);
	if (!hash) return false;

	SHAMapTreeNode::pointer newNode = boost::make_shared<SHAMapTreeNode>(node, rawNode, mSeq, snfWIRE);
	if (hash != newNode->getNodeHash()) // these aren't the droids we're looking for
		return false;

	if (filter)
	{
		Serializer s;
		newNode->addRaw(s, snfPREFIX);
		filter->gotNode(node, hash, s.peekData(), newNode->getType());
	}

	mTNByID[*newNode] = newNode;
	if (!newNode->isLeaf())
		return true; // only a leaf can fill a branch

	// did this new leaf cause its parents to fill up
	do
	{
		iNode = stack.top();
		stack.pop();
		assert(iNode->isInner());
		for (int i = 0; i < 16; ++i)
			if (!iNode->isEmptyBranch(i))
			{
				try
				{
					SHAMapTreeNode::pointer nextNode = getNode(iNode->getChildNodeID(i), iNode->getChildHash(i), false);
					if (nextNode->isInner() && !nextNode->isFullBelow())
						return true;
				}
				catch (SHAMapMissingNode&)
				{
					return true;
				}
			}
		iNode->setFullBelow();
	} while (!stack.empty());
	if (root->isFullBelow())
		clearSynching();
	return true;
}

bool SHAMap::deepCompare(SHAMap& other)
{ // Intended for debug/test only
	std::stack<SHAMapTreeNode::pointer> stack;
	boost::recursive_mutex::scoped_lock sl(mLock);

	stack.push(root);
	while (!stack.empty())
	{
		SHAMapTreeNode::pointer node = stack.top();
		stack.pop();

		SHAMapTreeNode::pointer otherNode;
		if (node->isRoot()) otherNode = other.root;
		else otherNode = other.getNode(*node, node->getNodeHash(), false);

		if (!otherNode)
		{
			cLog(lsINFO) << "unable to fetch node";
			return false;
		}
		else if (otherNode->getNodeHash() != node->getNodeHash())
		{
			cLog(lsWARNING) << "node hash mismatch";
			return false;
		}

//		cLog(lsTRACE) << "Comparing inner nodes " << *node;

		if (node->getNodeHash() != otherNode->getNodeHash())
			return false;
		if (node->isLeaf())
		{
			if (!otherNode->isLeaf()) return false;
			if (node->peekItem()->getTag() != otherNode->peekItem()->getTag()) return false;
			if (node->peekItem()->getData() != otherNode->peekItem()->getData()) return false;
		}
		else if (node->isInner())
		{
			if (!otherNode->isInner())
				return false;
			for (int i = 0; i < 16; ++i)
			{
				if (node->isEmptyBranch(i))
				{
					if (!otherNode->isEmptyBranch(i)) return false;
				}
				else
				{
					SHAMapTreeNode::pointer next = getNode(node->getChildNodeID(i), node->getChildHash(i), false);
					if (!next)
					{
						cLog(lsWARNING) << "unable to fetch inner node";
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
	for (int d = 0; d < 3; ++d) s.add32(rand());
	return boost::make_shared<SHAMapItem>(s.getRIPEMD160().to256(), s.peekData());
}

static bool confuseMap(SHAMap &map, int count)
{
	// add a bunch of random states to a map, then remove them
	// map should be the same
	uint256 beforeHash = map.getHash();

	std::list<uint256> items;

	for (int i = 0; i < count; ++i)
	{
		SHAMapItem::pointer item = makeRandomAS();
		items.push_back(item->getTag());
		if (!map.addItem(*item, false, false))
		{
			cLog(lsFATAL) << "Unable to add item to map";
			return false;
		}
	}

	for (std::list<uint256>::iterator it = items.begin(); it != items.end(); ++it)
	{
		if (!map.delItem(*it))
		{
			cLog(lsFATAL) << "Unable to remove item from map";
			return false;
		}
	}

	if (beforeHash != map.getHash())
	{
		cLog(lsFATAL) << "Hashes do not match";
		return false;
	}

	return true;
}

std::list<std::vector<unsigned char> > SHAMap::getTrustedPath(const uint256& index)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::stack<SHAMapTreeNode::pointer> stack = SHAMap::getStack(index, false, false);

	if (stack.empty() || !stack.top()->isLeaf())
		throw std::runtime_error("requested leaf not present");

	std::list< std::vector<unsigned char> > path;
	Serializer s;
	while (!stack.empty())
	{
		stack.top()->addRaw(s, snfWIRE);
		path.push_back(s.getData());
		s.erase();
		stack.pop();
	}
	return path;
}

BOOST_AUTO_TEST_SUITE( SHAMapSync )

BOOST_AUTO_TEST_CASE( SHAMapSync_test )
{
	cLog(lsTRACE) << "begin sync test";
	unsigned int seed;
	RAND_pseudo_bytes(reinterpret_cast<unsigned char *>(&seed), sizeof(seed));
	srand(seed);

	cLog(lsTRACE) << "Constructing maps";
	SHAMap source(smtFREE), destination(smtFREE);

	// add random data to the source map
	cLog(lsTRACE) << "Adding random data";
	int items = 10000;
	for (int i = 0; i < items; ++i)
		source.addItem(*makeRandomAS(), false, false);

	cLog(lsTRACE) << "Adding items, then removing them";
	if (!confuseMap(source, 500)) BOOST_FAIL("ConfuseMap");

	source.setImmutable();

	cLog(lsTRACE) << "SOURCE COMPLETE, SYNCHING";

	std::vector<SHAMapNode> nodeIDs, gotNodeIDs;
	std::list< std::vector<unsigned char> > gotNodes;
	std::vector<uint256> hashes;

	std::vector<SHAMapNode>::iterator nodeIDIterator;
	std::list< std::vector<unsigned char> >::iterator rawNodeIterator;

	int passes = 0;
	int nodes = 0;

	destination.setSynching();

	if (!source.getNodeFat(SHAMapNode(), nodeIDs, gotNodes, (rand() % 2) == 0, (rand() % 2) == 0))
	{
		cLog(lsFATAL) << "GetNodeFat(root) fails";
		BOOST_FAIL("GetNodeFat");
	}
	if (gotNodes.size() < 1)
	{
		cLog(lsFATAL) << "Didn't get root node " << gotNodes.size();
		BOOST_FAIL("NodeSize");
	}
	if (!destination.addRootNode(*gotNodes.begin(), snfWIRE, NULL))
	{
		cLog(lsFATAL) << "AddRootNode fails";
		BOOST_FAIL("AddRootNode");
	}
	nodeIDs.clear();
	gotNodes.clear();

	cLog(lsINFO) << "ROOT COMPLETE, INNER SYNCHING";
#ifdef SMS_DEBUG
	int bytes = 0;
#endif

	do
	{
		++passes;
		hashes.clear();

		// get the list of nodes we know we need
		destination.getMissingNodes(nodeIDs, hashes, 2048, NULL);
		if (nodeIDs.empty()) break;

		cLog(lsINFO) << nodeIDs.size() << " needed nodes";
		
		// get as many nodes as possible based on this information
		for (nodeIDIterator = nodeIDs.begin(); nodeIDIterator != nodeIDs.end(); ++nodeIDIterator)
		{
			if (!source.getNodeFat(*nodeIDIterator, gotNodeIDs, gotNodes, (rand() % 2) == 0, (rand() % 2) == 0))
			{
				cLog(lsFATAL) << "GetNodeFat fails";
				BOOST_FAIL("GetNodeFat");
			}
		}
		assert(gotNodeIDs.size() == gotNodes.size());
		nodeIDs.clear();
		hashes.clear();

		if (gotNodeIDs.empty())
		{
			cLog(lsFATAL) << "No nodes gotten";
			BOOST_FAIL("Got Node ID");
		}

		cLog(lsTRACE) << gotNodeIDs.size() << " found nodes";
		for (nodeIDIterator = gotNodeIDs.begin(), rawNodeIterator = gotNodes.begin();
				nodeIDIterator != gotNodeIDs.end(); ++nodeIDIterator, ++rawNodeIterator)
		{
			++nodes;
#ifdef SMS_DEBUG
			bytes += rawNodeIterator->size();
#endif
			if (!destination.addKnownNode(*nodeIDIterator, *rawNodeIterator, NULL))
			{
				cLog(lsTRACE) << "AddKnownNode fails";
				BOOST_FAIL("AddKnownNode");
			}
		}
		gotNodeIDs.clear();
		gotNodes.clear();


	} while (1);
	destination.clearSynching();

#ifdef SMS_DEBUG
	cLog(lsINFO) << "SYNCHING COMPLETE " << items << " items, " << nodes << " nodes, " <<
		bytes / 1024 << " KB";
#endif

	if (!source.deepCompare(destination))
	{
		cLog(lsFATAL) << "DeepCompare fails";
		BOOST_FAIL("Deep Compare");
	}

#ifdef SMS_DEBUG
	cLog(lsINFO) << "SHAMapSync test passed: " << items << " items, " <<
		passes << " passes, " << nodes << " nodes";
#endif
	
}

BOOST_AUTO_TEST_SUITE_END();

// vim:ts=4
