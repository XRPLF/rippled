#include "Serializer.h"
#include "BitcoinUtil.h"
#include "SHAMap.h"

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>

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
	if(!hVal)
	{
#ifdef ST_DEBUG
		std::cerr << "  erasingL " << leaf->getString() << std::endl;
#endif
		mLeafByID.erase(*leaf);
	}

	for(int depth=SHAMapNode::leafDepth-1; depth>=0; depth--)
	{ // walk up the tree to the root updating nodes
		SHAMapInnerNode::pointer node=mInnerNodeByID[SHAMapNode(depth, leaf->getNodeID())];
		if(!node) throw SHAMapException(MissingNode);
		if(!node->setChildHash(node->selectBranch(id), hVal))
		{
#ifdef ST_DEBUG
			std::cerr << "  no change@ " << node->getString() << std::endl;
#endif
			return;
		}
#ifdef ST_DEBUG
		std::cerr << "Dirty " << node->getString() << std::endl;
#endif
		if(mDirtyInnerNodes) (*mDirtyInnerNodes)[*node]=node;
		hVal=node->getNodeHash();
		if(!hVal)
		{
#ifdef ST_DEBUG
			std::cerr << "  erasingN " << node->getString() << std::endl;
#endif
			mInnerNodeByID.erase(*node);
		}
	}
}

SHAMapLeafNode::pointer SHAMap::checkCacheLeaf(const SHAMapNode& iNode)
{
	assert(iNode.isLeaf());
	SHAMapLeafNode::pointer leaf=mLeafByID[iNode];
#ifdef ST_DEBUG
	if(!leaf) std::cerr << "Leaf(" << iNode.getString() << ") not in cache" << std::endl;
	else std::cerr << "Leaf(" << iNode.getString() << ") found in cache" << std::endl;
#endif
	return leaf;
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

	for(int i=0; i<SHAMapNode::leafDepth; i++)
	{
		int branch=inNode->selectBranch(id);
		if(branch<0) // somehow we got on the wrong branch
			throw SHAMapException(InvalidNode);
		if(inNode->isEmptyBranch(branch))
		{ // no nodes below this one
#ifdef DEBUG
			std::cerr << "No nodes below level " << i << ", branch " << branch << std::endl;
			std::cerr << "  terminal node is " << inNode->getString() << std::endl;
#endif
			if(!create) return SHAMapLeafNode::pointer();
			return createLeaf(*inNode, id);
		}
		if(inNode->isChildLeaf())
		{ // child is leaf node
			ln=getLeaf(inNode->getChildNodeID(branch), inNode->getChildHash(branch), modify);
			if(ln==NULL)
			{
 				if(!create) return SHAMapLeafNode::pointer();
				return createLeaf(*inNode, id);
			}
		}
		else
		{ // child is another inner node
			inNode=getInner(inNode->getChildNodeID(branch), inNode->getChildHash(branch), modify);
			if(inNode==NULL) throw SHAMapException(InvalidNode);
		}
	}

	assert(ln || !create);
	return returnLeaf(ln, modify);
}

SHAMapLeafNode::pointer SHAMap::getLeaf(const SHAMapNode& id, const uint256& hash, bool modify)
{ // retrieve a leaf whose node hash is known
	assert(!!hash);
	if(!id.isLeaf()) return SHAMapLeafNode::pointer();

	SHAMapLeafNode::pointer leaf=mLeafByID[id];			// is the leaf in memory
	if(leaf) return returnLeaf(leaf, modify);

	std::vector<unsigned char> leafData;
	if(!fetchNode(hash, leafData)) throw SHAMapException(MissingNode);
	leaf=SHAMapLeafNode::pointer(new SHAMapLeafNode(id, leafData, mSeq));
	if(leaf->getNodeHash()!=hash) throw SHAMapException(InvalidNode);
	mLeafByID[id]=leaf;
	return leaf;
}

SHAMapInnerNode::pointer SHAMap::getInner(const SHAMapNode& id, const uint256& hash, bool modify)
{ // retrieve an inner node whose node hash is known
	SHAMapInnerNode::pointer node=mInnerNodeByID[id];
	if(node) return returnNode(node, modify);
	
	std::vector<unsigned char> rawNode;
	if(!fetchNode(hash, rawNode)) throw SHAMapException(MissingNode);
	node=SHAMapInnerNode::pointer(new SHAMapInnerNode(id, rawNode, mSeq));
	if(node->getNodeHash()!=hash) throw SHAMapException(InvalidNode);

	mInnerNodeByID[id]=node;
	if(id.getDepth()==0) root=node;
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
#ifdef ST_DEBUG
		std::cerr << "Node(" << node->getString() << ") bumpseq" << std::endl;
#endif
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
	boost::recursive_mutex::scoped_lock sl(mLock);
	return firstBelow(root);
}

