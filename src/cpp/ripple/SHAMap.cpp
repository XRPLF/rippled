
#include "SHAMap.h"

#include <stack>

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/smart_ptr/make_shared.hpp>
#include <boost/test/unit_test.hpp>
#include <iostream>

#include "Serializer.h"
#include "BitcoinUtil.h"
#include "Log.h"
#include "SHAMap.h"
#include "Application.h"

SETUP_LOG();

DECLARE_INSTANCE(SHAMap);
DECLARE_INSTANCE(SHAMapItem);
DECLARE_INSTANCE(SHAMapTreeNode);

std::size_t hash_value(const SHAMapNode& mn)
{
	std::size_t seed = theApp->getNonceST();

	boost::hash_combine(seed, mn.getDepth());

	return mn.getNodeID().hash_combine(seed);
}

std::size_t hash_value(const uint256& u)
{
	std::size_t seed = theApp->getNonceST();

	return u.hash_combine(seed);
}

std::size_t hash_value(const uint160& u)
{
	std::size_t seed = theApp->getNonceST();

	return u.hash_combine(seed);
}


SHAMap::SHAMap(SHAMapType t, uint32 seq) : mSeq(seq), mState(smsModifying), mType(t)
{
	root = boost::make_shared<SHAMapTreeNode>(mSeq, SHAMapNode(0, uint256()));
	root->makeInner();
	mTNByID[*root] = root;
}

SHAMap::SHAMap(SHAMapType t, const uint256& hash) : mSeq(1), mState(smsSynching), mType(t)
{ // FIXME: Need to acquire root node
	root = boost::make_shared<SHAMapTreeNode>(mSeq, SHAMapNode(0, uint256()));
	root->makeInner();
	mTNByID[*root] = root;
}

SHAMap::pointer SHAMap::snapShot(bool isMutable)
{ // Return a new SHAMap that is an immutable snapshot of this one
  // Initially nodes are shared, but CoW is forced on both ledgers
	SHAMap::pointer ret = boost::make_shared<SHAMap>(mType);
	SHAMap& newMap = *ret;
	newMap.mSeq = ++mSeq;
	newMap.mTNByID = mTNByID;
	newMap.root = root;
	if (!isMutable)
		newMap.mState = smsImmutable;
	return ret;
}

std::stack<SHAMapTreeNode::pointer> SHAMap::getStack(const uint256& id, bool include_nonmatching_leaf, bool partialOk)
{
	// Walk the tree as far as possible to the specified identifier
	// produce a stack of nodes along the way, with the terminal node at the top
	std::stack<SHAMapTreeNode::pointer> stack;
	SHAMapTreeNode::pointer node = root;
	while (!node->isLeaf())
	{
		stack.push(node);

		int branch = node->selectBranch(id);
		assert(branch >= 0);

		uint256 hash = node->getChildHash(branch);
		if (hash.isZero())
			return stack;

		try
		{
			node = getNode(node->getChildNodeID(branch), hash, false);
		}
		catch (SHAMapMissingNode& mn)
		{
			if (partialOk)
				return stack;
			mn.setTargetNode(id);
			throw;
		}
	}

	if (include_nonmatching_leaf || (node->peekItem()->getTag() == id))
		stack.push(node);

	return stack;
}

void SHAMap::dirtyUp(std::stack<SHAMapTreeNode::pointer>& stack, const uint256& target, uint256 prevHash)
{ // walk the tree up from through the inner nodes to the root
  // update linking hashes and add nodes to dirty list

	assert((mState != smsSynching) && (mState != smsImmutable));

	while (!stack.empty())
	{
		SHAMapTreeNode::pointer node = stack.top();
		stack.pop();
		assert(node->isInnerNode());

		int branch = node->selectBranch(target);
		assert(branch >= 0);

		returnNode(node, true);

		if (!node->setChildHash(branch, prevHash))
		{
			cLog(lsFATAL) << "dirtyUp terminates early";
			assert(false);
			return;
		}
#ifdef ST_DEBUG
		cLog(lsTRACE) << "dirtyUp sets branch " << branch << " to " << prevHash;
#endif
		prevHash = node->getNodeHash();
		assert(prevHash.isNonZero());
	}
}

