#include "LoadMonitor.h"
#include "Log.h"

SETUP_LOG();

void LoadMonitor::update()
{ // call with the mutex
	int now = upTime();

	if (now == mLastUpdate) // current
		return;

	if ((now < mLastUpdate) || (now > (mLastUpdate + 8)))
	{ // way out of date
		mCounts = 0;
		mLatencyEvents = 0;
		mLatencyMSAvg = 0;
		mLatencyMSPeak = 0;
		mLastUpdate = now;
		return;
	}

	do
	{ // do exponential decay
		++mLastUpdate;
		mCounts -= ((mCounts + 3) / 4);
		mLatencyEvents -= ((mLatencyEvents + 3) / 4);
		mLatencyMSAvg -= (mLatencyMSAvg / 4);
		mLatencyMSPeak -= (mLatencyMSPeak / 4);
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
	if (latency == 1)
		latency = 0;
	boost::mutex::scoped_lock sl(mLock);

	update();

	++mLatencyEvents;
	mLatencyMSAvg += latency;
	mLatencyMSPeak += latency;

	int lp = mLatencyEvents * latency * 4;
	if (mLatencyMSPeak < lp)
		mLatencyMSPeak = lp;
}

void LoadMonitor::addCountAndLatency(const std::string& name, int counts, int latency)
{
	if (latency > 500)
	{
		cLog((latency > 1000) ? lsWARNING : lsINFO) << "Job: " << name << " ExecutionTime: " << latency;
	}
	if (latency == 1)
		latency = 0;
	boost::mutex::scoped_lock sl(mLock);

	update();
	mCounts += counts;
	++mLatencyEvents;
	mLatencyMSAvg += latency;
	mLatencyMSPeak += latency;

	int lp = mLatencyEvents * latency * 4;
	if (mLatencyMSPeak < lp)
		mLatencyMSPeak = lp;
}

bool LoadMonitor::isOver()
{
	boost::mutex::scoped_lock sl(mLock);

	update();

	if (mLatencyEvents == 0)
		return 0;

	return isOverTarget(mLatencyMSAvg / (mLatencyEvents * 4), mLatencyMSPeak / (mLatencyEvents * 4));
}

void LoadMonitor::getCountAndLatency(uint64& count, uint64& latencyAvg, uint64& latencyPeak, bool& isOver)
{
	boost::mutex::scoped_lock sl(mLock);

	update();

	count = mCounts / 4;

	if (mLatencyEvents == 0)
	{
		latencyAvg = 0;
		latencyPeak = 0;
	}
	else
	{
		latencyAvg = mLatencyMSAvg / (mLatencyEvents * 4);
		latencyPeak = mLatencyMSPeak / (mLatencyEvents * 4);
	}
	isOver = isOverTarget(latencyAvg, latencyPeak);
}

// vim:ts=4
