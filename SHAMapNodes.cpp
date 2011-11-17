#include "Serializer.h"
#include "BitcoinUtil.h"
#include "SHAMap.h"

#include <boost/foreach.hpp>

uint256 SHAMapNode::smMasks[11];

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

void SHAMapNode::ClassInit()
{ // set up the depth masks
	int i;
	char HexBuf[65];

	for(i=0; i<64; i++) HexBuf[i]='0';
		HexBuf[64]=0;
	for(i=0; i<leafDepth; i++)
	{
		smMasks[i].SetHex(HexBuf);
		HexBuf[2*i]='1';
		HexBuf[2*i+1]='F';
	}
}

uint256 SHAMapNode::getNodeID(int depth, const uint256& hash)
{
	assert(depth>=0 && depth<=leafDepth);
	return hash & smMasks[depth];
}

SHAMapNode::SHAMapNode(int depth, const uint256 &hash)
{ // canonicalize the hash to a node ID for this depth
	assert(depth>=0 && depth<leafDepth);
	mDepth = depth;
	mNodeID = getNodeID(depth, hash);
}

SHAMapNode SHAMapNode::getChildNodeID(int m)
{
	assert(!isLeaf());

	uint256 branch=m;
	branch>>=(mDepth*8);

	return SHAMapNode(mDepth+1, mNodeID | branch);
}

int SHAMapNode::selectBranch(const uint256 &hash)
{
	if(isLeaf())	// no nodes under this node
		return -1;
	if((hash&smMasks[mDepth])!=mNodeID)
		return -1;	// does not go under this node

	uint256 selector=hash&smMasks[mDepth+1];
	int branch=*(selector.begin()+mDepth);

	assert(branch>=0 && branch<32);
	return branch;
}

SHAMapLeafNode::SHAMapLeafNode(const SHAMapNode& nodeID) : SHAMapNode(nodeID), mHash(0)
{
 ;
}

bool SHAMapLeafNode::hasItem(const uint256& item) const
{
	BOOST_FOREACH(const SHAMapItem& nodeItem, mItems)
		if(nodeItem==item) return true;
	return false;
}

bool SHAMapLeafNode::addUpdateItem(const SHAMapItem& item)
{ // The node will almost never have more than one item in it
	std::list<SHAMapItem>::iterator it;
	for(it=mItems.begin(); it!=mItems.end(); it++)
	{
		if(*it==item)
		{
		    if(it->peekData()==item.peekData())
		        return false; // no change
            it->updateData(item.peekData());
            return updateHash();
		}
		if((*it)>item)
		{
			mItems.insert(it, item);
		}
	}
	mItems.push_back(item);
	return updateHash();
}	

bool SHAMapLeafNode::delItem(const uint256& tag)
{
	std::list<SHAMapItem>::iterator it;
	for(it=mItems.begin(); it!=mItems.end(); it++)
	{
		if(*it==tag)
		{
			mItems.erase(it);
			return updateHash();
		}
	}
	return false;
}

bool SHAMapLeafNode::updateHash(void)
{
	uint256 nh;
	if(mItems.size()!=0) nh=0;
	{
		Serializer s;
		BOOST_FOREACH(const SHAMapItem &mi, mItems)
			s.addRaw(mi.peekData());
		nh=s.getSHA512Half();
	}
	if(nh==mHash) return false;
	mHash=nh;
	return true;
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
	Serializer s;
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

