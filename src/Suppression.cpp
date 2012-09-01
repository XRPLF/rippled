#include "Suppression.h"

#include <boost/foreach.hpp>

bool SuppressionTable::addSuppression(const uint160& suppression)
{
	boost::mutex::scoped_lock sl(mSuppressionMutex);

	if (mSuppressionMap.find(suppression) != mSuppressionMap.end())
		return false;

	time_t now = time(NULL);
	time_t expireTime = now - mHoldTime;

	boost::unordered_map< time_t, std::list<uint160> >::iterator
		it = mSuppressionTimes.begin(), end = mSuppressionTimes.end();
	while (it != end)
	{
		if (it->first <= expireTime)
		{
			BOOST_FOREACH(const uint160& lit, it->second)
				mSuppressionMap.erase(lit);
			it = mSuppressionTimes.erase(it);
		}
		else ++it;
	}

	mSuppressionMap[suppression] = now;
	mSuppressionTimes[now].push_back(suppression);

	return true;
}

bool SuppressionTable::addSuppression(const uint256& suppression)
{
	uint160 u;
	memcpy(u.begin(), suppression.begin() + (suppression.size() - u.size()), u.size());
	return addSuppression(u);
}
