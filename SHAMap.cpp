#include "Serializer.h"
#include "BitcoinUtil.h"
#include "SHAMap.h"

#include <boost/foreach.hpp>

SHAMap::SHAMap() : mSeq(0)
{
	root=SHAMapInnerNode::pointer(new SHAMapInnerNode(SHAMapNode(SHAMapNode::rootDepth, uint256()), mSeq));
	mInnerNodeByID[*root]=root;
}

void SHAMap::dirtyUp(const uint256& id)
{ // walk the tree up from through the inner nodes to the root
  // update linking hashes and add nodes to dirty list
#ifdef DEBUG
	std::cerr << "dirtyUp(" << id.GetHex() << ")" << std::endl;
#endif
	SHAMapLeafNode::pointer leaf=mLeafByID[SHAMapNode(SHAMapNode::leafDepth, id)];
	if(!leaf) throw SHAMapException(MissingNode);

	uint256 hVal=leaf->getNodeHash();
	if(mDirtyLeafNodes) (*mDirtyLeafNodes)[*leaf]=leaf;
	if(!hVal) mLeafByID.erase(*leaf);

	for(int depth=SHAMapNode::leafDepth-1; depth>=0; depth--)
	{ // walk up the tree to the root updating nodes
		SHAMapInnerNode::pointer node=mInnerNodeByID[SHAMapNode(depth, leaf->getNodeID())];
		if(!node) throw SHAMapException(MissingNode);
		if(!node->setChildHash(node->selectBranch(id), hVal)) return;
		if(mDirtyInnerNodes) (*mDirtyInnerNodes)[*node]=node;
		hVal=node->getNodeHash();
		if(!hVal) mInnerNodeByID.erase(*node);
	}
}

SHAMapLeafNode::pointer SHAMap::checkCacheLeaf(const SHAMapNode& iNode)
{
	assert(iNode.isLeaf());
	return mLeafByID[iNode];
}


SHAMapLeafNode::pointer SHAMap::walkToLeaf(const uint256& id, bool create, bool modify)
{ // walk down to the leaf that would contain this ID
	// is leaf node in cache
#ifdef DEBUG
	std::cerr << "walkToLeaf(" << id.GetHex() << ")";
	if(create) std::cerr << " create";
	if(modify) std::cerr << " modify";
	std::cerr << std::endl;
#endif
	SHAMapLeafNode::pointer ln=checkCacheLeaf(SHAMapNode(SHAMapNode::leafDepth, id));
	if(ln) return returnLeaf(ln, modify);

	// walk tree to leaf
	SHAMapInnerNode::pointer inNode=root;

	for(int i=1; i<SHAMapNode::leafDepth; i++)
	{
		int branch=inNode->selectBranch(id);
		if(branch<0) // somehow we got on the wrong branch
			throw SHAMapException(InvalidNode);
		if(inNode->isEmptyBranch(branch))
		{ // no nodes below this one
#ifdef DEBUG
			std::cerr << "No nodes below level " << i << std::endl;
#endif
			if(!create) return SHAMapLeafNode::pointer();
			return createLeaf(*inNode, id);
		}
		if(i!=(SHAMapNode::leafDepth)-1)
		{ // child is another inner node
			inNode=getInner(inNode->getChildNodeID(branch), inNode->getChildHash(branch), modify);
			if(inNode==NULL) throw SHAMapException(InvalidNode);
		}
		else // child is leaf node
		{
			ln=getLeaf(inNode->getChildNodeID(branch), inNode->getChildHash(branch), modify);
			if(ln==NULL)
			{
 				if(!create) return SHAMapLeafNode::pointer();
				return createLeaf(*inNode, id);
			}
		}
	}
	
	return returnLeaf(ln, modify);
}

