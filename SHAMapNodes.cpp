
#include <cstring>

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/smart_ptr/make_shared.hpp>

#include <openssl/sha.h>

#include "Serializer.h"
#include "BitcoinUtil.h"
#include "SHAMap.h"

std::string SHAMapNode::getString() const
{
	std::string ret="NodeID(";
	ret+=boost::lexical_cast<std::string>(mDepth);
	ret+=",";
	ret+=mNodeID.GetHex();
	ret+=")";
	return ret;
}

uint256 SHAMapNode::smMasks[64];

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
	for(int i=0; i<64; i+=2)
	{
		smMasks[i]=selector;
		*(selector.begin()+(i/2))=0x0F;
		smMasks[i+1]=selector;
		*(selector.begin()+(i/2))=0xFF;
	}
}

uint256 SHAMapNode::getNodeID(int depth, const uint256& hash)
{
	assert(depth>=0 && depth<64);
	return hash & smMasks[depth];
}

SHAMapNode::SHAMapNode(int depth, const uint256 &hash) : mDepth(depth)
{ // canonicalize the hash to a node ID for this depth
	assert(depth>=0 && depth<64);
	mNodeID = getNodeID(depth, hash);
}

SHAMapNode SHAMapNode::getChildNodeID(int m) const
{ // This can be optimized to avoid the << if needed
	assert((m>=0) && (m<16));

	uint256 branch=m;
	branch<<=mDepth*4;

	return SHAMapNode(mDepth+1, mNodeID | branch);
}

int SHAMapNode::selectBranch(const uint256& hash) const
{ // Which branch would contain the specified hash
	if(mDepth==63)
	{
		assert(false);
		return -1;
	}
	if((hash&smMasks[mDepth])!=mNodeID)
	{
		std::cerr << "selectBranch(" << getString() << std::endl;
		std::cerr << "  " << hash.GetHex() << " off branch" << std::endl;
		assert(false);
		return -1;	// does not go under this node
	}

	int branch=*(hash.begin()+(mDepth/2));
	if(mDepth%2) branch>>=4;
	else branch&=0xf;

	assert(branch>=0 && branch<16);
	return branch;
}

void SHAMapNode::dump() const
{
	std::cerr << getString() << std::endl;
}

SHAMapTreeNode::SHAMapTreeNode(const SHAMapNode& nodeID, uint32 seq) : SHAMapNode(nodeID), mHash(0), mSeq(seq),
	mType(ERROR), mFullBelow(false)
{
}

SHAMapTreeNode::SHAMapTreeNode(const SHAMapTreeNode& node, uint32 seq) : SHAMapNode(node),
		mHash(node.mHash), mItem(node.mItem), mSeq(seq), mType(node.mType), mFullBelow(false)
{
	if(node.mItem)
		mItem=boost::make_shared<SHAMapItem>(*node.mItem);
	else
		memcpy(mHashes, node.mHashes, sizeof(mHashes));
}

SHAMapTreeNode::SHAMapTreeNode(const SHAMapNode& node, SHAMapItem::pointer item, TNType type, uint32 seq) :
	SHAMapNode(node), mItem(item), mSeq(seq), mType(type), mFullBelow(true)
{
	assert(item->peekData().size()>=32);
	updateHash();
}

SHAMapTreeNode::SHAMapTreeNode(const SHAMapNode& id, const std::vector<unsigned char>& rawNode, uint32 seq)
	: SHAMapNode(id), mSeq(seq), mType(ERROR), mFullBelow(false)
{
	Serializer s(rawNode);

	int type=s.removeLastByte();
	int len=s.getLength();
	if( (type<0) || (type>3) || (len<33) ) throw SHAMapException(InvalidNode);

	if(type==0)
	{ // transaction
		mItem=boost::make_shared<SHAMapItem>(s.getSHA512Half(), s.peekData());
		mType=TRANSACTION;
	}
	else if(type==1)
	{ // account state
		uint160 u;
		s.get160(u, len-20);
		s.chop(20);
		if(!u) throw SHAMapException(InvalidNode);
		mItem=boost::make_shared<SHAMapItem>(u, s.peekData());
		mType=ACCOUNT_STATE;
	}
	else if(type==2)
	{ // full inner
		if(len!=512) throw SHAMapException(InvalidNode);
		for(int i=0; i<16; i++)
			s.get256(mHashes[i], i*32);
		mType=INNER;
	}
	else if(type==3)
	{ // compressed inner
		for(int i=0; i<(len/33); i++)
		{
			int pos;
			s.get1(pos, 32+(i*33));
			if( (pos<0) || (pos>=16)) throw SHAMapException(InvalidNode);
			s.get256(mHashes[pos], i*33);
		}
		mType=INNER;
	}

	updateHash();
}