SHAMapTreeNode::pointer SHAMap::checkCacheNode(const SHAMapNode& iNode)
{
	boost::unordered_map<SHAMapNode, SHAMapTreeNode::pointer>::iterator it = mTNByID.find(iNode);
	if (it == mTNByID.end())
		return SHAMapTreeNode::pointer();
	it->second->touch(mSeq);
	return it->second;
}

SHAMapTreeNode::pointer SHAMap::walkTo(const uint256& id, bool modify)
{ // walk down to the terminal node for this ID

	SHAMapTreeNode::pointer inNode = root;

	while (!inNode->isLeaf())
	{
		int branch = inNode->selectBranch(id);
		if (inNode->isEmptyBranch(branch))
			return inNode;
		uint256 childHash = inNode->getChildHash(branch);

		try
		{
			inNode = getNode(inNode->getChildNodeID(branch), childHash, false);
		}
		catch (SHAMapMissingNode& mn)
		{
			mn.setTargetNode(id);
			throw;
		}
	}
	if (inNode->getTag() != id)
		return SHAMapTreeNode::pointer();
	if (modify)
		returnNode(inNode, true);
	return inNode;
}

SHAMapTreeNode* SHAMap::walkToPointer(const uint256& id)
{
	SHAMapTreeNode* inNode = root.get();
	while (!inNode->isLeaf())
	{
		int branch = inNode->selectBranch(id);
		const uint256& nextHash = inNode->getChildHash(branch);
		if (nextHash.isZero()) return NULL;
		inNode = getNodePointer(inNode->getChildNodeID(branch), nextHash);
		if (!inNode)
			throw SHAMapMissingNode(mType, inNode->getChildNodeID(branch), nextHash, id);
	}
	return (inNode->getTag() == id) ? inNode : NULL;
}

SHAMapTreeNode::pointer SHAMap::getNode(const SHAMapNode& id, const uint256& hash, bool modify)
{ // retrieve a node whose node hash is known
	SHAMapTreeNode::pointer node = checkCacheNode(id);
	if (node)
	{
#ifdef DEBUG
		if (node->getNodeHash() != hash)
		{
			std::cerr << "Attempt to get node, hash not in tree" << std::endl;
			std::cerr << "ID: " << id << std::endl;
			std::cerr << "TgtHash " << hash << std::endl;
			std::cerr << "NodHash " << node->getNodeHash() << std::endl;
			dump();
			throw std::runtime_error("invalid node");
		}
#endif
		returnNode(node, modify);
		return node;
	}

	node = fetchNodeExternal(id, hash);
	if (!mTNByID.insert(std::make_pair(id, node)).second)
		assert(false);
	trackNewNode(node);
	return node;
}

SHAMapTreeNode* SHAMap::getNodePointer(const SHAMapNode& id, const uint256& hash)
{ // fast, but you do not hold a reference
	boost::unordered_map<SHAMapNode, SHAMapTreeNode::pointer>::iterator it = mTNByID.find(id);
	if (it != mTNByID.end())
		return it->second.get();

	SHAMapTreeNode::pointer node = fetchNodeExternal(id, hash);
	if (!mTNByID.insert(std::make_pair(id, node)).second)
		assert(false);
	trackNewNode(node);
	return node.get();
}

void SHAMap::returnNode(SHAMapTreeNode::pointer& node, bool modify)
{ // make sure the node is suitable for the intended operation (copy on write)
	assert(node->isValid());
	assert(node->getSeq() <= mSeq);
	if (node && modify && (node->getSeq() != mSeq))
	{ // have a CoW
		assert(node->getSeq() < mSeq);

		node = boost::make_shared<SHAMapTreeNode>(*node, mSeq); // here's to the new node, same as the old node
		assert(node->isValid());

		mTNByID[*node] = node;
		if (node->isRoot())
			root = node;
		if (mDirtyNodes)
			(*mDirtyNodes)[*node] = node;
	}
}

