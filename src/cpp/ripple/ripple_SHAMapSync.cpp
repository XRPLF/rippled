
// VFALCO TODO tidy up this global

static const uint256 uZero;

KeyCache <uint256, UptimeTimerAdapter> SHAMap::fullBelowCache("fullBelowCache", 65536, 240);

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
		WriteLog (lsWARNING, SHAMap) << "synching empty tree";
		return;
	}

	std::stack<SHAMapTreeNode*> stack;
	stack.push(root.get());

	while (!stack.empty())
	{
		SHAMapTreeNode* node = stack.top();
		stack.pop();

		int base = rand() % 256;
		bool have_all = true;
		for (int ii = 0; ii < 16; ++ii)
		{ // traverse in semi-random order
			int branch = (base + ii) % 16;
			if (!node->isEmptyBranch(branch))
			{
				uint256 const& childHash = node->getChildHash(branch);
				if (!fullBelowCache.isPresent(childHash))
				{
					SHAMapNode childID = node->getChildNodeID(branch);
					SHAMapTreeNode* d = NULL;
					try
					{
						d = getNodePointer(childID, childHash, filter);
						if (d->isInner() && !d->isFullBelow())
						{
							have_all = false;
							stack.push(d);
						}
					}
					catch (SHAMapMissingNode&)
					{ // node is not in the map
						nodeIDs.push_back(childID);
						hashes.push_back(childHash);
						if (--max <= 0)
							return;
						have_all = false;
					}
				}
			}
		}
		if (have_all)
		{
			node->setFullBelow();
			if (mType == smtSTATE)
			{
				fullBelowCache.add(node->getNodeHash());
				dropBelow(node);
			}
		}
	}
	if (nodeIDs.empty())
		clearSynching();
}

std::vector<uint256> SHAMap::getNeededHashes(int max, SHAMapSyncFilter* filter)
{
	std::vector<uint256> ret;
	boost::recursive_mutex::scoped_lock sl(mLock);

	assert(root->isValid());

	if (root->isFullBelow() || !root->isInner())
	{
		clearSynching();
		return ret;
	}

	std::stack<SHAMapTreeNode*> stack;
	stack.push(root.get());

	while (!stack.empty())
	{
		SHAMapTreeNode* node = stack.top();
		stack.pop();

		int base = rand() % 256;
		bool have_all = true;
		for (int ii = 0; ii < 16; ++ii)
		{ // traverse in semi-random order
			int branch = (base + ii) % 16;
			if (!node->isEmptyBranch(branch))
			{
				uint256 const& childHash = node->getChildHash(branch);
				if (!fullBelowCache.isPresent(childHash))
				{
					SHAMapNode childID = node->getChildNodeID(branch);
					SHAMapTreeNode* d = NULL;
					try
					{
						d = getNodePointer(childID, childHash, filter);
						if (d->isInner() && !d->isFullBelow())
						{
							have_all = false;
							stack.push(d);
						}
					}
					catch (SHAMapMissingNode&)
					{ // node is not in the map
						have_all = false;
						ret.push_back(childHash);
						if (--max <= 0)
							return ret;
					}
				}
			}
		}
		if (have_all)
		{
			node->setFullBelow();
			if (mType == smtSTATE)
			{
				fullBelowCache.add(node->getNodeHash());
				dropBelow(node);
			}
		}
	}
	if (ret.empty())
		clearSynching();
	return ret;
}

