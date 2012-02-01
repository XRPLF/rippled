#include "Serializer.h"
#include "BitcoinUtil.h"
#include "SHAMap.h"

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>

std::string SHAMapNode::getString() const
{
	std::string ret="NodeID(";
	ret+=boost::lexical_cast<std::string>(mDepth);
	ret+=",";
	ret+=mNodeID.GetHex();
	ret+=")";
	return ret;
}

uint256 SHAMapNode::smMasks[21];

bool SHAMapNode::operator<(const SHAMapNode &s) const
{
	if(s.mDepth<mDepth) return true;
	if(s.mDepth>mDepth) return false;
	return mNodeID<s.mNodeID;
}

bool SHAMapNode::operator>(const SHAMapNode &s) const
{
	if(s.mDepth<mDepth) return false;
	if(s.mDepth>mDepth) return true;
	return mNodeID>s.mNodeID;
}

bool SHAMapNode::operator<=(const SHAMapNode &s) const
{
	if(s.mDepth<mDepth) return true;
	if(s.mDepth>mDepth) return false;
	return mNodeID<=s.mNodeID;
}

bool SHAMapNode::operator>=(const SHAMapNode &s) const
{
	if(s.mDepth<mDepth) return false;
	if(s.mDepth>mDepth) return true;
	return mNodeID>=s.mNodeID;
}

bool SHAMapNode::operator==(const SHAMapNode &s) const
{
	return (s.mDepth==mDepth) && (s.mNodeID==mNodeID);
}

bool SHAMapNode::operator!=(const SHAMapNode &s) const
{
	return (s.mDepth!=mDepth) || (s.mNodeID!=mNodeID);
}

bool SHAMapNode::operator==(const uint256 &s) const
{
	return s==mNodeID;
}

bool SHAMapNode::operator!=(const uint256 &s) const
{
	return s!=mNodeID;
}

void SHAMapNode::ClassInit()
{ // set up the depth masks
	uint256 selector;
	for(int i=0; i<=leafDepth; i++)
	{
		smMasks[i]=selector;
		*(selector.begin()+i)=0x1F;
	}
}

uint256 SHAMapNode::getNodeID(int depth, const uint256& hash)
{
	assert(depth>=0 && depth<=leafDepth);
	return hash & smMasks[depth];
}

SHAMapNode::SHAMapNode(int depth, const uint256 &hash) : mDepth(depth)
{ // canonicalize the hash to a node ID for this depth
	assert(depth>=0 && depth<=leafDepth);
	mNodeID = getNodeID(depth, hash);
}

SHAMapNode SHAMapNode::getChildNodeID(int m)
{
	assert(!isLeaf());
	assert((m>=0) && (m<32));

	uint256 branch=m;
	branch<<=mDepth*8;

	SHAMapNode ret(mDepth+1, mNodeID | branch);
	return ret;
}

int SHAMapNode::selectBranch(const uint256 &hash)
{
	if(isLeaf())	// no nodes under this node
	{
		assert(false);
		return -1;
	}
	if((hash&smMasks[mDepth])!=mNodeID)
	{
		assert(false);
		return -1;	// does not go under this node
	}

	uint256 selector(hash & smMasks[mDepth+1]);
	int branch=*(selector.begin()+mDepth);
	assert(branch>=0 && branch<32);
	return branch;
}

void SHAMapNode::dump()
{
	std::cerr << getString() << std::endl;
}

SHAMapLeafNode::SHAMapLeafNode(const SHAMapNode& nodeID, uint32 seq) : SHAMapNode(nodeID), mHash(0), mSeq(seq)
{
	assert(nodeID.isLeaf());
}

SHAMapLeafNode::SHAMapLeafNode(const SHAMapLeafNode& node, uint32 seq) : SHAMapNode(node),
		mHash(node.mHash), mItems(node.mItems), mSeq(seq)
{
	assert(node.isLeaf());
}