SHAMapLeafNode::pointer SHAMap::getLeaf(const SHAMapNode &id, const uint256& hash, bool modify)
{ // retrieve a leaf whose node hash is known
	assert(!!hash);
	if(!id.isLeaf()) return SHAMapLeafNode::pointer();

	SHAMapLeafNode::pointer leaf=mLeafByID[id];			// is the leaf in memory
	if(leaf) return returnLeaf(leaf, modify);

	std::vector<SHAMapItem::pointer> leafData;			// is it in backing store
	if(!fetchLeafNode(hash, id, leafData))
 		throw SHAMapException(MissingNode);

	leaf=SHAMapLeafNode::pointer(new SHAMapLeafNode(id, mSeq));
	BOOST_FOREACH(SHAMapItem::pointer& item, leafData)
		leaf->addUpdateItem(item);
	leaf->updateHash();
	if(leaf->getNodeHash()!=hash) throw SHAMapException(InvalidNode);
	mLeafByID[id]=leaf;
	return leaf;
}

SHAMapInnerNode::pointer SHAMap::getInner(const SHAMapNode &id, const uint256& hash, bool modify)
{ // retrieve an inner node whose node hash is known
	SHAMapInnerNode::pointer node=mInnerNodeByID[id];
	if(node) return returnNode(node, modify);
	
	std::vector<unsigned char> rawNode;
	if(!fetchInnerNode(hash, id, rawNode)) throw SHAMapException(MissingNode);

	node=SHAMapInnerNode::pointer(new SHAMapInnerNode(id, rawNode, mSeq));
	if(node->getNodeHash()!=hash) throw SHAMapException(InvalidNode);

	mInnerNodeByID[id]=node;
	return node;
}

SHAMapLeafNode::pointer SHAMap::returnLeaf(SHAMapLeafNode::pointer leaf, bool modify)
{ // make sure the leaf is suitable for the intended operation (copy on write)
	if(leaf && modify && (leaf->getSeq()!=mSeq))
	{
		leaf=SHAMapLeafNode::pointer(new SHAMapLeafNode(*leaf, mSeq));
		mLeafByID[*leaf]=leaf;
		if(mDirtyLeafNodes) (*mDirtyLeafNodes)[*leaf]=leaf;
	}
	return leaf;
}

SHAMapInnerNode::pointer SHAMap::returnNode(SHAMapInnerNode::pointer node, bool modify)
{ // make sure the node is suitable for the intended operation (copy on write)
	if(node && modify && (node->getSeq()!=mSeq))
	{
		node=SHAMapInnerNode::pointer(new SHAMapInnerNode(*node, mSeq));
		mInnerNodeByID[*node]=node;
		if(mDirtyInnerNodes) (*mDirtyInnerNodes)[*node]=node;
	}
	return node;
}

SHAMapItem::SHAMapItem(const uint256& tag, const std::vector<unsigned char>& data)
	: mTag(tag), mData(data)
{ ; }

SHAMapItem::SHAMapItem(const uint160& tag, const std::vector<unsigned char>& data)
	: mTag(uint160to256(tag)), mData(data)
{ ; }

SHAMapItem::pointer SHAMap::peekFirstItem()
{
	ScopedLock sl(mLock);
	return firstBelow(root);
}

SHAMapItem::pointer SHAMap::peekLastItem()
{
	ScopedLock sl(mLock);
	return lastBelow(root);
}

SHAMapItem::pointer SHAMap::firstBelow(SHAMapInnerNode::pointer node)
{
#ifdef DEBUG
	std::cerr << "firstBelow(" << node->getString() << ")" << std::endl;
#endif

	int i;

	while(!node->isChildLeaf())
	{
		for(i=0; i<32; i++)
		{
			uint256 cHash(node->getChildHash(i));
			if(!!cHash)
			{
				node=getInner(node->getChildNodeID(i), cHash, false);
				if(!node) throw SHAMapException(MissingNode);
				break;
			}
		}
		if(i==32) return SHAMapItem::pointer();
	}
	assert(node->isChildLeaf());
	
	for(int i=0; i<32; i++)
	{
		uint256 cHash=node->getChildHash(i);
		if(!!cHash)
		{
			SHAMapLeafNode::pointer mLeaf=getLeaf(node->getChildNodeID(i), cHash, false);
			if(!mLeaf) throw SHAMapException(MissingNode);
			return mLeaf->firstItem();
		}
	}
	return SHAMapItem::pointer();
}

