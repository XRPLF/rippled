
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

uint256 hash_SMN::mNonce;

std::size_t hash_SMN::operator() (const SHAMapNode& mn) const
{
	return mn.getDepth()
		^ *reinterpret_cast<const std::size_t *>(mn.getNodeID().begin())
		^ *reinterpret_cast<const std::size_t *>(mNonce.begin());
}

std::size_t hash_SMN::operator() (const uint256& u) const
{
	return *reinterpret_cast<const std::size_t *>(u.begin())
		^ *reinterpret_cast<const std::size_t *>(mNonce.begin());
}

SHAMap::SHAMap(uint32 seq) : mSeq(seq), mState(Modifying)
{
	root = boost::make_shared<SHAMapTreeNode>(mSeq, SHAMapNode(0, uint256()));
	root->makeInner();
	mTNByID[*root] = root;
}

SHAMap::pointer SHAMap::snapShot()
{ // Return a new SHAMap that is a snapshot of this one
  // Initially nodes are shared, but CoW is forced on both ledgers
	SHAMap::pointer ret = boost::make_shared<SHAMap>();
	SHAMap& newMap = *ret;
	newMap.mSeq = ++mSeq;
	newMap.mTNByID = mTNByID;
	newMap.root = root;
	newMap.mState = Immutable;
	return ret;
}

std::stack<SHAMapTreeNode::pointer> SHAMap::getStack(const uint256& id, bool include_nonmatching_leaf)
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
		if (hash.isZero()) return stack;

		node = getNode(node->getChildNodeID(branch), hash, false);
		if (!node)
		{
			if (isSynching()) return stack;
			throw SHAMapException(MissingNode);
		}
	}

	if (include_nonmatching_leaf || (node->peekItem()->getTag() == id))
		stack.push(node);

	return stack;
}

void SHAMap::dirtyUp(std::stack<SHAMapTreeNode::pointer>& stack, const uint256& target, uint256 prevHash)
{ // walk the tree up from through the inner nodes to the root
  // update linking hashes and add nodes to dirty list

	assert((mState != Synching) && (mState != Immutable));

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
			std::cerr << "dirtyUp terminates early" << std::endl;
			assert(false);
			return;
		}
#ifdef ST_DEBUG
		std::cerr << "dirtyUp sets branch " << branch << " to " << prevHash.GetHex() << std::endl;
#endif
		prevHash = node->getNodeHash();
		assert(prevHash.isNonZero());
	}
}

SHAMapTreeNode::pointer SHAMap::checkCacheNode(const SHAMapNode& iNode)
{
	boost::unordered_map<SHAMapNode, SHAMapTreeNode::pointer>::iterator it = mTNByID.find(iNode);
	if (it == mTNByID.end()) return SHAMapTreeNode::pointer();
	return it->second;
}

SHAMapTreeNode::pointer SHAMap::walkTo(const uint256& id, bool modify)
{ // walk down to the terminal node for this ID

	SHAMapTreeNode::pointer inNode = root;

	while (!inNode->isLeaf())
	{
		int branch = inNode->selectBranch(id);
		if (inNode->isEmptyBranch(branch)) return inNode;
		uint256 childHash = inNode->getChildHash(branch);

		SHAMapTreeNode::pointer nextNode = getNode(inNode->getChildNodeID(branch), childHash, false);
		if (!nextNode) throw SHAMapException(MissingNode);
		inNode = nextNode;
	}
	if (inNode->getTag() != id) return SHAMapTreeNode::pointer();
	if (modify) returnNode(inNode, true);
	return inNode;
}

SHAMapTreeNode* SHAMap::walkToPointer(const uint256& id)
{
	SHAMapTreeNode* inNode = &*root;
	while (!inNode->isLeaf())
	{
		int branch = inNode->selectBranch(id);
		const uint256& nextHash = inNode->getChildHash(branch);
		if (!nextHash) return NULL;
		inNode = getNodePointer(inNode->getChildNodeID(branch), nextHash);
		if (!inNode) throw SHAMapException(MissingNode);
	}
	return (inNode->getTag() == id) ? inNode : NULL;
}

