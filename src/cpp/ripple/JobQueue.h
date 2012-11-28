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
	jtVALIDATION_ut	= 0,	// A validation from an untrusted source
	jtCLIENTOP_ut	= 1,	// A client operation from a non-local/untrusted source
	jtPROOFWORK		= 2,	// A proof of work demand from another server
	jtTRANSACTION	= 3,	// A transaction received from the network
	jtPROPOSAL_ut	= 4,	// A proposal from an untrusted source
	jtCLIENTOP_t	= 5,	// A client operation from a trusted source
	jtVALIDATION_t	= 6,	// A validation from a trusted source
	jtTRANSACTION_l	= 7,	// A local transaction
	jtPROPOSAL_t	= 8,	// A proposal from a trusted source
	jtADMIN			= 9,	// An administrative operation
	jtDEATH			= 10,	// job of death, used internally

// special types not dispatched by the job pool
	jtCLIENT		= 16,
	jtPEER			= 17,
	jtDISK			= 18,
	jtRPC			= 19,
	jtACCEPTLEDGER	= 20,
	jtPUBLEDGER		= 21,
};
#define NUM_JOB_TYPES 24

class Job
{
protected:
	JobType						mType;
	uint64						mJobIndex;
	boost::function<void(Job&)>	mJob;
	LoadEvent::pointer			mLoadMonitor;

public:

	Job()							: mType(jtINVALID), mJobIndex(0)	{ ; }

	Job(JobType type, uint64 index)	: mType(type), mJobIndex(index)
	{ ; }

	Job(JobType type, uint64 index, LoadMonitor& lm, const boost::function<void(Job&)>& job)
		: mType(type), mJobIndex(index), mJob(job)
	{ mLoadMonitor = boost::make_shared<LoadEvent>(boost::ref(lm), true, 1); }

	JobType getType() const				{ return mType; }
	void doJob(void)					{ mJob(*this); }

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

	JobQueue() : mLastJob(0), mThreadCount(0), mShuttingDown(false) { ; }

	void addJob(JobType type, const boost::function<void(Job&)>& job);

	int getJobCount(JobType t);		// Jobs at this priority
	int getJobCountGE(JobType t);	// All jobs at or greater than this priority
	std::vector< std::pair<JobType, int> > getJobCounts();

	void shutdown();
	void setThreadCount(int c = 0);

	LoadEvent::pointer getLoadEvent(JobType t)
	{ return boost::make_shared<LoadEvent>(boost::ref(mJobLoads[t]), true, 1); }
	LoadEvent::autoptr getLoadEventAP(JobType t)
	{ return LoadEvent::autoptr(new LoadEvent(mJobLoads[t], true, 1)); }

	Json::Value getJson(int c = 0);
};

#endif
