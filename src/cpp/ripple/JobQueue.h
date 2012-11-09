#ifndef JOB_QUEUE__H
#define JOB_QUEUE__H

#include <map>
#include <set>
#include <vector>

#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/function.hpp>

#include "types.h"

// Note that this queue should only be used for CPU-bound jobs
// It is primarily intended for signature checking

enum JobType
{ // must be in priority order, low to high
	jtINVALID,
	jtVALIDATION_ut,
	jtTRANSACTION,
	jtPROPOSAL_ut,
	jtVALIDATION_t,
	jtTRANSACTION_l,
	jtPROPOSAL_t,
	jtADMIN,
	jtDEATH,			// job of death, used internally
};

class Job
{
protected:
	JobType						mType;
	uint64						mJobIndex;
	boost::function<void(Job&)>	mJob;

public:
	Job()							: mType(jtINVALID), mJobIndex(0)	{ ; }
	Job(JobType type, uint64 index)	: mType(type), mJobIndex(index)		{ ; }

	Job(JobType type, uint64 index, const boost::function<void(Job&)>& job)
		: mType(type), mJobIndex(index), mJob(job) { ; }

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
	boost::mutex				mJobLock;
	boost::condition_variable	mJobCond;

	uint64						mLastJob;
	std::set<Job>				mJobSet;
	std::map<JobType, int>		mJobCounts;
	int							mThreadCount;
	bool						mShuttingDown;


	void threadEntry(void);

public:

	JobQueue() : mLastJob(0), mThreadCount(0), mShuttingDown(false) { ; }

	void addJob(JobType type, const boost::function<void(Job&)>& job);

	int getJobCount(JobType t);		// Jobs at this priority
	int getJobCountGE(JobType t);	// All jobs at or greater than this priority
	std::vector< std::pair<JobType, int> > getJobCounts();

	void shutdown();
	void setThreadCount(int c = 0);
};

#endif