bool SHAMap::getNodeFat(const SHAMapNode& wanted, std::vector<SHAMapNode>& nodeIDs,
	std::list<Blob >& rawNodes, bool fatRoot, bool fatLeaves)
{ // Gets a node and some of its children
	boost::recursive_mutex::scoped_lock sl(mLock);

	SHAMapTreeNode::pointer node = getNode(wanted);
	if (!node)
	{
		WriteLog (lsWARNING, SHAMap) << "peer requested node that is not in the map: " << wanted;
		throw std::runtime_error("Peer requested node not in map");
	}

	if (node->isInner() && node->isEmpty())
	{
		WriteLog (lsWARNING, SHAMap) << "peer requests empty node";
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

SHAMapAddNode SHAMap::addRootNode(Blob const& rootNode, SHANodeFormat format,
	SHAMapSyncFilter* filter)
{
	boost::recursive_mutex::scoped_lock sl(mLock);

	// we already have a root node
	if (root->getNodeHash().isNonZero())
	{
		WriteLog (lsTRACE, SHAMap) << "got root node, already have one";
		return SHAMapAddNode::okay();
	}

	assert(mSeq >= 1);
	SHAMapTreeNode::pointer node =
		boost::make_shared<SHAMapTreeNode>(SHAMapNode(), rootNode, mSeq - 1, format, uZero, false);
	if (!node)
		return SHAMapAddNode::invalid();

#ifdef DEBUG
	node->dump();
#endif

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
		filter->gotNode(false, *root, root->getNodeHash(), s.peekData(), root->getType());
	}

	return SHAMapAddNode::useful();
}

SHAMapAddNode SHAMap::addRootNode(uint256 const& hash, Blob const& rootNode, SHANodeFormat format,
	SHAMapSyncFilter* filter)
{
	boost::recursive_mutex::scoped_lock sl(mLock);

	// we already have a root node
	if (root->getNodeHash().isNonZero())
	{
		WriteLog (lsTRACE, SHAMap) << "got root node, already have one";
		assert(root->getNodeHash() == hash);
		return SHAMapAddNode::okay();
	}

	assert(mSeq >= 1);
	SHAMapTreeNode::pointer node =
		boost::make_shared<SHAMapTreeNode>(SHAMapNode(), rootNode, mSeq - 1, format, uZero, false);
	if (!node || node->getNodeHash() != hash)
		return SHAMapAddNode::invalid();

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
		filter->gotNode(false, *root, root->getNodeHash(), s.peekData(), root->getType());
	}

	return SHAMapAddNode::useful();
}

SHAMapAddNode SHAMap::addKnownNode(const SHAMapNode& node, Blob const& rawNode,
	SHAMapSyncFilter* filter)
{ // return value: true=okay, false=error
	assert(!node.isRoot());
	if (!isSynching())
	{
		WriteLog (lsTRACE, SHAMap) << "AddKnownNode while not synching";
		return SHAMapAddNode::okay();
	}

	boost::recursive_mutex::scoped_lock sl(mLock);

	if (checkCacheNode(node)) // Do we already have this node?
		return SHAMapAddNode::okay();

	SHAMapTreeNode* iNode = root.get();
	while (!iNode->isLeaf() && !iNode->isFullBelow() && (iNode->getDepth() < node.getDepth()))
	{
		int branch = iNode->selectBranch(node.getNodeID());
		assert(branch >= 0);

		if (iNode->isEmptyBranch(branch))
		{
			WriteLog (lsWARNING, SHAMap) << "Add known node for empty branch" << node;
			return SHAMapAddNode::invalid();
		}
		if (fullBelowCache.isPresent(iNode->getChildHash(branch)))
			return SHAMapAddNode::okay();

		try
		{
			iNode = getNodePointer(iNode->getChildNodeID(branch), iNode->getChildHash(branch), filter);
		}
		catch (SHAMapMissingNode)
		{
			if (iNode->getDepth() != (node.getDepth() - 1))
			{ // Either this node is broken or we didn't request it (yet)
				WriteLog (lsWARNING, SHAMap) << "unable to hook node " << node;
				WriteLog (lsINFO, SHAMap) << " stuck at " << *iNode;
				WriteLog (lsINFO, SHAMap) << "got depth=" << node.getDepth() << ", walked to= " << iNode->getDepth();
				return SHAMapAddNode::invalid();
			}

			SHAMapTreeNode::pointer newNode =
				boost::make_shared<SHAMapTreeNode>(node, rawNode, mSeq - 1, snfWIRE, uZero, false);
			if (iNode->getChildHash(branch) != newNode->getNodeHash())
			{
				WriteLog (lsWARNING, SHAMap) << "Corrupt node recevied";
				return SHAMapAddNode::invalid();
			}

			if (filter)
			{
				Serializer s;
				newNode->addRaw(s, snfPREFIX);
				filter->gotNode(false, node, iNode->getChildHash(branch), s.peekData(), newNode->getType());
			}
			mTNByID[node] = newNode;
			return SHAMapAddNode::useful();
		}
	}

	WriteLog (lsTRACE, SHAMap) << "got node, already had it (late)";
	return SHAMapAddNode::okay();
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
			WriteLog (lsINFO, SHAMap) << "unable to fetch node";
			return false;
		}
		else if (otherNode->getNodeHash() != node->getNodeHash())
		{
			WriteLog (lsWARNING, SHAMap) << "node hash mismatch";
			return false;
		}

