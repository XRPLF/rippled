//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (LoadMonitor)

LoadMonitor::LoadMonitor ()
    : mCounts (0)
    , mLatencyEvents (0)
    , mLatencyMSAvg (0)
    , mLatencyMSPeak (0)
    , mTargetLatencyAvg (0)
    , mTargetLatencyPk (0)
    , mLastUpdate (UptimeTimer::getInstance ().getElapsedSeconds ())
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
    int now = UptimeTimer::getInstance ().getElapsedSeconds ();

    // VFALCO TODO stop returning from the middle of functions.

    if (now == mLastUpdate) // current
        return;

    // VFALCO TODO Why 8?
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
    /*
        David:
        
        "Imagine if you add 10 to something every second. And you
         also reduce it by 1/4 every second. It will "idle" at 40,
         correponding to 10 counts per second."
    */
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
    boost::mutex::scoped_lock sl (mLock);

    update ();
    ++mCounts;
}

void LoadMonitor::addLatency (int latency)
{
    // VFALCO NOTE Why does 1 become 0?
    if (latency == 1)
        latency = 0;

    boost::mutex::scoped_lock sl (mLock);

    update ();

    ++mLatencyEvents;
    mLatencyMSAvg += latency;
    mLatencyMSPeak += latency;

    // VFALCO NOTE Why are we multiplying by 4?
    int const latencyPeak = mLatencyEvents * latency * 4;

    if (mLatencyMSPeak < latencyPeak)
        mLatencyMSPeak = latencyPeak;
}


void LoadMonitor::addCountAndLatency (const std::string& name, int latency)
{
    if (latency > 500)
    {
        WriteLog ((latency > 1000) ? lsWARNING : lsINFO, LoadMonitor) << "Job: " << name << " ExecutionTime: " << latency;
    }

    // VFALCO NOTE Why does 1 become 0?
    if (latency == 1)
        latency = 0;

    boost::mutex::scoped_lock sl (mLock);

    update ();
    ++mCounts;
    ++mLatencyEvents;
    mLatencyMSAvg += latency;
    mLatencyMSPeak += latency;

    // VFALCO NOTE Why are we multiplying by 4?
    int const latencyPeak = mLatencyEvents * latency * 4;

    if (mLatencyMSPeak < latencyPeak)
        mLatencyMSPeak = latencyPeak;
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
    boost::mutex::scoped_lock sl (mLock);

    update ();

    if (mLatencyEvents == 0)
        return 0;

    return isOverTarget (mLatencyMSAvg / (mLatencyEvents * 4), mLatencyMSPeak / (mLatencyEvents * 4));
}

void LoadMonitor::getCountAndLatency (uint64& count, uint64& latencyAvg, uint64& latencyPeak, bool& isOver)
{
    boost::mutex::scoped_lock sl (mLock);

    update ();

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

    isOver = isOverTarget (latencyAvg, latencyPeak);
}

// vim:ts=4
