#include "Serializer.h"
#include "BitcoinUtil.h"
#include "SHAMap.h"

#include <boost/foreach.hpp>

SHAMap::SHAMap(int leafDataSize, int leafDataOffset) : mLeafDataSize(leafDataSize), mLeafDataOffset(leafDataOffset)
{
 ;
}

void SHAMap::dirtyUp(const uint256& id, const std::vector<SHAMapInnerNode::pointer>& path)
{ // walk the tree up from through the inner nodes to the root
  // update linking hashes and add nodes to dirty list
	if(mDirtyInnerNodes==NULL) return;

	std::vector<SHAMapInnerNode::pointer>::const_reverse_iterator it;

	it = path.rbegin();
	if(it == path.rend()) return;
	
	(*mDirtyInnerNodes)[**it]=*it;
	uint256 hVal=(*it)->getNodeHash();
	if(hVal==0)
		mInnerNodeByID.erase(**it);
	
	for(++it; it<path.rend(); ++it)
	{
		if(!(*it)->setChildHash((*it)->selectBranch(id), hVal))
			return;
		(*mDirtyInnerNodes)[**it]=*it;
		hVal = (*it)->getNodeHash();
		if(hVal == 0) mInnerNodeByID.erase(**it);
	}
	assert(**it == *root);
}

SHAMapLeafNode::pointer SHAMap::checkCacheLeaf(const SHAMapNode& iNode)
{
	assert(iNode.isLeaf());
	return mLeafByID[iNode];
}


SHAMapLeafNode::pointer SHAMap::walkToLeaf(const uint256& id, bool create,
	 std::vector<SHAMapInnerNode::pointer>& path)
{ // walk down to the leaf that would contain this ID
	// is leaf node in cache
	SHAMapLeafNode::pointer ln=checkCacheLeaf(SHAMapNode(SHAMapNode::leafDepth, id));
	if(ln) return ln;

	// walk tree to leaf
	SHAMapInnerNode::pointer inNode=root;
	path.push_back(inNode);

	for(int i=1; i<SHAMapNode::leafDepth; i++)
	{
		int branch=inNode->selectBranch(id);
		if(branch<0)
		{ // somehow we got on the wrong branch
			assert(false);
			return SHAMapLeafNode::pointer();
		}
		if(inNode->isEmptyBranch(branch))
		{ // no nodes below this one
			if(!create) return SHAMapLeafNode::pointer();
			return createLeaf(*inNode, id, path);
		}
		if(i!=(SHAMapNode::leafDepth)-1)
		{ // child is another inner node
			inNode=getInner(inNode->getChildNodeID(branch), inNode->getChildHash(branch));
			if(inNode==NULL) return SHAMapLeafNode::pointer(); // we don't have the node
			path.push_back(inNode);
		}
		else // child is leaf node
		{
			ln=getLeaf(inNode->getChildNodeID(branch), inNode->getChildHash(branch));
			if(ln==NULL)
			{
				if(!create) return SHAMapLeafNode::pointer();
				return createLeaf(*inNode, id, path);
			}
		}
	}
	
	return ln;
}

SHAMapLeafNode::pointer SHAMap::getLeaf(const SHAMapNode &id, const uint256& hash)
{ // retrieve a leaf whose node hash is known
	assert(!!hash);
	SHAMapLeafNode::pointer leaf=mLeafByID[id];			// is the leaf in memory
	if(leaf) return leaf;

	std::vector<unsigned char> rawNode;					// is it in backing store
	if(!fetchNode(hash, id, rawNode)) return SHAMapLeafNode::pointer();

	Serializer s(rawNode);	
	leaf=SHAMapLeafNode::pointer(new SHAMapLeafNode(id));

	for(int i=0; i<s.getLength(); i+=mLeafDataSize)
	{
		uint256 tag;
		if(mLeafDataOffset<0)
		{
			Serializer inner;
			inner.addRaw(s.getRaw(i, mLeafDataSize));
			tag=inner.getSHA512Half();
		}
		else tag=s.get256(i+mLeafDataOffset);
		leaf->addUpdateItem(SHAMapItem::pointer(new SHAMapItem(tag, s.getRaw(i, mLeafDataSize))));
	}
	leaf->updateHash();
	mLeafByID[*leaf]=leaf;
	assert(leaf->getNodeHash()==hash);
	return leaf;
}

