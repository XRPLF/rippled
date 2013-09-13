//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_LOADMONITOR_RIPPLEHEADER
#define RIPPLE_LOADMONITOR_RIPPLEHEADER

// Monitors load levels and response times

// VFALCO TODO Rename this. Having both LoadManager and LoadMonitor is confusing.
//
class LoadMonitor
{
public:
    LoadMonitor ();

    void addCount ();

    void addLatency (int latency);

    void addCountAndLatency (const std::string& name, int latency);

    void setTargetLatency (uint64 avg, uint64 pk);

    bool isOverTarget (uint64 avg, uint64 peak);

    // VFALCO TODO make this return the values in a struct.
    void getCountAndLatency (uint64& count, uint64& latencyAvg, uint64& latencyPeak, bool& isOver);

    bool isOver ();

private:
    void update ();

    typedef RippleMutex LockType;
    typedef LockType::ScopedLockType ScopedLockType;
    LockType mLock;

    uint64              mCounts;
    int mLatencyEvents;
    uint64              mLatencyMSAvg;
    uint64              mLatencyMSPeak;
    uint64              mTargetLatencyAvg;
    uint64              mTargetLatencyPk;
    int                 mLastUpdate;
};

#endif
