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
            updateHash();
            return true;
		}
		if(*it>item) break;
	}
	mItems.insert(it, item);
	updateHash();
	return true;
}	

bool SHAMapLeafNode::delItem(const uint256& tag)
{
	std::list<SHAMapItem>::iterator it;
	for(it=mItems.begin(); it!=mItems.end(); it++)
	{
		if(*it==tag)
		{
			mItems.erase(it);
			updateHash();
			return true;
		}
	}
	return false;
}

void SHAMapLeafNode::updateHash(void)
{
}

