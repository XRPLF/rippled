#ifndef LOADMONITOR__H_
#define LOADMONITOR__H_

#include <string>

#include <boost/thread/mutex.hpp>
#include <boost/shared_ptr.hpp>

#include "types.h"

// Monitors load levels and response times

class LoadMonitor
{
protected:
	uint64				mCounts;
	uint64				mLatencyEvents;
	uint64				mLatencyMSAvg;
	uint64				mLatencyMSPeak;
	uint64				mTargetLatencyAvg;
	uint64				mTargetLatencyPk;
	time_t				mLastUpdate;
	boost::mutex		mLock;

	void update();

public:
	LoadMonitor() : mCounts(0), mLatencyEvents(0), mLatencyMSAvg(0), mLatencyMSPeak(0),
		mTargetLatencyAvg(0), mTargetLatencyPk(0)
	{ mLastUpdate = time(NULL); }

	void addCount(int counts);
	void addLatency(int latency);
	void addCountAndLatency(int counts, int latency);

	void setTargetLatency(uint64 avg, uint64 pk)
	{
		mTargetLatencyAvg = avg * 4;
		mTargetLatencyPk = pk * 4;
	}

	bool isOverTarget()
	{
		return (mTargetLatencyPk && (mLatencyMSPeak > mTargetLatencyPk)) ||
			(mTargetLatencyAvg && (mLatencyMSAvg > mTargetLatencyAvg));
	}

	void getCountAndLatency(uint64& count, uint64& latencyAvg, uint64& latencyPeak, bool& isOver);
};

class LoadEvent
{
public:
	typedef boost::shared_ptr<LoadEvent>	pointer;
	typedef std::auto_ptr<LoadEvent>		autoptr;

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
			static_cast<int>((boost::posix_time::microsec_clock::universal_time() - mStartTime).total_milliseconds()));
	}
};

#endif
