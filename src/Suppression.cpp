
#include "Suppression.h"

bool SuppressionTable::addSuppression(const uint160& suppression)
{
	boost::mutex::scoped_lock sl(mSuppressionMutex);

	if (mSuppressionMap.find(suppression) != mSuppressionMap.end())
		return false;

	time_t now = time(NULL);

	boost::unordered_map< time_t, std::list<uint160> >::iterator it = mSuppressionTimes.begin();
	while (it != mSuppressionTimes.end())
	{
		if ((it->first + mHoldTime) < now)
		{
			for (std::list<uint160>::iterator lit = it->second.begin(), end = it->second.end(); 
					lit != end; ++lit)
				mSuppressionMap.erase(*lit);
			it=mSuppressionTimes.erase(it);
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
