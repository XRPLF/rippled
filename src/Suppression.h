#ifndef __SUPPRESSION__
#define __SUPPRESSION__

#include <set>
#include <map>
#include <list>

#include <boost/unordered_map.hpp>
#include <boost/thread/mutex.hpp>

#include "uint256.h"
#include "types.h"
#include "InstanceCounter.h"

DEFINE_INSTANCE(Suppression);

class Suppression : private IS_INSTANCE(Suppression)
{
protected:
	int						mFlags;
	std::set<uint64>		mPeers;

public:
	Suppression()	: mFlags(0)					{ ; }

	const std::set<uint64>& peekPeers()			{ return mPeers; }
	void addPeer(uint64 peer)					{ mPeers.insert(peer); }
	bool hasPeer(uint64 peer)					{ return mPeers.count(peer) > 0; }

	bool hasFlag(int f)							{ return (mFlags & f) != 0; }
	void setFlag(int f)							{ mFlags |= f; }
	void clearFlag(int f)						{ mFlags &= ~f; }
};

class SuppressionTable
{
protected:

	boost::mutex mSuppressionMutex;

	// Stores all suppressed hashes and their expiration time
	boost::unordered_map<uint256, Suppression> mSuppressionMap;

	// Stores all expiration times and the hashes indexed for them
	std::map< time_t, std::list<uint256> > mSuppressionTimes;

	int mHoldTime;

	Suppression& findCreateEntry(const uint256&, bool& created);

public:
	SuppressionTable(int holdTime = 120) : mHoldTime(holdTime) { ; }

	bool addSuppression(const uint256& index);

	bool addSuppressionPeer(const uint256& index, uint64 peer);
	bool addSuppressionFlags(const uint256& index, int flag);

	Suppression getEntry(const uint256&);
};

#endif