void SHAMap::trackNewNode(SHAMapTreeNode::pointer& node)
{
	if (mDirtyNodes)
		(*mDirtyNodes)[*node] = node;
}

SHAMapItem::SHAMapItem(const uint256& tag, const std::vector<unsigned char>& data)
	: mTag(tag), mData(data)
{ ; }

SHAMapItem::SHAMapItem(const uint256& tag, const Serializer& data)
	: mTag(tag), mData(data.peekData())
{ ; }

SHAMapTreeNode* SHAMap::firstBelow(SHAMapTreeNode* node)
{
	// Return the first item below this node
#ifdef ST_DEBUG
	std::cerr << "firstBelow(" << *node << ")" << std::endl;
#endif
	do
	{ // Walk down the tree
		if (node->hasItem()) return node;

		bool foundNode = false;
		for (int i = 0; i < 16; ++i)
			if (!node->isEmptyBranch(i))
			{
#ifdef ST_DEBUG
	std::cerr << " FB: node " << *node << std::endl;
	std::cerr << "  has non-empty branch " << i << " : " <<
		node->getChildNodeID(i) << ", " << node->getChildHash(i) << std::endl;
#endif
				node = getNodePointer(node->getChildNodeID(i), node->getChildHash(i));
				foundNode = true;
				break;
			}
		if (!foundNode)
			return NULL;
	} while (true);
}

SHAMapTreeNode* SHAMap::lastBelow(SHAMapTreeNode* node)
{
#ifdef DEBUG
	std::cerr << "lastBelow(" << *node << ")" << std::endl;
#endif

	do
	{ // Walk down the tree
		if (node->hasItem())
			return node;

		bool foundNode = false;
		for (int i = 15; i >= 0; ++i)
			if (!node->isEmptyBranch(i))
			{
				node = getNodePointer(node->getChildNodeID(i), node->getChildHash(i));
				foundNode = true;
				break;
			}
		if (!foundNode)
			return NULL;
	} while (true);
}

SHAMapItem::pointer SHAMap::onlyBelow(SHAMapTreeNode* node)
{
	// If there is only one item below this node, return it
	while (!node->isLeaf())
	{
		SHAMapTreeNode* nextNode = NULL;

		for (int i = 0; i < 16; ++i)
			if (!node->isEmptyBranch(i))
			{
				if (nextNode)
					return SHAMapItem::pointer(); // two leaves below
				nextNode = getNodePointer(node->getChildNodeID(i), node->getChildHash(i));
			}

		if (!nextNode)
		{
			std::cerr << *node << std::endl;
			assert(false);
			return SHAMapItem::pointer();
		}
		node = nextNode;
	}
	assert(node->hasItem());
	return node->peekItem();
}

void SHAMap::eraseChildren(SHAMapTreeNode::pointer node)
{ // this node has only one item below it, erase its children
	bool erase = false;
	while (node->isInner())
	{
		for (int i = 0; i < 16; ++i)
			if (!node->isEmptyBranch(i))
			{
				SHAMapTreeNode::pointer nextNode = getNode(node->getChildNodeID(i), node->getChildHash(i), false);
				if (erase)
				{
					returnNode(node, true);
					if (mTNByID.erase(*node))
						assert(false);
				}
				erase = true;
				node = nextNode;
				break;
			}
	}
	returnNode(node, true);
	if (mTNByID.erase(*node) == 0)
		assert(false);
	return;
}

SHAMapItem::pointer SHAMap::peekFirstItem()
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	SHAMapTreeNode *node = firstBelow(root.get());
	if (!node)
		return SHAMapItem::pointer();
	return node->peekItem();
}

SHAMapItem::pointer SHAMap::peekFirstItem(SHAMapTreeNode::TNType& type)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	SHAMapTreeNode *node = firstBelow(root.get());
	if (!node)
		return SHAMapItem::pointer();
	type = node->getType();
	return node->peekItem();
}

