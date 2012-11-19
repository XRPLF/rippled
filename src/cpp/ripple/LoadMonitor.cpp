#include "LoadMonitor.h"

void LoadMonitor::LoadMonitor::update()
{ // call with the mutex
	time_t now = time(NULL);

	if (now == mLastUpdate) // current
		return;

	if ((now < mLastUpdate) || (now > (mLastUpdate + 8)))
	{ // way out of date
		mCounts = 0;
		mLatencyEvents = 0;
		mLatencyMS = 0;
		mLastUpdate = now;
		return;
	}

	do
	{ // do exponential decay
		++mLastUpdate;
		mCounts -= (mCounts / 4);
		mLatencyEvents -= (mLatencyEvents / 4);
		mLatencyMS -= (mLatencyMS / 4);
	} while (mLastUpdate < now);
}

void LoadMonitor::addCount(int counts)
{
	boost::mutex::scoped_lock sl(mLock);

	update();
	mCounts += counts;
}

void LoadMonitor::addLatency(int latency)
{
	boost::mutex::scoped_lock sl(mLock);

	update();
	++mLatencyEvents;
	mLatencyMS += latency;
}

void LoadMonitor::addCountAndLatency(int counts, int latency)
{
	boost::mutex::scoped_lock sl(mLock);

	update();
	mCounts += counts;
	++mLatencyEvents;
	mLatencyMS += latency;
}

void LoadMonitor::getCountAndLatency(uint64& count, uint64& latency)
{
	boost::mutex::scoped_lock sl(mLock);

	update();

	count = mCounts / 4;

	if (mLatencyEvents == 0)
		latency = 0;
	else latency = mLatencyMS / (mLatencyEvents * 4);
}