SHAMapItem::pointer SHAMap::lastBelow(SHAMapInnerNode::pointer node)
{
	ScopedLock sl(mLock);

	const uint256 zero;
	int i;

	while(!node->isChildLeaf())
	{
		for(i=31; i>=0; i--)
		{
			uint256 cHash(node->getChildHash(i));
			if(cHash!=0)
			{
				node=getInner(node->getChildNodeID(i), cHash, false);
				if(!node) return SHAMapItem::pointer();
				break;
			}
		}
		if(i<0) return SHAMapItem::pointer();
	}
	for(int i=31; i>=0; i--)
	{
		uint256 cHash=node->getChildHash(i);
		if(cHash!=zero)
		{
			SHAMapLeafNode::pointer mLeaf=getLeaf(node->getChildNodeID(i), cHash, false);
			if(!mLeaf) return SHAMapItem::pointer();
			return mLeaf->lastItem();
		}
	}
}

SHAMapItem::pointer SHAMap::peekNextItem(const uint256& id)
{ // Get a pointer to the next item in the tree after a given item - item must be in tree
	ScopedLock sl(mLock);

	SHAMapLeafNode::pointer leaf=walkToLeaf(id, false, false);
	if(!leaf) return SHAMapItem::pointer();

	// is there another item in this leaf? (there almost never will be)
	SHAMapItem::pointer next=leaf->nextItem(id);
	if(next) return next;

	for(int depth=SHAMapNode::leafDepth-1; depth>=0; depth--)
	{ // walk up the tree until we find a node with a subsequent child
		SHAMapInnerNode::pointer node=mInnerNodeByID[SHAMapNode(depth, id)];
		if(!node)
		{
#ifdef DEBUG
			std::cerr << "InnerNode missing: " << SHAMapNode(depth,id).getString() << std::endl;
#endif
			throw SHAMapException(MissingNode);
		}
		for(int i=node->selectBranch(id)+1; i<32; i++)
			if(!!node->getChildHash(i))
			{ // node has a subsequent child
				SHAMapNode nextNode(node->getChildNodeID(i));
				const uint256& nextHash(node->getChildHash(i));
				
				if(nextNode.isLeaf())
				{ // this is a terminal inner node
					leaf=getLeaf(nextNode, nextHash, false);
					if(!leaf) throw SHAMapException(MissingNode);
					next=leaf->firstItem();
					if(!next) throw SHAMapException(InvalidNode);
					return next;
				}

				// the next item is the first item below this node
				SHAMapInnerNode::pointer inner=getInner(nextNode, nextHash, false);
				if(!inner) throw SHAMapException(MissingNode);
				next=firstBelow(inner);
				if(!next) throw SHAMapException(InvalidNode);
				return next;
			}
	}

	// must be last item
	return SHAMapItem::pointer();
}

SHAMapItem::pointer SHAMap::peekPrevItem(const uint256& id)
{
	// WRITEME
}

SHAMapLeafNode::pointer SHAMap::createLeaf(const SHAMapInnerNode& lowestParent, const uint256& id)
{
#ifdef DEBUG
	std::cerr << "createLeaf(" << lowestParent.getString() << std::endl;
	std::cerr << "  for " << id.GetHex() << ")" << std::endl;
#endif
	assert(!!id);
	for(int depth=lowestParent.getDepth()+1; depth<SHAMapNode::leafDepth; depth++)
	{
		SHAMapInnerNode::pointer newNode(new SHAMapInnerNode(SHAMapNode(depth, id), mSeq));
#ifdef DEBUG
		std::cerr << "create node " << newNode->getString() << std::endl;
#endif
		mInnerNodeByID[*newNode]=newNode;
	}
	SHAMapLeafNode::pointer newLeaf(new SHAMapLeafNode(SHAMapNode(SHAMapNode::leafDepth, id), mSeq));
	mLeafByID[*newLeaf]=newLeaf;
#ifdef DEBUG
	std::cerr << "made leaf " << newLeaf->getString() << std::endl;
#endif
	return newLeaf;
}

