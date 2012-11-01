
#include "Suppression.h"

#include <boost/foreach.hpp>

DECLARE_INSTANCE(Suppression);

Suppression& SuppressionTable::findCreateEntry(const uint256& index, bool& created)
{
	boost::unordered_map<uint256, Suppression>::iterator fit = mSuppressionMap.find(index);

	if (fit != mSuppressionMap.end())
	{
		created = false;
		return fit->second;
	}
	created = true;

	time_t now = time(NULL);
	time_t expireTime = now - mHoldTime;

	// See if any supressions need to be expired
	std::map< time_t, std::list<uint256> >::iterator it = mSuppressionTimes.begin();
	if ((it != mSuppressionTimes.end()) && (it->first <= expireTime))
	{
		BOOST_FOREACH(const uint256& lit, it->second)
			mSuppressionMap.erase(lit);
		mSuppressionTimes.erase(it);
	}

	mSuppressionTimes[now].push_back(index);
	return mSuppressionMap.insert(std::make_pair(index, Suppression())).first->second;
}

bool SuppressionTable::addSuppression(const uint256& index)
{
	boost::mutex::scoped_lock sl(mSuppressionMutex);

	bool created;
	findCreateEntry(index, created);
	return created;
}

Suppression SuppressionTable::getEntry(const uint256& index)
{
	boost::mutex::scoped_lock sl(mSuppressionMutex);

	bool created;
	return findCreateEntry(index, created);
}

bool SuppressionTable::addSuppressionPeer(const uint256& index, uint64 peer)
{
	boost::mutex::scoped_lock sl(mSuppressionMutex);

	bool created;
	findCreateEntry(index, created).addPeer(peer);
	return created;
}

bool SuppressionTable::addSuppressionFlags(const uint256& index, int flag)
{
	boost::mutex::scoped_lock sl(mSuppressionMutex);

	bool created;
	findCreateEntry(index, created).setFlag(flag);
	return created;
}

bool SuppressionTable::setFlag(const uint256& index, int flag)
{ // return: true = changed, false = unchanged
	assert(flag != 0);

	boost::mutex::scoped_lock sl(mSuppressionMutex);

	bool created;
	Suppression &s = findCreateEntry(index, created);

	if ((s.getFlags() & flag) == flag)
		return false;

	s.setFlag(flag);
	return true;
}

bool SuppressionTable::swapSet(const uint256& index, std::set<uint64>& peers, int flag)
{
	boost::mutex::scoped_lock sl(mSuppressionMutex);

	bool created;
	Suppression &s = findCreateEntry(index, created);

	if ((s.getFlags() & flag) == flag)
		return false;

	s.swapSet(peers);
	s.setFlag(flag);

	return true;
}