SHAMapInnerNode::pointer SHAMap::getInner(const SHAMapNode &id, const uint256& hash)
{ // retrieve an inner node whose node hash is known
	SHAMapInnerNode::pointer node=mInnerNodeByID[id];
	if(node) return node;
	
	std::vector<unsigned char> rawNode;
	if(!fetchNode(hash, id, rawNode)) return SHAMapInnerNode::pointer();

	node=SHAMapInnerNode::pointer(new SHAMapInnerNode(id, rawNode));
	if(node->getNodeHash()!=hash)
	{
		badNode(hash, id);
		return SHAMapInnerNode::pointer();
	}
	mInnerNodeByID[id]=node;
	return node;
}

SHAMapItem::SHAMapItem(const uint256& tag, const std::vector<unsigned char>& data)
	: mTag(tag), mData(data)
{ ; }

SHAMapItem::SHAMapItem(const uint160& tag, const std::vector<unsigned char>& data)
	: mTag(uint160to256(tag)), mData(data)
{ ; }

SHAMapItem::pointer SHAMap::firstItem()
{
	ScopedLock sl(mLock);
	return firstBelow(root);
}

SHAMapItem::pointer SHAMap::lastItem()
{
	ScopedLock sl(mLock);
	return lastBelow(root);
}

SHAMapItem::pointer SHAMap::firstBelow(SHAMapInnerNode::pointer Node)
{
	const uint256 zero;
	int i;

	while(Node->isChildLeaf())
	{
		for(i=0; i<32; i++)
		{
			uint256 cHash(Node->getChildHash(i));
			if(cHash!=zero)
			{
				Node=getInner(Node->getChildNodeID(i), cHash);
				if(!Node) return SHAMapItem::pointer();
				break;
			}
		}
		if(i==32) return SHAMapItem::pointer();
	}
	
	for(int i=0; i<32; i++)
	{
		uint256 cHash=Node->getChildHash(i);
		if(cHash!=zero)
		{
			SHAMapLeafNode::pointer mLeaf=getLeaf(Node->getChildNodeID(i), cHash);
			if(!mLeaf) return SHAMapItem::pointer();
			return mLeaf->firstItem();
		}
	}
	return SHAMapItem::pointer();
}

SHAMapItem::pointer SHAMap::lastBelow(SHAMapInnerNode::pointer Node)
{
	ScopedLock sl(mLock);

	const uint256 zero;
	int i;

	while(Node->isChildLeaf())
	{
		for(i=31; i>=0; i--)
		{
			uint256 cHash(Node->getChildHash(i));
			if(cHash!=0)
			{
				Node=getInner(Node->getChildNodeID(i), cHash);
				if(!Node) return SHAMapItem::pointer();
				break;
			}
		}
		if(i<0) return SHAMapItem::pointer();
	}
	for(int i=31; i>=0; i--)
	{
		uint256 cHash=Node->getChildHash(i);
		if(cHash!=zero)
		{
			SHAMapLeafNode::pointer mLeaf=getLeaf(Node->getChildNodeID(i), cHash);
			if(!mLeaf) return SHAMapItem::pointer();
			return mLeaf->lastItem();
		}
	}
}

SHAMapItem::pointer SHAMap::nextItem(const SHAMapItem &)
{
	// WRITEME
}

SHAMapItem::pointer SHAMap::prevItem(const SHAMapItem &)
{
	// WRITEME
}

