#ifndef RIPPLE_LOADMONITOR_H
#define RIPPLE_LOADMONITOR_H

// Monitors load levels and response times

class LoadMonitor
{
public:
	LoadMonitor ();

	void addCount ();
	
    void addLatency (int latency);

    void addCountAndLatency (const std::string& name, int latency);

	void setTargetLatency (uint64 avg, uint64 pk);

	bool isOverTarget(uint64 avg, uint64 peak);

    // VFALCO TODO make this return the values in a struct.
	void getCountAndLatency (uint64& count, uint64& latencyAvg, uint64& latencyPeak, bool& isOver);

    bool isOver ();

private:
	void update ();

	boost::mutex		mLock;
    uint64				mCounts;
	uint64				mLatencyEvents;
	uint64				mLatencyMSAvg;
	uint64				mLatencyMSPeak;
	uint64				mTargetLatencyAvg;
	uint64				mTargetLatencyPk;
	int					mLastUpdate;
};

#endif
