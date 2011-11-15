#include "BitcoinUtil.h"
#include "SHAMap.h"
#include <boost/foreach.hpp>

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
{
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

SHAMapNode::SHAMapNode(int depth, const uint256 &hash)
{
	assert(depth>=0 && depth<leafDepth);
	mDepth=depth;
	mNodeID=getNodeID(depth, hash);
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

void SHAMapLeafNode::updateHash(void)
{
}

bool SHAMapLeafNode::hasHash(const uint256& hash) const
{
	BOOST_FOREACH(const uint256& entry, mHashes)
		if(entry==hash) return true;
	return false;
}

bool SHAMapLeafNode::addHash(const uint256& hash)
{ // The node will almost never have more than one hash in it
	std::list<uint256>::iterator it;
	for(it=mHashes.begin(); it!=mHashes.end(); it++)
	{
		if(*it==hash) return false;
		if(*it>hash) break;
	}
	mHashes.insert(it, hash);
	updateHash();
	return true;
}	

bool SHAMapLeafNode::delHash(const uint256& hash)
{
	std::list<uint256>::iterator it;
	for(it=mHashes.begin(); it!=mHashes.end(); it++)
	{
		if(*it==hash)
		{
			mHashes.erase(it);
			updateHash();
			return true;
		}
	}
	return false;
}