SHAMapItem::pointer SHAMap::peekItem(const uint256& id)
{
	ScopedLock sl(mLock);
	SHAMapLeafNode::pointer leaf=walkToLeaf(id, false, false);
	if(!leaf) return SHAMapItem::pointer();
	return leaf->findItem(id);
}

bool SHAMap::hasItem(const uint256& id)
{ // does the tree have an item with this ID
	ScopedLock sl(mLock);  
	SHAMapLeafNode::pointer leaf=walkToLeaf(id, false, false);
	if(!leaf) return false;
	SHAMapItem::pointer item=leaf->findItem(id);
	return (bool) item;
}

bool SHAMap::delItem(const uint256& id)
{ // delete the item with this ID
	ScopedLock sl(mLock);
	SHAMapLeafNode::pointer leaf=walkToLeaf(id, false, false);
	if(!leaf) return false;
	if(!leaf->delItem(id)) return false;
	dirtyUp(id);
	return true;
}

bool SHAMap::addGiveItem(const SHAMapItem::pointer item)
{ // add the specified item
	ScopedLock sl(mLock);
	SHAMapLeafNode::pointer leaf=walkToLeaf(item->getTag(), true, true);
	if(!leaf) return false;
	if(leaf->hasItem(item->getTag())) return false;
	if(!leaf->addUpdateItem(item)) return false;
	dirtyUp(item->getTag());
	return true;
}

bool SHAMap::addItem(const SHAMapItem& i)
{
	return addGiveItem(SHAMapItem::pointer(new SHAMapItem(i)));
}

bool SHAMap::updateGiveItem(SHAMapItem::pointer item)
{
	ScopedLock sl(mLock);
	SHAMapLeafNode::pointer leaf=walkToLeaf(item->getTag(), true, true);
	if(!leaf) return false;
	if(!leaf->addUpdateItem(item)) return false;
	dirtyUp(item->getTag());
	return true;
}

void SHAMapItem::dump()
{
	std::cerr << "SHAMapItem(" << mTag.GetHex() << ") " << mData.size() << "bytes" << std::endl;
}

// overloads for backed maps
bool SHAMap::fetchInnerNode(const uint256&, const SHAMapNode&, std::vector<unsigned char>&)
{
	return false;
}

bool SHAMap::fetchLeafNode(const uint256&, const SHAMapNode&, std::vector<SHAMapItem::pointer>&)
{
	return false;
}

bool SHAMap::writeInnerNode(const uint256&, const SHAMapNode&, const std::vector<unsigned char>&)
{
	return true;
}

bool SHAMap::writeLeafNode(const uint256&, const SHAMapNode&, const std::vector<unsigned char>&)
{
	return true;
}

void SHAMap::dump()
{
	std::cerr << "SHAMap::dump" << std::endl;
	SHAMapItem::pointer i=peekFirstItem();
	while(i)
	{
		std::cerr << "Item: id=" << i->getTag().GetHex() << std::endl;
		i=peekNextItem(i->getTag());
	}
	std::cerr << "SHAMap::dump done" << std::endl;
}

static std::vector<unsigned char>IntToVUC(int i)
{
	std::vector<unsigned char> vuc;
	vuc.push_back((unsigned char) i);
	return vuc;
}

bool SHAMap::TestSHAMap()
{
 uint256 h1, h2, h3, h4, h5;
 h1.SetHex("092891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");
 h2.SetHex("436ccbac3347baa1f1e53baeef1f43334da88f1f6d70d963b833afd6dfa289fe");
 h3.SetHex("b92891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");
 h4.SetHex("b92891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca8");
 h5.SetHex("a92891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");

 SHAMap sMap;
 SHAMapItem i1(h1, IntToVUC(1)), i2(h2, IntToVUC(2)), i3(h3, IntToVUC(3)), i4(h4, IntToVUC(4)), i5(h5, IntToVUC(5));

 sMap.addItem(i1);
 sMap.addItem(i2);
 sMap.dump();
}