SHAMapItem::pointer SHAMap::peekLastItem()
{
	boost::recursive_mutex::scoped_lock sl(mLock);
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
			if(!node->isEmptyBranch(i))
			{
				node=getInner(node->getChildNodeID(i), node->getChildHash(i), false);
				if(!node) throw SHAMapException(MissingNode);
				break;
			}

		if(i==32)
		{
#ifdef ST_DEBUG
			std::cerr << "  node:" << node->getString() << " has no children" << std::endl;
#endif
			return SHAMapItem::pointer();
		}
	}

	assert(node->isChildLeaf());
	for(int i=0; i<32; i++)
		if(!node->isEmptyBranch(i))
		{
			SHAMapLeafNode::pointer mLeaf=getLeaf(node->getChildNodeID(i), node->getChildHash(i), false);
			if(!mLeaf) throw SHAMapException(MissingNode);
			SHAMapItem::pointer item=mLeaf->firstItem();
			if(!item) throw SHAMapException(InvalidNode);
			return item;
		}

#ifdef ST_DEBUG
	std::cerr << "  node:" << node->getString() << " has no children" << std::endl;
#endif
	return SHAMapItem::pointer();
}

SHAMapItem::pointer SHAMap::lastBelow(SHAMapInnerNode::pointer node)
{
	boost::recursive_mutex::scoped_lock sl(mLock);

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
	return SHAMapItem::pointer();
}

SHAMapItem::pointer SHAMap::peekNextItem(const uint256& id)
{ // Get a pointer to the next item in the tree after a given item - item must be in tree
	boost::recursive_mutex::scoped_lock sl(mLock);

	SHAMapLeafNode::pointer leaf=walkToLeaf(id, false, false);
	if(!leaf)
	{
#ifdef DEBUG
		std::cerr << "peekNextItem: current not found" << std::endl;
#endif
		return SHAMapItem::pointer();
	}

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
#ifdef ST_DEBUG
		std::cerr << "  UpTo " << node->getString() << std::endl;
#endif
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
#ifdef ST_DEBUG
	std::cerr << "   peekNext off end" << std::endl;
#endif
	// must be last item
	return SHAMapItem::pointer();
}

SHAMapItem::pointer SHAMap::peekPrevItem(const uint256& id)
{
	boost::recursive_mutex::scoped_lock sl(mLock);

	SHAMapLeafNode::pointer leaf=walkToLeaf(id, false, false);
	if(!leaf) return SHAMapItem::pointer();

	// is there another item in this leaf? (there almost never will be)
	SHAMapItem::pointer prev=leaf->prevItem(id);
	if(prev) return prev;

	for(int depth=SHAMapNode::leafDepth-1; depth>=0; depth--)
	{ // walk up the tree until we find a node with a previous child
		SHAMapInnerNode::pointer node=mInnerNodeByID[SHAMapNode(depth, id)];
		if(!node)
		{
#ifdef DEBUG
			std::cerr << "InnerNode missing: " << SHAMapNode(depth,id).getString() << std::endl;
#endif
			throw SHAMapException(MissingNode);
		}
		for(int i=node->selectBranch(id)-1; i>=0; i--)
			if(!!node->getChildHash(i))
			{ // node has a subsequent child
				SHAMapNode prevNode(node->getChildNodeID(i));
				const uint256& prevHash(node->getChildHash(i));
				
				if(prevNode.isLeaf())
				{ // this is a terminal inner node
					leaf=getLeaf(prevNode, prevHash, false);
					if(!leaf) throw SHAMapException(MissingNode);
					prev=leaf->firstItem();
					if(!prev) throw SHAMapException(InvalidNode);
					return prev;
				}

				// the next item is the first item below this node
				SHAMapInnerNode::pointer inner=getInner(prevNode, prevHash, false);
				if(!inner) throw SHAMapException(MissingNode);
				prev=lastBelow(inner);
				if(!prev) throw SHAMapException(InvalidNode);
				return prev;
			}
	}

	// must be last item
	return SHAMapItem::pointer();
}

SHAMapLeafNode::pointer SHAMap::createLeaf(const SHAMapInnerNode& lowestParent, const uint256& id)
{ // caller must call dirtyUp if they populate the leaf
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
	boost::recursive_mutex::scoped_lock sl(mLock);
	SHAMapLeafNode::pointer leaf=walkToLeaf(id, false, false);
	if(!leaf) return SHAMapItem::pointer();
	return leaf->findItem(id);
}