SHAMapLeafNode::pointer SHAMap::createLeaf(const SHAMapInnerNode& lowestParent, const uint256& id,
	std::vector<SHAMapInnerNode::pointer>& path)
{
	int depth=lowestParent.getDepth();
	for(int depth=lowestParent.getDepth(); depth<SHAMapNode::leafDepth; depth++)
	{
		SHAMapInnerNode::pointer newNode(new SHAMapInnerNode(SHAMapNode(++depth, id)));
		mInnerNodeByID[*newNode]=newNode;
		path.push_back(newNode);
	}
	
	SHAMapLeafNode::pointer newLeaf(new SHAMapLeafNode(SHAMapNode(SHAMapNode::leafDepth, id)));
	mLeafByID[*newLeaf]=newLeaf;
	return newLeaf;
}

SHAMapItem::pointer SHAMap::peekItem(const uint256& id)
{
	ScopedLock sl(mLock);
	std::vector<SHAMapInnerNode::pointer> path;
	SHAMapLeafNode::pointer leaf=walkToLeaf(id, false, path);
	if(!leaf) return SHAMapItem::pointer();
	return leaf->findItem(id);
}

bool SHAMap::hasItem(const uint256& id)
{ // does the tree have an item with this ID
	ScopedLock sl(mLock);
	std::vector<SHAMapInnerNode::pointer> path;
	SHAMapLeafNode::pointer leaf=walkToLeaf(id, false, path);
	if(!leaf) return false;
	SHAMapItem::pointer item=leaf->findItem(id);
	return (bool) item;
}

bool SHAMap::delItem(const uint256& id)
{ // delete the item with this ID
	ScopedLock sl(mLock);
	std::vector<SHAMapInnerNode::pointer> path;
	SHAMapLeafNode::pointer leaf=walkToLeaf(id, false, path);
	if(!leaf) return false;
	if(!leaf->delItem(id)) return false;
	dirtyUp(id, path);
	return true;
}

bool SHAMap::addGiveItem(const SHAMapItem::pointer item)
{ // add the specified item
	ScopedLock sl(mLock);
	std::vector<SHAMapInnerNode::pointer> path;
	SHAMapLeafNode::pointer leaf=walkToLeaf(item->getTag(), true, path);
	if(!leaf) return false;
	if(leaf->hasItem(item->getTag())) return false;
	if(!leaf->addUpdateItem(item)) return false;
	dirtyUp(item->getTag(), path);
	return true;
}

bool SHAMap::updateGiveItem(SHAMapItem::pointer item)
{
	ScopedLock sl(mLock);
	std::vector<SHAMapInnerNode::pointer> path;
	SHAMapLeafNode::pointer leaf=walkToLeaf(item->getTag(), true, path);
	if(!leaf) return false;
	if(!leaf->addUpdateItem(item)) return false;
	dirtyUp(item->getTag(), path);
	return true;
}

void SHAMapItem::dump()
{
	std::cerr << "SHAMapItem(" << mTag.GetHex() << ") " << mData.size() << "bytes" << std::endl;
}

// overloads for backed maps
bool SHAMap::fetchNode(const uint256 &, const SHAMapNode &, std::vector<unsigned char> &)
{
	return false;
}

bool SHAMap::writeNode(const uint256 &, const SHAMapNode &, const std::vector<unsigned char> &)
{
	return true;
}

void SHAMap::badNode(const uint256 &, const SHAMapNode &)
{
	return;
}
    

void SHAMap::dump()
{
}

bool SHAMap::TestSHAMap()
{
 uint256 h1, h2, h3, h4, h5;
 h1.SetHex("436ccbac3347baa1f1e53baeef1f43334da88f1f6d70d963b833afd6dfa289fe");
 h2.SetHex("b92891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");
 h3.SetHex("b92891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca8");
 h4.SetHex("a92891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");
 h5.SetHex("092891fe4ef6cee585fdc6fda0e09eb4d386363158ec3321b8123e5a772c6ca7");

 SHAMap sMap(32);
// SHAMapItem i1(h1), i2(h2), i3(h3), i4(h4), i5(h5);
 
 sMap.dump();
}

