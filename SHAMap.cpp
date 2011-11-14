#include "BitcoinUtil.h"
#include "SHAMap.h"

bool SHAMapNodeID::operator<(const SHAMapNodeID &s) const
{
 if(s.mDepth<mDepth) return true;
 if(s.mDepth>mDepth) return false;
 return mNodeID<s.mNodeID;
}

bool SHAMapNodeID::operator>(const SHAMapNodeID &s) const
{
 if(s.mDepth<mDepth) return false;
 if(s.mDepth>mDepth) return true;
 return mNodeID>s.mNodeID;
}

bool SHAMapNodeID::operator<=(const SHAMapNodeID &s) const
{
 if(s.mDepth<mDepth) return true;
 if(s.mDepth>mDepth) return false;
 return mNodeID<=s.mNodeID;
}

bool SHAMapNodeID::operator>=(const SHAMapNodeID &s) const
{
 if(s.mDepth<mDepth) return false;
 if(s.mDepth>mDepth) return true;
 return mNodeID>=s.mNodeID;
}

bool SHAMapNodeID::operator==(const SHAMapNodeID &s) const
{
 return (s.mDepth==mDepth) && (s.mNodeID==mNodeID);
}

bool SHAMapNodeID::operator!=(const SHAMapNodeID &s) const
{
 return (s.mDepth!=mDepth) || (s.mNodeID!=mNodeID);
}

void SHAMapNodeID::ClassInit()
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

SHAMapNodeID::SHAMapNodeID(int depth, const uint256 &hash)
{
    assert(depth>=0 && depth<leafDepth);
    mDepth=depth;
    mNodeID=getNodeID(depth, hash);
}

SHAMapNodeID SHAMapNodeID::getChildNodeID(int m)
{
    assert(!isLeaf());

    uint256 branch=m;
    branch>>=(mDepth*8);

    return SHAMapNodeID(mDepth+1, mNodeID | branch);
}

int SHAMapNodeID::selectBranch(const uint256 &hash)
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

