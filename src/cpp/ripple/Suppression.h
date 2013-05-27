#ifndef __SUPPRESSION__
#define __SUPPRESSION__

#include <set>
#include <map>
#include <list>

#include <boost/unordered_map.hpp>
#include <boost/thread/mutex.hpp>

#include "InstanceCounter.h"

DEFINE_INSTANCE(Suppression);

#define SF_RELAYED		0x01	// Has already been relayed to other nodes
#define SF_BAD			0x02	// Signature/format is bad
#define SF_SIGGOOD		0x04	// Signature is good
#define SF_SAVED		0x08
#define SF_RETRY		0x10	// Transaction can be retried
#define SF_TRUSTED		0x20	// comes from trusted source

class Suppression : private IS_INSTANCE(Suppression)
{
protected:
	int						mFlags;
	std::set<uint64>		mPeers;

public:
	Suppression()	: mFlags(0)					{ ; }

	const std::set<uint64>& peekPeers()			{ return mPeers; }
	void addPeer(uint64 peer)					{ if (peer != 0) mPeers.insert(peer); }
	bool hasPeer(uint64 peer)					{ return mPeers.count(peer) > 0; }

	int getFlags(void)							{ return mFlags; }
	bool hasFlag(int f)							{ return (mFlags & f) != 0; }
	void setFlag(int f)							{ mFlags |= f; }
	void clearFlag(int f)						{ mFlags &= ~f; }
	void swapSet(std::set<uint64>& s)			{ mPeers.swap(s); }
};

class SuppressionTable
{
protected:

	boost::mutex mSuppressionMutex;

	// Stores all suppressed hashes and their expiration time
	boost::unordered_map<uint256, Suppression> mSuppressionMap;

	// Stores all expiration times and the hashes indexed for them
	std::map< int, std::list<uint256> > mSuppressionTimes;

	int mHoldTime;

	Suppression& findCreateEntry(const uint256&, bool& created);

public:
	SuppressionTable(int holdTime = 120) : mHoldTime(holdTime) { ; }

	bool addSuppression(const uint256& index);

	bool addSuppressionPeer(const uint256& index, uint64 peer);
	bool addSuppressionPeer(const uint256& index, uint64 peer, int& flags);
	bool addSuppressionFlags(const uint256& index, int flag);
	bool setFlag(const uint256& index, int flag);
	int getFlags(const uint256& index);

	Suppression getEntry(const uint256&);

	bool swapSet(const uint256& index, std::set<uint64>& peers, int flag);
	bool swapSet(const uint256& index, std::set<uint64>& peers);
};

#endif
