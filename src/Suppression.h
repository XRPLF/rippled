#ifndef __SUPPRESSION__
#define __SUPPRESSION__

#include <list>

#include <boost/unordered_map.hpp>
#include <boost/thread/mutex.hpp>

#include "uint256.h"

extern std::size_t hash_value(const uint160& u);

class SuppressionTable
{
protected:

	boost::mutex mSuppressionMutex;

	// Stores all suppressed hashes and their expiration time
	boost::unordered_map<uint160, time_t> mSuppressionMap;

	// Stores all expiration times and the hashes indexed for them
	boost::unordered_map< time_t, std::list<uint160> > mSuppressionTimes;

	int mHoldTime;

public:
	SuppressionTable(int holdTime = 120) : mHoldTime(holdTime) { ; }

	bool addSuppression(const uint256& suppression);
	bool addSuppression(const uint160& suppression);
};

#endif