//		WriteLog (lsTRACE) << "Comparing inner nodes " << *node;

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
						WriteLog (lsWARNING, SHAMap) << "unable to fetch inner node";
						return false;
					}
					stack.push(next);
				}
			}
		}
	}
	return true;
}

bool SHAMap::hasInnerNode(const SHAMapNode& nodeID, uint256 const& nodeHash)
{
	SHAMapTreeNode* node = root.get();
	while (node->isInner() && (node->getDepth() < nodeID.getDepth()))
	{
		int branch = node->selectBranch(nodeID.getNodeID());
		if (node->isEmptyBranch(branch))
			break;
		node = getNodePointer(node->getChildNodeID(branch), node->getChildHash(branch));
	}
	return node->getNodeHash() == nodeHash;
}

bool SHAMap::hasLeafNode(uint256 const& tag, uint256 const& nodeHash)
{
	SHAMapTreeNode* node = root.get();
	while (node->isInner())
	{
		int branch = node->selectBranch(tag);
		if (node->isEmptyBranch(branch))
			return false;
		node = getNodePointer(node->getChildNodeID(branch), node->getChildHash(branch));
	}
	return node->getNodeHash() == nodeHash;
}

std::list<SHAMap::fetchPackEntry_t> SHAMap::getFetchPack(SHAMap* have, bool includeLeaves, int max)
{
	std::list<fetchPackEntry_t> ret;

	boost::recursive_mutex::scoped_lock ul1(mLock);

	boost::shared_ptr< boost::unique_lock<boost::recursive_mutex> > ul2;

	if (have)
	{
		ul2 = boost::make_shared< boost::unique_lock<boost::recursive_mutex> >
			(boost::ref(have->mLock), boost::try_to_lock);
		if (!(*ul2))
		{
			WriteLog (lsINFO, SHAMap) << "Unable to create pack due to lock";
			return ret;
		}
	}


	if (root->isLeaf())
	{
		if (includeLeaves && !root->getNodeHash().isZero() &&
			(!have || !have->hasLeafNode(root->getTag(), root->getNodeHash())))
		{
			Serializer s;
			root->addRaw(s, snfPREFIX);
			ret.push_back(fetchPackEntry_t(root->getNodeHash(), s.peekData()));
		}
		return ret;
	}

	if (root->getNodeHash().isZero())
		return ret;

	if (have && (root->getNodeHash() == have->root->getNodeHash()))
		return ret;

	std::stack<SHAMapTreeNode*> stack; // contains unexplored non-matching inner node entries
	stack.push(root.get());

	while (!stack.empty())
	{
		SHAMapTreeNode* node = stack.top();
		stack.pop();

		// 1) Add this node to the pack
		Serializer s;
		node->addRaw(s, snfPREFIX);
		ret.push_back(fetchPackEntry_t(node->getNodeHash(), s.peekData()));
		--max;

		// 2) push non-matching child inner nodes
		for (int i = 0; i < 16; ++i)
		{
			if (!node->isEmptyBranch(i))
			{
				uint256 const& childHash = node->getChildHash(i);
				SHAMapNode childID = node->getChildNodeID(i);

				SHAMapTreeNode *next = getNodePointer(childID, childHash);
				if (next->isInner())
				{
					if (!have || !have->hasInnerNode(*next, childHash))
						stack.push(next);
				}
				else if (includeLeaves && (!have || !have->hasLeafNode(next->getTag(), childHash)))
				{
					Serializer s;
					node->addRaw(s, snfPREFIX);
					ret.push_back(fetchPackEntry_t(node->getNodeHash(), s.peekData()));
					--max;
				}
			}
		}

		if (max <= 0)
			break;
	}
	return ret;
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
			WriteLog (lsFATAL, SHAMap) << "Unable to add item to map";
			return false;
		}
	}

	for (std::list<uint256>::iterator it = items.begin(); it != items.end(); ++it)
	{
		if (!map.delItem(*it))
		{
			WriteLog (lsFATAL, SHAMap) << "Unable to remove item from map";
			return false;
		}
	}

	if (beforeHash != map.getHash())
	{
		WriteLog (lsFATAL, SHAMap) << "Hashes do not match";
		return false;
	}

	return true;
}

