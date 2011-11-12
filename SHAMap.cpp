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