SHAMapTreeNode::pointer SHAMap::getNode(const SHAMapNode& id, const uint256& hash, bool modify)
{ // retrieve a node whose node hash is known
	SHAMapTreeNode::pointer node = checkCacheNode(id);
	if (node)
	{
		if (node->getNodeHash() != hash)
		{
#ifdef DEBUG
			std::cerr << "Attempt to get node, hash not in tree" << std::endl;
			std::cerr << "ID: " << id.getString() << std::endl;
			std::cerr << "TgtHash " << hash.GetHex() << std::endl;
			std::cerr << "NodHash " << node->getNodeHash().GetHex() << std::endl;
			dump();
#endif
			throw SHAMapException(InvalidNode);
		}
		returnNode(node, modify);
		return node;
	}

	std::vector<unsigned char> nodeData;
	if (!fetchNode(hash, nodeData)) return SHAMapTreeNode::pointer();

	node = boost::make_shared<SHAMapTreeNode>(id, nodeData, mSeq);
	if (node->getNodeHash() != hash) throw SHAMapException(InvalidNode);

	if (!mTNByID.insert(std::make_pair(id, node)).second)
		assert(false);
	return node;
}

SHAMapTreeNode* SHAMap::getNodePointer(const SHAMapNode& id, const uint256& hash)
{ // fast, but you do not hold a reference
	boost::unordered_map<SHAMapNode, SHAMapTreeNode::pointer>::iterator it = mTNByID.find(id);
	if (it != mTNByID.end())
		return &*it->second;

	SHAMapTreeNode::pointer node;
	std::vector<unsigned char> nodeData;
	if (!fetchNode(hash, nodeData)) return NULL;

	node = boost::make_shared<SHAMapTreeNode>(id, nodeData, mSeq);
	if (node->getNodeHash() != hash) throw SHAMapException(InvalidNode);

	if (!mTNByID.insert(std::make_pair(id, node)).second)
		assert(false);
	return &*node;
}

void SHAMap::returnNode(SHAMapTreeNode::pointer& node, bool modify)
{ // make sure the node is suitable for the intended operation (copy on write)
	assert(node->isValid());
	if (node && modify && (node->getSeq() != mSeq))
	{ // have a CoW
		if (mDirtyNodes) (*mDirtyNodes)[*node] = node;
		node = boost::make_shared<SHAMapTreeNode>(*node, mSeq);
		assert(node->isValid());
		mTNByID[*node] = node;
		if (node->isRoot()) root = node;
	}
}

SHAMapItem::SHAMapItem(const uint256& tag, const std::vector<unsigned char>& data)
	: mTag(tag), mData(data)
{ ; }

SHAMapItem::pointer SHAMap::firstBelow(SHAMapTreeNode* node)
{
	// Return the first item below this node
#ifdef ST_DEBUG
	std::cerr << "firstBelow(" << node->getString() << ")" << std::endl;
#endif
	do
	{ // Walk down the tree
		if (node->hasItem()) return node->peekItem();

		bool foundNode = false;
		for (int i = 0; i < 16; ++i)
			if (!node->isEmptyBranch(i))
			{
#ifdef ST_DEBUG
	std::cerr << " FB: node " << node->getString() << std::endl;
	std::cerr << "  has non-empty branch " << i << " : " <<
		node->getChildNodeID(i).getString() << ", " << node->getChildHash(i).GetHex() << std::endl;
#endif
				node = getNodePointer(node->getChildNodeID(i), node->getChildHash(i));
				foundNode = true;
				break;
			}
		if (!foundNode) return SHAMapItem::pointer();
	} while (1);
}

SHAMapItem::pointer SHAMap::lastBelow(SHAMapTreeNode* node)
{
#ifdef DEBUG
	std::cerr << "lastBelow(" << node->getString() << ")" << std::endl;
#endif

	do
	{ // Walk down the tree
		if (node->hasItem()) return node->peekItem();

		bool foundNode = false;
		for (int i = 15; i >= 0; ++i)
			if (!node->isEmptyBranch(i))
			{
				node = getNodePointer(node->getChildNodeID(i), node->getChildHash(i));
				foundNode = true;
				break;
			}
		if (!foundNode) return SHAMapItem::pointer();
	} while (1);
}