SHAMapItem::pointer SHAMap::peekLastItem()
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	SHAMapTreeNode *node = lastBelow(root.get());
	if (!node)
		return SHAMapItem::pointer();
	return node->peekItem();
}

SHAMapItem::pointer SHAMap::peekNextItem(const uint256& id)
{
	SHAMapTreeNode::TNType type;
	return peekNextItem(id, type);
}


SHAMapItem::pointer SHAMap::peekNextItem(const uint256& id, SHAMapTreeNode::TNType& type)
{ // Get a pointer to the next item in the tree after a given item - item must be in tree
	boost::recursive_mutex::scoped_lock sl(mLock);

	std::stack<SHAMapTreeNode::pointer> stack = getStack(id, true, false);
	while (!stack.empty())
	{
		SHAMapTreeNode::pointer node = stack.top();
		stack.pop();

		if (node->isLeaf())
		{
			if (node->peekItem()->getTag() > id)
			{
				type = node->getType();
				return node->peekItem();
			}
		}
		else
			for (int i = node->selectBranch(id) + 1; i < 16; ++i)
				if (!node->isEmptyBranch(i))
				{
					SHAMapTreeNode *firstNode = getNodePointer(node->getChildNodeID(i), node->getChildHash(i));
					if (!firstNode)
						throw std::runtime_error("missing node");
					firstNode = firstBelow(firstNode);
					if (!firstNode)
						throw std::runtime_error("missing node");
					type = firstNode->getType();
					return firstNode->peekItem();
				}
	}
	// must be last item
	return SHAMapItem::pointer();
}

SHAMapItem::pointer SHAMap::peekPrevItem(const uint256& id)
{ // Get a pointer to the previous item in the tree after a given item - item must be in tree
	boost::recursive_mutex::scoped_lock sl(mLock);

	std::stack<SHAMapTreeNode::pointer> stack = getStack(id, true, false);
	while (!stack.empty())
	{
		SHAMapTreeNode::pointer node = stack.top();
		stack.pop();

		if (node->isLeaf())
		{
			if (node->peekItem()->getTag() < id)
				return node->peekItem();
		}
		else for (int i = node->selectBranch(id) - 1; i >= 0; --i)
				if (!node->isEmptyBranch(i))
				{
					node = getNode(node->getChildNodeID(i), node->getChildHash(i), false);
					SHAMapTreeNode* item = firstBelow(node.get());
					if (!item)
						throw std::runtime_error("missing node");
					return item->peekItem();
				}
	}
	// must be last item
	return SHAMapItem::pointer();
}

SHAMapItem::pointer SHAMap::peekItem(const uint256& id)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	SHAMapTreeNode* leaf = walkToPointer(id);
	if (!leaf)
		return SHAMapItem::pointer();
	return leaf->peekItem();
}

SHAMapItem::pointer SHAMap::peekItem(const uint256& id, SHAMapTreeNode::TNType& type)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	SHAMapTreeNode* leaf = walkToPointer(id);
	if (!leaf)
		return SHAMapItem::pointer();
	type = leaf->getType();
	return leaf->peekItem();
}

bool SHAMap::hasItem(const uint256& id)
{ // does the tree have an item with this ID
	boost::recursive_mutex::scoped_lock sl(mLock); 

	SHAMapTreeNode* leaf = walkToPointer(id);
	return (leaf != NULL);
}