void SHAMapTreeNode::addRaw(Serializer &s)
{
	if(mType==ERROR) throw SHAMapException(InvalidNode);

	if(mType==TRANSACTION)
	{
		mItem->addRaw(s);
		s.add1(0);
		assert(s.getLength()>32);
		return;
	}

	if(mType==ACCOUNT_STATE)
	{
		mItem->addRaw(s);
		assert(s.getLength()>20);
		s.add160(mItem->getTag().to160());
		s.add1(1);
		return;
	}

	if(getBranchCount()<5)
	{ // compressed node
		for(int i=0; i<16; i++)
			if(!!mHashes[i])
			{
				s.add256(mHashes[i]);
				s.add1(i);
			}
		s.add1(3);
		return;
	}

	for(int i=0; i<16; i++)
		s.add256(mHashes[i]);
	s.add1(2);
}

bool SHAMapTreeNode::updateHash()
{
	uint256 nh;

	if(mType==INNER)
	{
		bool empty=true;
		for(int i=0; i<16; i++)
			if(!!mHashes[i])
			{
				empty=false;
				break;
			}
		if(!empty)
			nh=Serializer::getSHA512Half(reinterpret_cast<unsigned char *>(mHashes), sizeof(mHashes));
	}
	else if(mType==ACCOUNT_STATE)
	{
		Serializer s;
		mItem->addRaw(s);
		s.add160(mItem->getTag().to160());
		nh=s.getSHA512Half();
	}
	else if(mType==TRANSACTION)
		nh=mItem->getTag();
	else assert(false);

	if(nh==mHash) return false;
	mHash=nh;
	return true;
}

bool SHAMapTreeNode::setItem(SHAMapItem::pointer& i, TNType type)
{
	uint256 hash=getNodeHash();
	mType=type;
	mItem=i;
	return getNodeHash()==hash;
}

SHAMapItem::pointer SHAMapTreeNode::getItem() const
{
	return boost::make_shared<SHAMapItem>(*mItem);
}

int SHAMapTreeNode::getBranchCount() const
{
	int ret=0;
	for(int i=0; i<16; i++)
		if(!mHashes[i]) ret++;
	return ret;
}

void SHAMapTreeNode::makeInner()
{
	mItem=SHAMapItem::pointer();
	memset(mHashes, 0, sizeof(mHashes));
	mType=INNER;
	mHash.zero();
}

void SHAMapTreeNode::dump()
{
	std::cerr << "SHAMapTreeNode(" << getNodeID().GetHex() << ")" << std::endl;
}

std::string SHAMapTreeNode::getString() const
{
	std::string ret="NodeID(";
	ret+=boost::lexical_cast<std::string>(getDepth());
	ret+=",";
	ret+=getNodeID().GetHex();
	ret+=")";
	if(isInner())
	{
		for(int i=0; i<16; i++)
			if(!isEmptyBranch(i))
			{
				ret+=",b";
				ret+=boost::lexical_cast<std::string>(i);
			}
	}
	if(isLeaf())
	{
		ret+=",leaf";
	}
	return ret;
}

bool SHAMapTreeNode::setChildHash(int m, const uint256 &hash)
{
	assert( (m>=0) && (m<16) );
	assert(mType==INNER);
	if(mHashes[m]==hash)
		return false;
	mHashes[m]=hash;
	return updateHash();
}

const uint256& SHAMapTreeNode::getChildHash(int m) const
{
	assert( (m>=0) && (m<16) && (mType==INNER) );
	return mHashes[m];
}
