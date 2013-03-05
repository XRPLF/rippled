#ifndef JOB_QUEUE__H
#define JOB_QUEUE__H

#include <map>
#include <set>
#include <vector>

#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/function.hpp>
#include <boost/make_shared.hpp>

#include "../json/value.h"

#include "types.h"
#include "LoadMonitor.h"

// Note that this queue should only be used for CPU-bound jobs
// It is primarily intended for signature checking

enum JobType
{ // must be in priority order, low to high
	jtINVALID		= -1,
	jtPUBOLDLEDGER	= 1,	// An old ledger has been accepted
	jtVALIDATION_ut	= 2,	// A validation from an untrusted source
	jtPROOFWORK		= 3,	// A proof of work demand from another server
	jtPROPOSAL_ut	= 4,	// A proposal from an untrusted source
	jtCLIENT		= 5,	// A websocket command from the client
	jtTRANSACTION	= 6,	// A transaction received from the network
	jtPUBLEDGER		= 7,	// Publish a fully-accepted ledger
	jtWAL			= 8,	// Write-ahead logging
	jtVALIDATION_t	= 9,	// A validation from a trusted source
	jtWRITE			= 10,	// Write out hashed objects
	jtTRANSACTION_l	= 11,	// A local transaction
	jtPROPOSAL_t	= 12,	// A proposal from a trusted source
	jtADMIN			= 13,	// An administrative operation
	jtDEATH			= 14,	// job of death, used internally

// special types not dispatched by the job pool
	jtPEER			= 24,
	jtDISK			= 25,
	jtRPC			= 26,
	jtACCEPTLEDGER	= 27,
	jtTXN_PROC		= 28,
	jtOB_SETUP		= 29,
	jtPATH_FIND		= 30
}; // CAUTION: If you add new types, add them to JobType.cpp too
#define NUM_JOB_TYPES 32

class Job
{
protected:
	JobType						mType;
	uint64						mJobIndex;
	boost::function<void(Job&)>	mJob;
	LoadEvent::pointer			mLoadMonitor;
	std::string					mName;

public:

	Job()							: mType(jtINVALID), mJobIndex(0)	{ ; }

	Job(JobType type, uint64 index)	: mType(type), mJobIndex(index)
	{ ; }

	Job(JobType type, const std::string& name, uint64 index, LoadMonitor& lm, const boost::function<void(Job&)>& job)
		: mType(type), mJobIndex(index), mJob(job), mName(name)
	{
		mLoadMonitor = boost::make_shared<LoadEvent>(boost::ref(lm), name, false, 1);
	}

	JobType getType() const				{ return mType; }
	void doJob(void)					{ mLoadMonitor->start(); mJob(*this); mLoadMonitor->reName(mName); }
	void rename(const std::string& n)	{ mName = n; }

	bool operator<(const Job& j) const;
	bool operator>(const Job& j) const;
	bool operator<=(const Job& j) const;
	bool operator>=(const Job& j) const;

	static const char* toString(JobType);
};

class JobQueue
{
protected:
	boost::mutex					mJobLock;
	boost::condition_variable		mJobCond;

	uint64							mLastJob;
	std::set<Job>					mJobSet;
	std::map<JobType, int>			mJobCounts;
	LoadMonitor						mJobLoads[NUM_JOB_TYPES];
	int								mThreadCount;
	bool							mShuttingDown;


	void threadEntry(void);

public:

	JobQueue();

	void addJob(JobType type, const std::string& name, const boost::function<void(Job&)>& job);

	int getJobCount(JobType t);		// Jobs at this priority
	int getJobCountGE(JobType t);	// All jobs at or greater than this priority
	std::vector< std::pair<JobType, int> > getJobCounts();

	void shutdown();
	void setThreadCount(int c = 0);

	LoadEvent::pointer getLoadEvent(JobType t, const std::string& name)
	{ return boost::make_shared<LoadEvent>(boost::ref(mJobLoads[t]), name, true, 1); }
	LoadEvent::autoptr getLoadEventAP(JobType t, const std::string& name)
	{ return LoadEvent::autoptr(new LoadEvent(mJobLoads[t], name, true, 1)); }

	int isOverloaded();
	Json::Value getJson(int c = 0);
};

#endif