bool SHAMap::delItem(const uint256& id)
{ // delete the item with this ID
	boost::recursive_mutex::scoped_lock sl(mLock);
	assert(mState != smsImmutable);

	std::stack<SHAMapTreeNode::pointer> stack = getStack(id, true, false);
	if (stack.empty())
		throw std::runtime_error("missing node");

	SHAMapTreeNode::pointer leaf=stack.top();
	stack.pop();
	if (!leaf || !leaf->hasItem() || (leaf->peekItem()->getTag() != id))
		return false;

	SHAMapTreeNode::TNType type=leaf->getType();
	returnNode(leaf, true);
	if (mTNByID.erase(*leaf) == 0)
		assert(false);

	uint256 prevHash;
	while (!stack.empty())
	{
		SHAMapTreeNode::pointer node=stack.top();
		stack.pop();
		returnNode(node, true);
		assert(node->isInner());

		if (!node->setChildHash(node->selectBranch(id), prevHash))
		{
			assert(false);
			return true;
		}
		if (!node->isRoot())
		{ // we may have made this a node with 1 or 0 children
			int bc = node->getBranchCount();
			if (bc == 0)
			{
#ifdef DEBUG
				std::cerr << "delItem makes empty node" << std::endl;
#endif
				prevHash=uint256();
				if (!mTNByID.erase(*node))
					assert(false);
			}
			else if (bc == 1)
			{ // pull up on the thread
				SHAMapItem::pointer item = onlyBelow(node.get());
				if (item)
				{
					eraseChildren(node);
#ifdef ST_DEBUG
					std::cerr << "Making item node " << *node << std::endl;
#endif
					node->setItem(item, type);
				}
				prevHash = node->getNodeHash();
				assert(prevHash.isNonZero());
			}
			else
			{
				prevHash = node->getNodeHash();
				assert(prevHash.isNonZero());
			}
		}
		else assert(stack.empty());
	}
	return true;
}

bool SHAMap::addGiveItem(SHAMapItem::ref item, bool isTransaction, bool hasMeta)
{ // add the specified item, does not update
#ifdef ST_DEBUG
	std::cerr << "aGI " << item->getTag() << std::endl;
#endif

	uint256 tag = item->getTag();
	SHAMapTreeNode::TNType type = !isTransaction ? SHAMapTreeNode::tnACCOUNT_STATE :
		(hasMeta ? SHAMapTreeNode::tnTRANSACTION_MD : SHAMapTreeNode::tnTRANSACTION_NM);

	boost::recursive_mutex::scoped_lock sl(mLock);
	assert(mState != smsImmutable);

	std::stack<SHAMapTreeNode::pointer> stack = getStack(tag, true, false);
	if (stack.empty())
		throw std::runtime_error("missing node");

	SHAMapTreeNode::pointer node = stack.top();
	stack.pop();

	if (node->isLeaf() && (node->peekItem()->getTag() == tag))
		throw std::runtime_error("addGiveItem ends on leaf with same tag");

	uint256 prevHash;
	returnNode(node, true);

	if (node->isInner())
	{ // easy case, we end on an inner node
#ifdef ST_DEBUG
		std::cerr << "aGI inner " << *node << std::endl;
#endif
		int branch = node->selectBranch(tag);
		assert(node->isEmptyBranch(branch));
		SHAMapTreeNode::pointer newNode =
			boost::make_shared<SHAMapTreeNode>(node->getChildNodeID(branch), item, type, mSeq);
		if (!mTNByID.insert(std::make_pair(SHAMapNode(*newNode), newNode)).second)
		{
			std::cerr << "Node: " << *node << std::endl;
			std::cerr << "NewNode: " << *newNode << std::endl;
			dump();
			assert(false);
			throw std::runtime_error("invalid inner node");
		}
		trackNewNode(newNode);
		node->setChildHash(branch, newNode->getNodeHash());
	}
	else
	{ // this is a leaf node that has to be made an inner node holding two items
#ifdef ST_DEBUG
		std::cerr << "aGI leaf " << *node << std::endl;
		std::cerr << "Existing: " << node->peekItem()->getTag() << std::endl;
#endif
		SHAMapItem::pointer otherItem = node->peekItem();
		assert(otherItem && (tag != otherItem->getTag()));

		node->makeInner();

		int b1, b2;

		while ((b1 = node->selectBranch(tag)) == (b2 = node->selectBranch(otherItem->getTag())))
		{ // we need a new inner node, since both go on same branch at this level
#ifdef ST_DEBUG
			std::cerr << "need new inner node at " << node->getDepth() << ", "
				<< b1 << "==" << b2 << std::endl;
#endif
			SHAMapTreeNode::pointer newNode =
				boost::make_shared<SHAMapTreeNode>(mSeq, node->getChildNodeID(b1));
			newNode->makeInner();
			if (!mTNByID.insert(std::make_pair(SHAMapNode(*newNode), newNode)).second)
				assert(false);
			stack.push(node);
			node = newNode;
			trackNewNode(node);
		}

		// we can add the two leaf nodes here
		assert(node->isInner());
		SHAMapTreeNode::pointer newNode =
			boost::make_shared<SHAMapTreeNode>(node->getChildNodeID(b1), item, type, mSeq);
		assert(newNode->isValid() && newNode->isLeaf());
		if (!mTNByID.insert(std::make_pair(SHAMapNode(*newNode), newNode)).second)
			assert(false);
		node->setChildHash(b1, newNode->getNodeHash()); // OPTIMIZEME hash op not needed
		trackNewNode(newNode);

		newNode = boost::make_shared<SHAMapTreeNode>(node->getChildNodeID(b2), otherItem, type, mSeq);
		assert(newNode->isValid() && newNode->isLeaf());
		if (!mTNByID.insert(std::make_pair(SHAMapNode(*newNode), newNode)).second)
			assert(false);
		node->setChildHash(b2, newNode->getNodeHash());
		trackNewNode(newNode);
	}

	dirtyUp(stack, tag, node->getNodeHash());
	return true;
}