SHAMapLeafNode::SHAMapLeafNode(const SHAMapNode& id, const std::vector<unsigned char>& rawLeaf, uint32 seq)
	: SHAMapNode(id), mSeq(seq)
{
	Serializer s(rawLeaf);
	int pos=0;
	while(pos<s.getLength())
	{
		uint256 id=s.get256(pos);
		pos+=32;
		uint16 len;
		if(!s.get16(len, pos)) throw SHAMapException(InvalidNode);
		pos+=2;
		if(!id || !len || ((pos+len)>s.getLength())) throw SHAMapException(InvalidNode);
		addUpdateItem(SHAMapItem::pointer(new SHAMapItem(id, s.getRaw(pos, len))), false);
		pos+=len;
	}
	updateHash();
}

void SHAMapLeafNode::addRaw(Serializer &s)
{
	BOOST_FOREACH(SHAMapItem::pointer& nodeItem, mItems)
	{
		s.add256(nodeItem->getTag());
		s.add16(nodeItem->peekData().size());
		s.addRaw(nodeItem->peekData());
	}
}

bool SHAMapLeafNode::hasItem(const uint256& item) const
{
	BOOST_FOREACH(const SHAMapItem::pointer& nodeItem, mItems)
		if(nodeItem->getTag()==item) return true;
	return false;
}

bool SHAMapLeafNode::addUpdateItem(SHAMapItem::pointer item, bool doHash)
{ // The node will almost never have more than one item in it
#ifdef DEBUG
	std::cerr << "Leaf(" << getString() << ")" << std::endl;
	std::cerr << "  addi(" << item->getTag().GetHex() << std::endl;
#endif
	std::list<SHAMapItem::pointer>::iterator it;
	for(it=mItems.begin(); it!=mItems.end(); ++it)
	{
		SHAMapItem &nodeItem=**it;
		if(nodeItem.getTag()==item->getTag())
		{
			if(nodeItem.peekData()==item->peekData())
				return false; // no change
			nodeItem.updateData(item->peekData());
			if(doHash) return updateHash();
			return true;
		}
		if(nodeItem.getTag()>item->getTag())
		{
			mItems.insert(it, item);
			if(doHash) return updateHash();
			return true;
		}
	}
	mItems.push_back(item);

	if(doHash) return updateHash();
	return true;
}	

bool SHAMapLeafNode::delItem(const uint256& tag)
{
	std::list<SHAMapItem::pointer>::iterator it;
	for(it=mItems.begin(); it!=mItems.end(); ++it)
	{
		if((*it)->getTag()==tag)
		{
			mItems.erase(it);
			return updateHash();
		}
	}
	return false;
}

SHAMapItem::pointer SHAMapLeafNode::findItem(const uint256& tag)
{
	BOOST_FOREACH(SHAMapItem::pointer& it, mItems)
		if(it->getTag() == tag) return it;
	return SHAMapItem::pointer();
}

SHAMapItem::pointer SHAMapLeafNode::firstItem()
{
	if(mItems.empty()) return SHAMapItem::pointer();
	return *(mItems.begin());
}

SHAMapItem::pointer SHAMapLeafNode::nextItem(const uint256& tag)
{
#ifdef ST_DEBUG
	std::cerr << "LeafNode::nextItem(" << tag.GetHex() << std::endl;
	BOOST_FOREACH(SHAMapItem::pointer& it, mItems)
		std::cerr << "  item(" << it->getTag().GetHex() << std::endl;
#endif
	std::list<SHAMapItem::pointer>::iterator it;
	for(it=mItems.begin(); it!=mItems.end(); ++it)
	{
		if((*it)->getTag()==tag)
		{
			if(++it==mItems.end()) return SHAMapItem::pointer();
			return *it;
		}
	}
#ifdef DEBUG
	std::cerr << "nextItem(!found)" << std::endl;
#endif
	return SHAMapItem::pointer();
}