std::list<Blob > SHAMap::getTrustedPath(uint256 const& index)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::stack<SHAMapTreeNode::pointer> stack = SHAMap::getStack(index, false, false);

	if (stack.empty() || !stack.top()->isLeaf())
		throw std::runtime_error("requested leaf not present");

	std::list< Blob > path;
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
	WriteLog (lsTRACE, SHAMap) << "begin sync test";
	unsigned int seed;
	RAND_pseudo_bytes(reinterpret_cast<unsigned char *>(&seed), sizeof(seed));
	srand(seed);

	WriteLog (lsTRACE, SHAMap) << "Constructing maps";
	SHAMap source(smtFREE), destination(smtFREE);

	// add random data to the source map
	WriteLog (lsTRACE, SHAMap) << "Adding random data";
	int items = 10000;
	for (int i = 0; i < items; ++i)
		source.addItem(*makeRandomAS(), false, false);

	WriteLog (lsTRACE, SHAMap) << "Adding items, then removing them";
	if (!confuseMap(source, 500)) BOOST_FAIL("ConfuseMap");

	source.setImmutable();

	WriteLog (lsTRACE, SHAMap) << "SOURCE COMPLETE, SYNCHING";

	std::vector<SHAMapNode> nodeIDs, gotNodeIDs;
	std::list< Blob > gotNodes;
	std::vector<uint256> hashes;

	std::vector<SHAMapNode>::iterator nodeIDIterator;
	std::list< Blob >::iterator rawNodeIterator;

	int passes = 0;
	int nodes = 0;

	destination.setSynching();

	if (!source.getNodeFat(SHAMapNode(), nodeIDs, gotNodes, (rand() % 2) == 0, (rand() % 2) == 0))
	{
		WriteLog (lsFATAL, SHAMap) << "GetNodeFat(root) fails";
		BOOST_FAIL("GetNodeFat");
	}
	if (gotNodes.size() < 1)
	{
		WriteLog (lsFATAL, SHAMap) << "Didn't get root node " << gotNodes.size();
		BOOST_FAIL("NodeSize");
	}

	if (!destination.addRootNode(*gotNodes.begin(), snfWIRE, NULL))
	{
		WriteLog (lsFATAL, SHAMap) << "AddRootNode fails";
		BOOST_FAIL("AddRootNode");
	}
	nodeIDs.clear();
	gotNodes.clear();

	WriteLog (lsINFO, SHAMap) << "ROOT COMPLETE, INNER SYNCHING";
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

		WriteLog (lsINFO, SHAMap) << nodeIDs.size() << " needed nodes";
		
		// get as many nodes as possible based on this information
		for (nodeIDIterator = nodeIDs.begin(); nodeIDIterator != nodeIDs.end(); ++nodeIDIterator)
		{
			if (!source.getNodeFat(*nodeIDIterator, gotNodeIDs, gotNodes, (rand() % 2) == 0, (rand() % 2) == 0))
			{
				WriteLog (lsFATAL, SHAMap) << "GetNodeFat fails";
				BOOST_FAIL("GetNodeFat");
			}
		}
		assert(gotNodeIDs.size() == gotNodes.size());
		nodeIDs.clear();
		hashes.clear();

		if (gotNodeIDs.empty())
		{
			WriteLog (lsFATAL, SHAMap) << "No nodes gotten";
			BOOST_FAIL("Got Node ID");
		}

		WriteLog (lsTRACE, SHAMap) << gotNodeIDs.size() << " found nodes";
		for (nodeIDIterator = gotNodeIDs.begin(), rawNodeIterator = gotNodes.begin();
				nodeIDIterator != gotNodeIDs.end(); ++nodeIDIterator, ++rawNodeIterator)
		{
			++nodes;
#ifdef SMS_DEBUG
			bytes += rawNodeIterator->size();
#endif
			if (!destination.addKnownNode(*nodeIDIterator, *rawNodeIterator, NULL))
			{
				WriteLog (lsTRACE, SHAMap) << "AddKnownNode fails";
				BOOST_FAIL("AddKnownNode");
			}
		}
		gotNodeIDs.clear();
		gotNodes.clear();


	} while (1);
	destination.clearSynching();

#ifdef SMS_DEBUG
	WriteLog (lsINFO, SHAMap) << "SYNCHING COMPLETE " << items << " items, " << nodes << " nodes, " <<
		bytes / 1024 << " KB";
#endif

	if (!source.deepCompare(destination))
	{
		WriteLog (lsFATAL, SHAMap) << "DeepCompare fails";
		BOOST_FAIL("Deep Compare");
	}

#ifdef SMS_DEBUG
	WriteLog (lsINFO, SHAMap) << "SHAMapSync test passed: " << items << " items, " <<
		passes << " passes, " << nodes << " nodes";
#endif
	
}

BOOST_AUTO_TEST_SUITE_END();

// vim:ts=4