bool SHAMap::addItem(const SHAMapItem& i, bool isTransaction, bool hasMetaData)
{
	return addGiveItem(boost::make_shared<SHAMapItem>(i), isTransaction, hasMetaData);
}

bool SHAMap::updateGiveItem(SHAMapItem::ref item, bool isTransaction, bool hasMeta)
{ // can't change the tag but can change the hash
	uint256 tag = item->getTag();

	boost::recursive_mutex::scoped_lock sl(mLock);
	assert(mState != smsImmutable);

	std::stack<SHAMapTreeNode::pointer> stack = getStack(tag, true, false);
	if (stack.empty()) throw std::runtime_error("missing node");

	SHAMapTreeNode::pointer node = stack.top();
	stack.pop();

	if (!node->isLeaf() || (node->peekItem()->getTag() != tag))
	{
		assert(false);
		return false;
	}

	returnNode(node, true);
	if (!node->setItem(item, !isTransaction ? SHAMapTreeNode::tnACCOUNT_STATE :
		(hasMeta ? SHAMapTreeNode::tnTRANSACTION_MD : SHAMapTreeNode::tnTRANSACTION_NM)))
	{
		cLog(lsWARNING) << "SHAMap setItem, no change";
		return true;
	}

	dirtyUp(stack, tag, node->getNodeHash());
	return true;
}

void SHAMapItem::dump()
{
	std::cerr << "SHAMapItem(" << mTag << ") " << mData.size() << "bytes" << std::endl;
}

SHAMapTreeNode::pointer SHAMap::fetchNodeExternal(const SHAMapNode& id, const uint256& hash)
{
	if (!theApp->running())
		throw SHAMapMissingNode(mType, id, hash);

	HashedObject::pointer obj(theApp->getHashedObjectStore().retrieve(hash));
	if (!obj)
	{
//		Log(lsTRACE) << "fetchNodeExternal: missing " << hash;
		throw SHAMapMissingNode(mType, id, hash);
	}

	try
	{
		SHAMapTreeNode::pointer ret = boost::make_shared<SHAMapTreeNode>(id, obj->getData(), mSeq - 1, snfPREFIX);
		if (id != *ret)
		{
			Log(lsFATAL) << "id:" << id << ", got:" << *ret;
			assert(false);
			return SHAMapTreeNode::pointer();
		}
		if (ret->getNodeHash() != hash)
		{
			Log(lsFATAL) << "Hashes don't match";
			assert(false);
			return SHAMapTreeNode::pointer();
		}
		return ret;
	}
	catch (...)
	{
		cLog(lsWARNING) << "fetchNodeExternal gets an invalid node: " << hash;
		throw SHAMapMissingNode(mType, id, hash);
	}
}