SHAMapItem::pointer SHAMap::onlyBelow(SHAMapTreeNode* node)
{
	// If there is only one item below this node, return it
	bool found;
	while (!node->isLeaf())
	{
		found = false;
		SHAMapTreeNode* nextNode;

		for (int i = 0; i < 16; ++i)
			if (!node->isEmptyBranch(i))
			{
				if (found) return SHAMapItem::pointer(); // two leaves below
				nextNode = getNodePointer(node->getChildNodeID(i), node->getChildHash(i));
				found = true;
			}

		if (!found)
		{
			std::cerr << node->getString() << std::endl;
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
	bool erase=false;
	while(node->isInner())
	{
		for(int i=0; i<16; i++)
			if(!node->isEmptyBranch(i))
			{
				SHAMapTreeNode::pointer nextNode = getNode(node->getChildNodeID(i), node->getChildHash(i), false);
				if(erase)
				{
					returnNode(node, true);
					if(mTNByID.erase(*node))
					assert(false);
				}
				erase=true;
				node=nextNode;
				break;
			}
	}
	returnNode(node, true);
	if(mTNByID.erase(*node) == 0)
		assert(false);
	return;
}

SHAMapItem::pointer SHAMap::peekFirstItem()
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	return firstBelow(&*root);
}

SHAMapItem::pointer SHAMap::peekLastItem()
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	return lastBelow(&*root);
}

SHAMapItem::pointer SHAMap::peekNextItem(const uint256& id)
{ // Get a pointer to the next item in the tree after a given item - item must be in tree
	boost::recursive_mutex::scoped_lock sl(mLock);

	std::stack<SHAMapTreeNode::pointer> stack = getStack(id, true);
	while(!stack.empty())
	{
		SHAMapTreeNode::pointer node = stack.top();
		stack.pop();

		if(node->isLeaf())
		{
			if(node->peekItem()->getTag()>id)
				return node->peekItem();
		}
		else for(int i = node->selectBranch(id) + 1; i < 16; ++i)
			if(!node->isEmptyBranch(i))
			{
				node = getNode(node->getChildNodeID(i), node->getChildHash(i), false);
				if (!node) throw SHAMapException(MissingNode);
				SHAMapItem::pointer item = firstBelow(&*node);
				if (!item) throw SHAMapException(MissingNode);
				return item;
			}
	}
	// must be last item
	return SHAMapItem::pointer();
}

SHAMapItem::pointer SHAMap::peekPrevItem(const uint256& id)
{ // Get a pointer to the previous item in the tree after a given item - item must be in tree
	boost::recursive_mutex::scoped_lock sl(mLock);

	std::stack<SHAMapTreeNode::pointer> stack = getStack(id, true);
	while (!stack.empty())
	{
		SHAMapTreeNode::pointer node = stack.top();
		stack.pop();

		if(node->isLeaf())
		{
			if(node->peekItem()->getTag()<id)
				return node->peekItem();
		}
		else for(int i = node->selectBranch(id) - 1; i >= 0; --i)
				if(!node->isEmptyBranch(i))
				{
					node = getNode(node->getChildNodeID(i), node->getChildHash(i), false);
					if(!node) throw SHAMapException(MissingNode);
					SHAMapItem::pointer item = firstBelow(&*node);
					if (!item) throw SHAMapException(MissingNode);
					return item;
				}
	}
	// must be last item
	return SHAMapItem::pointer();
}

