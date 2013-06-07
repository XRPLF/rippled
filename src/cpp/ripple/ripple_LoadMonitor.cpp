SETUP_LOG (LoadMonitor)

LoadMonitor::LoadMonitor ()
	: mCounts (0)
	, mLatencyEvents (0)
	, mLatencyMSAvg (0)
	, mLatencyMSPeak (0)
	, mTargetLatencyAvg (0)
	, mTargetLatencyPk (0)
    , mLastUpdate (UptimeTimer::getInstance().getElapsedSeconds ())
{
}

// VFALCO NOTE WHY do we need "the mutex?" This dependence on
//         a hidden global, especially a synchronization primitive,
//         is a flawed design.
//         It's not clear exactly which data needs to be protected.
//
// call with the mutex
void LoadMonitor::update ()
{
	int now = UptimeTimer::getInstance().getElapsedSeconds ();

    // VFALCO TODO stop returning from the middle of functions.

	if (now == mLastUpdate) // current
		return;

	if ((now < mLastUpdate) || (now > (mLastUpdate + 8)))
	{
        // way out of date
		mCounts = 0;
		mLatencyEvents = 0;
		mLatencyMSAvg = 0;
		mLatencyMSPeak = 0;
		mLastUpdate = now;
        // VFALCO TODO don't return from the middle...
		return;
	}

    // do exponential decay
	do
	{
		++mLastUpdate;
		mCounts -= ((mCounts + 3) / 4);
		mLatencyEvents -= ((mLatencyEvents + 3) / 4);
		mLatencyMSAvg -= (mLatencyMSAvg / 4);
		mLatencyMSPeak -= (mLatencyMSPeak / 4);
	}
    while (mLastUpdate < now);
}

void LoadMonitor::addCount ()
{
	boost::mutex::scoped_lock sl(mLock);

	update();
	++mCounts;
}

void LoadMonitor::addLatency (int latency)
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

void LoadMonitor::addCountAndLatency (const std::string& name, int latency)
{
	if (latency > 500)
	{
		WriteLog ((latency > 1000) ? lsWARNING : lsINFO, LoadMonitor) << "Job: " << name << " ExecutionTime: " << latency;
	}
	if (latency == 1)
		latency = 0;

	boost::mutex::scoped_lock sl(mLock);

	update();
	++mCounts;
	++mLatencyEvents;
	mLatencyMSAvg += latency;
	mLatencyMSPeak += latency;

	int lp = mLatencyEvents * latency * 4;
	if (mLatencyMSPeak < lp)
		mLatencyMSPeak = lp;
}

void LoadMonitor::setTargetLatency (uint64 avg, uint64 pk)
{
	mTargetLatencyAvg  = avg;
	mTargetLatencyPk = pk;
}

bool LoadMonitor::isOverTarget (uint64 avg, uint64 peak)
{
	return (mTargetLatencyPk && (peak > mTargetLatencyPk)) ||
		(mTargetLatencyAvg && (avg > mTargetLatencyAvg));
}

bool LoadMonitor::isOver ()
{
	boost::mutex::scoped_lock sl(mLock);

	update();

	if (mLatencyEvents == 0)
		return 0;

	return isOverTarget(mLatencyMSAvg / (mLatencyEvents * 4), mLatencyMSPeak / (mLatencyEvents * 4));
}

void LoadMonitor::getCountAndLatency (uint64& count, uint64& latencyAvg, uint64& latencyPeak, bool& isOver)
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