void SHAMap::fetchRoot(const uint256& hash)
{
	if (sLog(lsTRACE))
	{
		if (mType == smtTRANSACTION)
			Log(lsTRACE) << "Fetch root TXN node " << hash;
		else if (mType == smtSTATE)
			Log(lsTRACE) << "Fetch root STATE node " << hash;
		else
			Log(lsTRACE) << "Fetch root SHAMap node " << hash;
	}
	root = fetchNodeExternal(SHAMapNode(), hash);
	mTNByID[*root] = root;
	assert(root->getNodeHash() == hash);
}

int SHAMap::armDirty()
{ // begin saving dirty nodes
	mDirtyNodes = boost::make_shared< boost::unordered_map<SHAMapNode, SHAMapTreeNode::pointer> >();
	return ++mSeq;
}

int SHAMap::flushDirty(SHADirtyMap& map, int maxNodes, HashedObjectType t, uint32 seq)
{
	int flushed = 0;
	Serializer s;

	for(SHADirtyMap::iterator it = map.begin(); it != map.end(); it = map.erase(it))
	{
//		tLog(t == hotTRANSACTION_NODE, lsDEBUG) << "TX node write " << it->first;
//		tLog(t == hotACCOUNT_NODE, lsDEBUG) << "STATE node write " << it->first;
		s.erase();
		it->second->addRaw(s, snfPREFIX);
		if (s.getSHA512Half() != it->second->getNodeHash())
		{
			cLog(lsFATAL) << *(it->second);
			cLog(lsFATAL) << lexical_cast_i(s.getDataLength());
			cLog(lsFATAL) << s.getSHA512Half() << " != " << it->second->getNodeHash();
			assert(false);
		}
		theApp->getHashedObjectStore().store(t, seq, s.peekData(), it->second->getNodeHash());
		if (flushed++ >= maxNodes)
			return flushed;
	}

	return flushed;
}

boost::shared_ptr<SHAMap::SHADirtyMap> SHAMap::disarmDirty()
{ // stop saving dirty nodes
	boost::shared_ptr<SHADirtyMap> ret;
	ret.swap(mDirtyNodes);
	return ret;
}

SHAMapTreeNode::pointer SHAMap::getNode(const SHAMapNode& nodeID)
{
	boost::recursive_mutex::scoped_lock sl(mLock);

	SHAMapTreeNode::pointer node = checkCacheNode(nodeID);
	if (node) return node;

	node = root;
	while (nodeID != *node)
	{
		int branch = node->selectBranch(nodeID.getNodeID());
		assert(branch >= 0);
		if ((branch < 0) || node->isEmptyBranch(branch))
			return SHAMapTreeNode::pointer();

		node = getNode(node->getChildNodeID(branch), node->getChildHash(branch), false);
		if (!node) throw std::runtime_error("missing node");
	}
	return node;
}

bool SHAMap::getPath(const uint256& index, std::vector< std::vector<unsigned char> >& nodes, SHANodeFormat format)
{
	// Return the path of nodes to the specified index in the specified format
	// Return value: true = node present, false = node not present

	boost::recursive_mutex::scoped_lock sl(mLock);
	SHAMapTreeNode* inNode = root.get();

	while (!inNode->isLeaf())
	{
		Serializer s;
		inNode->addRaw(s, format);
		nodes.push_back(s.peekData());

		int branch = inNode->selectBranch(index);
		if (inNode->isEmptyBranch(branch)) // paths leads to empty branch
			return false;
		inNode = getNodePointer(inNode->getChildNodeID(branch), inNode->getChildHash(branch));
		if (!inNode)
			throw SHAMapMissingNode(mType, inNode->getChildNodeID(branch), inNode->getChildHash(branch), index);
	}

	if (inNode->getTag() != index) // path leads to different leaf
		return false;

	// path lead to the requested leaf
	Serializer s;
	inNode->addRaw(s, format);
	nodes.push_back(s.peekData());
	return true;
}

