#ifndef LOADMONITOR__H_
#define LOADMONITOR__H_

#include <string>

#include <boost/thread/mutex.hpp>

#include "types.h"

// Monitors load levels and response times

class LoadMonitor
{
protected:
	std::string			mName;
	uint64				mCounts;
	uint64				mLatencyEvents;
	uint64				mLatencyMS;
	time_t				mLastUpdate;
	boost::mutex		mLock;

	void update();

public:
	LoadMonitor(const std::string& n) : mName(n), mCounts(0), mLatencyEvents(0), mLatencyMS(0)
	{ mLastUpdate = time(NULL); }

	void setName(const std::string& n)		{ mName = n; }

	const std::string& getName() const		{ return mName; }

	void addCount(int counts);
	void addLatency(int latency);
	void addCountAndLatency(int counts, int latency);

	void getCountAndLatency(uint64& count, uint64& latency);
};

class LoadEvent
{
protected:
	LoadMonitor&				mMonitor;
	bool						mRunning;
	int							mCount;
	boost::posix_time::ptime	mStartTime;

public:
	LoadEvent(LoadMonitor& monitor, bool shouldStart, int count) : mMonitor(monitor), mRunning(false), mCount(count)
	{
		mStartTime = boost::posix_time::microsec_clock::universal_time();
		if (shouldStart)
			start();
	}

	~LoadEvent()
	{
		if (mRunning)
			stop();
	}

	void start()
	{ // okay to call if already started
		mRunning = true;
		mStartTime = boost::posix_time::microsec_clock::universal_time();
	}

	void stop()
	{
		assert(mRunning);
		mRunning = false;
		mMonitor.addCountAndLatency(mCount,
			(boost::posix_time::microsec_clock::universal_time() - mStartTime).total_milliseconds());
	}
};

#endif