SHAMapItem::pointer SHAMapLeafNode::prevItem(const uint256& tag)
{
	std::list<SHAMapItem::pointer>::reverse_iterator it;
	for(it=mItems.rbegin(); it!=mItems.rend(); ++it)
	{
		if((*it)->getTag()==tag)
		{
			++it;
			if(it==mItems.rend()) return SHAMapItem::pointer();
			return *it;
		}
	}
	return SHAMapItem::pointer();
}

SHAMapItem::pointer SHAMapLeafNode::lastItem()
{
	if(mItems.empty()) return SHAMapItem::pointer();
	return *(mItems.rbegin());
}

bool SHAMapLeafNode::updateHash()
{
	uint256 nh;
	if(!mItems.empty())
	{
		Serializer s;
		BOOST_FOREACH(const SHAMapItem::pointer &mi, mItems)
			s.addRaw(mi->peekData());
		nh=s.getSHA512Half();
	}
	if(nh==mHash) return false;
	mHash=nh;
	return true;
}

void SHAMapLeafNode::dump()
{
	std::cerr << "SHAMapLeafNode(" << getNodeID().GetHex() << ")" << std::endl;
	std::cerr << "  " << mItems.size() << " items" << std::endl;
}

SHAMapInnerNode::SHAMapInnerNode(const SHAMapNode& id, uint32 seq) : SHAMapNode(id), mSeq(seq), mFullBelow(false)
{ // can be root
	assert(id.getDepth()<SHAMapNode::leafDepth);
}

SHAMapInnerNode::SHAMapInnerNode(const SHAMapNode& id, const std::vector<unsigned char>& contents, uint32 seq)
	: SHAMapNode(id), mSeq(seq), mFullBelow(false)
{
	assert(!id.isLeaf());
	assert(contents.size()==32*256/8);
	Serializer s(contents);
	for(int i=0; i<32; i++)
		mHashes[i]=s.get256(i*32);
}

SHAMapInnerNode::SHAMapInnerNode(const SHAMapInnerNode& node, uint32 seq) : SHAMapNode(node), mHash(node.mHash),
		mSeq(seq), mFullBelow(false)
{
	assert(!node.isLeaf());
	memcpy(mHashes, node.mHashes, sizeof(mHashes));
}

std::string SHAMapInnerNode::getString() const
{
	std::string ret="NodeID(";
	ret+=boost::lexical_cast<std::string>(getDepth());
	ret+=",";
	ret+=getNodeID().GetHex();
	ret+=")";
	for(int i=0; i<32; i++)
		if(!isEmptyBranch(i))
		{
			ret+=",b";
			ret+=boost::lexical_cast<std::string>(i);
		}
	return ret;
}

void SHAMapInnerNode::addRaw(Serializer &s)
{
	for(int i=0; i<32; i++)
		s.add256(mHashes[i]);
}
bool SHAMapInnerNode::setChildHash(int m, const uint256 &hash)
{
	assert( (m>=0) && (m<32) );
	if(mHashes[m]==hash)
		return false;
	mHashes[m]=hash;
	return updateHash();
}

const uint256& SHAMapInnerNode::getChildHash(int m) const
{
	assert( (m>=0) && (m<32) );
	return mHashes[m];
}

bool SHAMapInnerNode::updateHash()
{
	int nc=0;
	Serializer s(1024);
	for(int i=0; i<32; i++)
	{
		if(mHashes[i]!=0) nc++;
		s.add256(mHashes[i]);
	}
	uint256 nh=0;
	if(nc!=0)
		nh=s.getSHA512Half();
	if(mHash==nh) return false;
	mHash=nh;
	return true;
}

void SHAMapInnerNode::dump()
{
	std::cerr << "SHAMapInnerNode(" << getDepth() << ", " << getNodeID().GetHex() << ")" << std::endl;

	int children=0;
	for(int i=0; i<32; i++)
		if(!!mHashes[i]) children++;

	std::cerr << "  " << children << " children" << std::endl;
}