void SHAMap::dump(bool hash)
{
#if 0
	std::cerr << "SHAMap::dump" << std::endl;
	SHAMapItem::pointer i=peekFirstItem();
	while (i)
	{
		std::cerr << "Item: id=" << i->getTag() << std::endl;
		i = peekNextItem(i->getTag());
	}
	std::cerr << "SHAMap::dump done" << std::endl;
#endif

	std::cerr << " MAP Contains" << std::endl;
	boost::recursive_mutex::scoped_lock sl(mLock);
	for(boost::unordered_map<SHAMapNode, SHAMapTreeNode::pointer>::iterator it = mTNByID.begin();
			it != mTNByID.end(); ++it)
	{
		std::cerr << it->second->getString() << std::endl;
		if (hash)
			std::cerr << "   " << it->second->getNodeHash() << std::endl;
	}

}

static std::vector<unsigned char>IntToVUC(int v)
{
	std::vector<unsigned char> vuc;
	for (int i = 0; i < 32; ++i)
		vuc.push_back(static_cast<unsigned char>(v));
	return vuc;
}

BOOST_AUTO_TEST_SUITE(SHAMap_suite)

BOOST_AUTO_TEST_CASE( SHAMap_test )
{ // h3 and h4 differ only in the leaf, same terminal node (level 19)
	cLog(lsTRACE) << "SHAMap test";
	uint256 h1, h2, h3, h4, h5;
	h1.SetHex("092891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");
	h2.SetHex("436ccbac3347baa1f1e53baeef1f43334da88f1f6d70d963b833afd6dfa289fe");
	h3.SetHex("b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
	h4.SetHex("b92891fe4ef6cee585fdc6fda2e09eb4d386363158ec3321b8123e5a772c6ca8");
	h5.SetHex("a92891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");

	SHAMap sMap(smtFREE);
	SHAMapItem i1(h1, IntToVUC(1)), i2(h2, IntToVUC(2)), i3(h3, IntToVUC(3)), i4(h4, IntToVUC(4)), i5(h5, IntToVUC(5));

	if (!sMap.addItem(i2, true, false)) BOOST_FAIL("no add");
	if (!sMap.addItem(i1, true, false)) BOOST_FAIL("no add");

	SHAMapItem::pointer i;

	i = sMap.peekFirstItem();
	if (!i || (*i != i1)) BOOST_FAIL("bad traverse");
	i = sMap.peekNextItem(i->getTag());
	if (!i || (*i != i2)) BOOST_FAIL("bad traverse");
	i = sMap.peekNextItem(i->getTag());
	if (i) BOOST_FAIL("bad traverse");

	sMap.addItem(i4, true, false);
	sMap.delItem(i2.getTag());
	sMap.addItem(i3, true, false);

	i = sMap.peekFirstItem();
	if (!i || (*i != i1)) BOOST_FAIL("bad traverse");
	i = sMap.peekNextItem(i->getTag());
	if (!i || (*i != i3)) BOOST_FAIL("bad traverse");
	i = sMap.peekNextItem(i->getTag());
	if (!i || (*i != i4)) BOOST_FAIL("bad traverse");
	i = sMap.peekNextItem(i->getTag());
	if (i) BOOST_FAIL("bad traverse");

	cLog(lsTRACE) << "SHAMap snap test";
	uint256 mapHash = sMap.getHash();
	SHAMap::pointer map2 = sMap.snapShot(false);
	if (sMap.getHash() != mapHash) BOOST_FAIL("bad snapshot");
	if (map2->getHash() != mapHash) BOOST_FAIL("bad snapshot");
	if (!sMap.delItem(sMap.peekFirstItem()->getTag())) BOOST_FAIL("bad mod");
	if (sMap.getHash() == mapHash) BOOST_FAIL("bad snapshot");
	if (map2->getHash() != mapHash) BOOST_FAIL("bad snapshot");
}

BOOST_AUTO_TEST_SUITE_END();

// vim:ts=4