bool SHAMap::hasItem(const uint256& id)
{ // does the tree have an item with this ID
	boost::recursive_mutex::scoped_lock sl(mLock);  
	SHAMapLeafNode::pointer leaf=walkToLeaf(id, false, false);
	if(!leaf) return false;
	SHAMapItem::pointer item=leaf->findItem(id);
	return (bool) item;
}

bool SHAMap::delItem(const uint256& id)
{ // delete the item with this ID
	boost::recursive_mutex::scoped_lock sl(mLock);
	SHAMapLeafNode::pointer leaf=walkToLeaf(id, false, false);
	if(!leaf) return false;
	if(!leaf->delItem(id)) return false;
	dirtyUp(id);
	return true;
}

bool SHAMap::addGiveItem(const SHAMapItem::pointer item)
{ // add the specified item, does not update
	boost::recursive_mutex::scoped_lock sl(mLock);
	SHAMapLeafNode::pointer leaf=walkToLeaf(item->getTag(), true, true);
	if(!leaf)
	{
		assert(false);
		return false;
	}
	if(leaf->hasItem(item->getTag()))
	{
#ifdef ST_DEBUG
		std::cerr << "leaf has item we're adding" << std::endl;
#endif
		return false;
	}
	if(!leaf->addUpdateItem(item))
	{
		assert(false);
		return false;
	}
	dirtyUp(item->getTag());
	return true;
}

bool SHAMap::addItem(const SHAMapItem& i)
{
	return addGiveItem(SHAMapItem::pointer(new SHAMapItem(i)));
}

bool SHAMap::updateGiveItem(SHAMapItem::pointer item)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
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

bool SHAMap::fetchNode(const uint256& hash, std::vector<unsigned char>& data)
{
	HashedObject::pointer obj(HashedObject::retrieve(hash));
	if(!obj) return false;
	data=obj->getData();
	return true;
}

int SHAMap::flushDirty(int maxNodes, HashedObjectType t, uint32 seq)
{
	int flushed=0;
	Serializer s;

	if(mDirtyLeafNodes)
	{
		while(!mDirtyLeafNodes->empty())
		{
			SHAMapLeafNode::pointer& dln=mDirtyLeafNodes->begin()->second;
			s.erase();
			dln->addRaw(s);
			HashedObject::store(t, seq, s.peekData(), s.getSHA512Half());
			mDirtyLeafNodes->erase(mDirtyLeafNodes->begin());
			if(flushed++>=maxNodes) return flushed;
		}
	}	

	if(mDirtyInnerNodes)
	{
		while(!mDirtyInnerNodes->empty())
		{
			SHAMapInnerNode::pointer& din=mDirtyInnerNodes->begin()->second;
			s.erase();
			din->addRaw(s);
			HashedObject::store(t, seq, s.peekData(), s.getSHA512Half());
			mDirtyInnerNodes->erase(mDirtyInnerNodes->begin());
			if(flushed++>=maxNodes) return flushed;
		}
	}

	return flushed;
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
{ // h3 and h4 differ only in the leaf, same terminal node (level 19)
 uint256 h1, h2, h3, h4, h5;
 h1.SetHex("092891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");
 h2.SetHex("436ccbac3347baa1f1e53baeef1f43334da88f1f6d70d963b833afd6dfa289fe");
 h3.SetHex("b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");
 h4.SetHex("b92891fe4ef6cee585fdc6fda2e09eb4d386363158ec3321b8123e5a772c6ca8");
 h5.SetHex("a92891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");

 SHAMap sMap;
 SHAMapItem i1(h1, IntToVUC(1)), i2(h2, IntToVUC(2)), i3(h3, IntToVUC(3)), i4(h4, IntToVUC(4)), i5(h5, IntToVUC(5));

 sMap.addItem(i2);
 sMap.addItem(i1);

 SHAMapItem::pointer i=sMap.peekFirstItem();
 assert(!!i && (*i==i1));
 i=sMap.peekNextItem(i->getTag());
 assert(!!i && (*i==i2));
 i=sMap.peekNextItem(i->getTag());
 assert(!i);

 sMap.addItem(i4);
 sMap.delItem(i2.getTag());
 sMap.addItem(i3);
 
 i=sMap.peekFirstItem();
 assert(!!i && (*i==i1));
 i=sMap.peekNextItem(i->getTag());
 assert(!!i && (*i==i3));
 i=sMap.peekNextItem(i->getTag());
 assert(!!i && (*i==i4));
 i=sMap.peekNextItem(i->getTag());
 assert(!i);

 return true;
}