SHAMapItem::pointer SHAMap::peekItem(const uint256& id)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	SHAMapTreeNode* leaf = walkToPointer(id);
	if (!leaf) return SHAMapItem::pointer();
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

	std::stack<SHAMapTreeNode::pointer> stack=getStack(id, true);
	if(stack.empty()) throw SHAMapException(MissingNode);

	SHAMapTreeNode::pointer leaf=stack.top();
	stack.pop();
	if( !leaf || !leaf->hasItem() || (leaf->peekItem()->getTag()!=id) )
		return false;

	SHAMapTreeNode::TNType type=leaf->getType();
	returnNode(leaf, true);
	if(mTNByID.erase(*leaf)==0)
		assert(false);

	uint256 prevHash;
	while(!stack.empty())
	{
		SHAMapTreeNode::pointer node=stack.top();
		stack.pop();
		returnNode(node, true);
		assert(node->isInner());

		if(!node->setChildHash(node->selectBranch(id), prevHash))
		{
			assert(false);
			return true;
		}
		if(!node->isRoot())
		{ // we may have made this a node with 1 or 0 children
			int bc=node->getBranchCount();
			if(bc==0)
			{
#ifdef DEBUG
				std::cerr << "delItem makes empty node" << std::endl;
#endif
				prevHash=uint256();
				if(!mTNByID.erase(*node))
					assert(false);
			}
			else if(bc==1)
			{ // pull up on the thread
				SHAMapItem::pointer item = onlyBelow(&*node);
				if(item)
				{
					eraseChildren(node);
#ifdef ST_DEBUG
					std::cerr << "Making item node " << node->getString() << std::endl;
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

bool SHAMap::addGiveItem(SHAMapItem::pointer item, bool isTransaction)
{ // add the specified item, does not update
#ifdef ST_DEBUG
	std::cerr << "aGI " << item->getTag().GetHex() << std::endl;
#endif

	uint256 tag = item->getTag();
	SHAMapTreeNode::TNType type = isTransaction ? SHAMapTreeNode::tnTRANSACTION : SHAMapTreeNode::tnACCOUNT_STATE;

	boost::recursive_mutex::scoped_lock sl(mLock);

	std::stack<SHAMapTreeNode::pointer> stack = getStack(tag, true);
	if (stack.empty()) throw SHAMapException(MissingNode);

	SHAMapTreeNode::pointer node = stack.top();
	stack.pop();

	if (node->isLeaf() && (node->peekItem()->getTag() == tag))
		throw std::runtime_error("addGiveItem ends on leaf with same tag");

	uint256 prevHash;
	returnNode(node, true);

	if(node->isInner())
	{ // easy case, we end on an inner node
#ifdef ST_DEBUG
		std::cerr << "aGI inner " << node->getString() << std::endl;
#endif
		int branch = node->selectBranch(tag);
		assert(node->isEmptyBranch(branch));
		SHAMapTreeNode::pointer newNode =
			boost::make_shared<SHAMapTreeNode>(node->getChildNodeID(branch), item, type, mSeq);
		if(!mTNByID.insert(std::make_pair(SHAMapNode(*newNode), newNode)).second)
		{
			std::cerr << "Node: " << node->getString() << std::endl;
			std::cerr << "NewNode: " << newNode->getString() << std::endl;
			dump();
			assert(false);
			throw SHAMapException(InvalidNode);
		}
		node->setChildHash(branch, newNode->getNodeHash());
	}
	else
	{ // this is a leaf node that has to be made an inner node holding two items
#ifdef ST_DEBUG
		std::cerr << "aGI leaf " << node->getString() << std::endl;
		std::cerr << "Existing: " << node->peekItem()->getTag().GetHex() << std::endl;
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
			if(!mTNByID.insert(std::make_pair(SHAMapNode(*newNode), newNode)).second)
				assert(false);
			stack.push(node);
			node = newNode;
		}

		// we can add the two leaf nodes here
		assert(node->isInner());
		SHAMapTreeNode::pointer newNode =
			boost::make_shared<SHAMapTreeNode>(node->getChildNodeID(b1), item, type, mSeq);
		assert(newNode->isValid() && newNode->isLeaf());
		if (!mTNByID.insert(std::make_pair(SHAMapNode(*newNode), newNode)).second)
			assert(false);
		node->setChildHash(b1, newNode->getNodeHash()); // OPTIMIZEME hash op not needed

		newNode = boost::make_shared<SHAMapTreeNode>(node->getChildNodeID(b2), otherItem, type, mSeq);
		assert(newNode->isValid() && newNode->isLeaf());
		if(!mTNByID.insert(std::make_pair(SHAMapNode(*newNode), newNode)).second)
			assert(false);
		node->setChildHash(b2, newNode->getNodeHash());
	}

	dirtyUp(stack, tag, node->getNodeHash());
	return true;
}

bool SHAMap::addItem(const SHAMapItem& i, bool isTransaction)
{
	return addGiveItem(boost::make_shared<SHAMapItem>(i), isTransaction);
}

bool SHAMap::updateGiveItem(SHAMapItem::pointer item, bool isTransaction)
{ // can't change the tag but can change the hash
	uint256 tag = item->getTag();

	boost::recursive_mutex::scoped_lock sl(mLock);

	std::stack<SHAMapTreeNode::pointer> stack = getStack(tag, true);
	if (stack.empty()) throw SHAMapException(MissingNode);

	SHAMapTreeNode::pointer node = stack.top();
	stack.pop();

	if (!node->isLeaf() || (node->peekItem()->getTag() != tag))
	{
		assert(false);
		return false;
	}

	returnNode(node, true);
	if (!node->setItem(item, isTransaction ? SHAMapTreeNode::tnTRANSACTION : SHAMapTreeNode::tnACCOUNT_STATE))
	{
		Log(lsWARNING) << "SHAMap setItem, no change";
		return true;
	}

	dirtyUp(stack, tag, node->getNodeHash());
	return true;
}

void SHAMapItem::dump()
{
	std::cerr << "SHAMapItem(" << mTag.GetHex() << ") " << mData.size() << "bytes" << std::endl;
}

bool SHAMap::fetchNode(const uint256& hash, std::vector<unsigned char>& data)
{
	HashedObject::pointer obj(HashedObject::retrieve(hash));
	if(!obj) return false;
	data = obj->getData();
	return true;
}

int SHAMap::flushDirty(int maxNodes, HashedObjectType t, uint32 seq)
{
	int flushed = 0;
	Serializer s;

	if(mDirtyNodes)
	{
		while (!mDirtyNodes->empty())
		{
			SHAMapTreeNode::pointer& din = mDirtyNodes->begin()->second;
			s.erase();
			din->addRaw(s);
			HashedObject::store(t, seq, s.peekData(), s.getSHA512Half());
			mDirtyNodes->erase(mDirtyNodes->begin());
			if(flushed++>=maxNodes) return flushed;
		}
	}

	return flushed;
}

SHAMapTreeNode::pointer SHAMap::getNode(const SHAMapNode& nodeID)
{
	boost::recursive_mutex::scoped_lock sl(mLock);

	SHAMapTreeNode::pointer node=checkCacheNode(nodeID);
	if(node) return node;

	node=root;
	while(nodeID!=*node)
	{
		int branch=node->selectBranch(nodeID.getNodeID());
		assert(branch>=0);
		if( (branch<0) || (node->isEmptyBranch(branch)) )
			return SHAMapTreeNode::pointer();

		node=getNode(node->getChildNodeID(branch), node->getChildHash(branch), false);
		if(!node) throw SHAMapException(MissingNode);
	}
	return node;
}

void SHAMap::dump(bool hash)
{
#if 0
	std::cerr << "SHAMap::dump" << std::endl;
	SHAMapItem::pointer i=peekFirstItem();
	while(i)
	{
		std::cerr << "Item: id=" << i->getTag().GetHex() << std::endl;
		i=peekNextItem(i->getTag());
	}
	std::cerr << "SHAMap::dump done" << std::endl;
#endif

	std::cerr << " MAP Contains" << std::endl;
	boost::recursive_mutex::scoped_lock sl(mLock);
	for(boost::unordered_map<SHAMapNode, SHAMapTreeNode::pointer, hash_SMN>::iterator it = mTNByID.begin();
			it != mTNByID.end(); ++it)
	{
		std::cerr << it->second->getString() << std::endl;
		if(hash) std::cerr << "   " << it->second->getNodeHash().GetHex() << std::endl;
	}

}

static std::vector<unsigned char>IntToVUC(int v)
{
	std::vector<unsigned char> vuc;
	for (int i = 0; i < 32; ++i)
		vuc.push_back(static_cast<unsigned char>(v));
	return vuc;
}

BOOST_AUTO_TEST_SUITE(shamap)

BOOST_AUTO_TEST_CASE( SHAMap_test )
{ // h3 and h4 differ only in the leaf, same terminal node (level 19)
	uint256 h1, h2, h3, h4, h5;
	h1.SetHex("092891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");
	h2.SetHex("436ccbac3347baa1f1e53baeef1f43334da88f1f6d70d963b833afd6dfa289fe");
	h3.SetHex("b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
	h4.SetHex("b92891fe4ef6cee585fdc6fda2e09eb4d386363158ec3321b8123e5a772c6ca8");
	h5.SetHex("a92891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");

	SHAMap sMap;
	SHAMapItem i1(h1, IntToVUC(1)), i2(h2, IntToVUC(2)), i3(h3, IntToVUC(3)), i4(h4, IntToVUC(4)), i5(h5, IntToVUC(5));

	if(!sMap.addItem(i2, true)) BOOST_FAIL("no add");
	if(!sMap.addItem(i1, true)) BOOST_FAIL("no add");

	SHAMapItem::pointer i;

	i=sMap.peekFirstItem();
	assert(!!i && (*i==i1));
	i=sMap.peekNextItem(i->getTag());
	assert(!!i && (*i==i2));
	i=sMap.peekNextItem(i->getTag());
	assert(!i);

	sMap.addItem(i4, true);
	sMap.delItem(i2.getTag());
	sMap.addItem(i3, true);

	i=sMap.peekFirstItem();
	assert(!!i && (*i==i1));
	i=sMap.peekNextItem(i->getTag());
	assert(!!i && (*i==i3));
	i=sMap.peekNextItem(i->getTag());
	assert(!!i && (*i==i4));
	i=sMap.peekNextItem(i->getTag());
	assert(!i);

}

BOOST_AUTO_TEST_SUITE_END();

// vim:ts=4
