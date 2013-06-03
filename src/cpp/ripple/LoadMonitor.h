#ifndef LOADMONITOR__H_
#define LOADMONITOR__H_

#include <string>

#include <boost/thread/mutex.hpp>
#include <boost/shared_ptr.hpp>


// Monitors load levels and response times

class LoadMonitor
{
public:
	LoadMonitor()
		: mCounts(0)
		, mLatencyEvents(0)
		, mLatencyMSAvg(0)
		, mLatencyMSPeak(0)
		, mTargetLatencyAvg(0)
		, mTargetLatencyPk(0)
	{
		mLastUpdate = UptimeTimer::getInstance().getElapsedSeconds ();
	}

	void addCount();
	void addLatency(int latency);
	void addCountAndLatency(const std::string& name, int latency);

	void setTargetLatency(uint64 avg, uint64 pk)
	{
		mTargetLatencyAvg  = avg;
		mTargetLatencyPk = pk;
	}

	bool isOverTarget(uint64 avg, uint64 peak)
	{
		return (mTargetLatencyPk && (peak > mTargetLatencyPk)) ||
			(mTargetLatencyAvg && (avg > mTargetLatencyAvg));
	}

	void getCountAndLatency(uint64& count, uint64& latencyAvg, uint64& latencyPeak, bool& isOver);
	bool isOver();

private:
	void update();

    uint64				mCounts;
	uint64				mLatencyEvents;
	uint64				mLatencyMSAvg;
	uint64				mLatencyMSPeak;
	uint64				mTargetLatencyAvg;
	uint64				mTargetLatencyPk;
	int					mLastUpdate;
	boost::mutex		mLock;
};

class LoadEvent
{
public:
	typedef boost::shared_ptr<LoadEvent>	pointer;
	typedef UPTR_T<LoadEvent>				autoptr;

public:
	LoadEvent(LoadMonitor& monitor, const std::string& name, bool shouldStart) :
		mMonitor(monitor), mRunning(false), mName(name)
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

	void reName(const std::string& name)
	{
		mName = name;
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
		mMonitor.addCountAndLatency(mName,
			static_cast<int>((boost::posix_time::microsec_clock::universal_time() - mStartTime).total_milliseconds()));
	}

private:
	LoadMonitor&				mMonitor;
	bool						mRunning;
	std::string					mName;
	boost::posix_time::ptime	mStartTime;
};

#endif
