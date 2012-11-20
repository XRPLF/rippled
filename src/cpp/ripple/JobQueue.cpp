#include "JobQueue.h"

#include <boost/foreach.hpp>
#include <boost/bind.hpp>
#include <boost/thread.hpp>

#include "Log.h"

SETUP_LOG();

const char* Job::toString(JobType t)
{
	switch(t)
	{
		case jtINVALID:			return "invalid";
		case jtVALIDATION_ut:	return "untrustedValidation";
		case jtTRANSACTION:		return "transaction";
		case jtPROPOSAL_ut:		return "untrustedProposal";
		case jtVALIDATION_t:	return "trustedValidation";
		case jtPROPOSAL_t:		return "trustedProposal";
		case jtADMIN:			return "administration";
		case jtDEATH:			return "jobOfDeath";
		case jtCLIENT:			return "clientCommand";
		case jtPEER:			return "peerCommand";
		case jtDISK:			return "diskAccess";
		default:				assert(false); return "unknown";
	}
}

bool Job::operator<(const Job& j) const
{ // These comparison operators make the jobs sort in priority order in the job set
	if (mType < j.mType)
		return true;
	if (mType > j.mType)
		return false;
	return mJobIndex < j.mJobIndex;
}

bool Job::operator<=(const Job& j) const
{
	if (mType < j.mType)
		return true;
	if (mType > j.mType)
		return false;
	return mJobIndex <= j.mJobIndex;
}

bool Job::operator>(const Job& j) const
{
	if (mType < j.mType)
		return false;
	if (mType > j.mType)
		return true;
	return mJobIndex > j.mJobIndex;
}

bool Job::operator>=(const Job& j) const
{
	if (mType < j.mType)
		return false;
	if (mType > j.mType)
		return true;
	return mJobIndex >= j.mJobIndex;
}

void JobQueue::addJob(JobType type, const boost::function<void(Job&)>& jobFunc)
{
	assert(type != jtINVALID);

	boost::mutex::scoped_lock sl(mJobLock);
	assert(mThreadCount != 0); // do not add jobs to a queue with no threads

	mJobSet.insert(Job(type, ++mLastJob, mJobLoads[type], jobFunc));
	++mJobCounts[type];
	mJobCond.notify_one();
}

int JobQueue::getJobCount(JobType t)
{
	boost::mutex::scoped_lock sl(mJobLock);

	std::map<JobType, int>::iterator c = mJobCounts.find(t);
	return (c == mJobCounts.end()) ? 0 : c->second;
}

int JobQueue::getJobCountGE(JobType t)
{ // return the number of jobs at this priority level or greater
	int ret = 0;

	boost::mutex::scoped_lock sl(mJobLock);

	typedef std::pair<JobType, int> jt_int_pair;
	BOOST_FOREACH(const jt_int_pair& it, mJobCounts)
		if (it.first >= t)
			ret += it.second;
	return ret;
}

std::vector< std::pair<JobType, int> > JobQueue::getJobCounts()
{ // return all jobs at all priority levels
	std::vector< std::pair<JobType, int> > ret;

	boost::mutex::scoped_lock sl(mJobLock);
	ret.reserve(mJobCounts.size());

	typedef std::pair<JobType, int> jt_int_pair;
	BOOST_FOREACH(const jt_int_pair& it, mJobCounts)
		ret.push_back(it);

	return ret;
}

Json::Value JobQueue::getJson(int)
{
	Json::Value ret(Json::objectValue);
	boost::mutex::scoped_lock sl(mJobLock);

	ret["threads"] = mThreadCount;

	Json::Value priorities = Json::arrayValue;
	for (int i = 0; i < NUM_JOB_TYPES; ++i)
	{
		uint64 count, latencyAvg, latencyPeak, jobCount;
		mJobLoads[i].getCountAndLatency(count, latencyAvg, latencyPeak);
		std::map<JobType, int>::iterator it = mJobCounts.find(static_cast<JobType>(i));
		if (it == mJobCounts.end())
			jobCount = 0;
		else
			jobCount = it->second;
		if ((count != 0) || (jobCount != 0) || (latencyPeak != 0))
		{
			Json::Value pri(Json::objectValue);
			pri["job_type"] = Job::toString(static_cast<JobType>(i));
			if (jobCount != 0)
				pri["waiting"] = static_cast<int>(jobCount);
			if (count != 0)
				pri["per_second"] = static_cast<int>(count);
			if (latencyPeak != 0)
				pri["peak_latency"] = static_cast<int>(latencyPeak);
			if (latencyAvg != 0)
				pri["avg_latency"] = static_cast<int>(latencyAvg);
			priorities.append(pri);
		}
	}
	ret["job_types"] = priorities;

	return ret;
}

void JobQueue::shutdown()
{ // shut down the job queue without completing pending jobs
	cLog(lsINFO) << "Job queue shutting down";
	boost::mutex::scoped_lock sl(mJobLock);
	mShuttingDown = true;
	mJobCond.notify_all();
	while (mThreadCount != 0)
		mJobCond.wait(sl);
}

void JobQueue::setThreadCount(int c)
{ // set the number of thread serving the job queue to precisely this number
	if (c == 0)
	{
		c = boost::thread::hardware_concurrency();
		if (c < 2)
			c = 2;
		cLog(lsINFO) << "Auto-tuning to " << c << " validation/transaction/proposal threads";
	}

	boost::mutex::scoped_lock sl(mJobLock);

	while (mJobCounts[jtDEATH] != 0)
		mJobCond.wait(sl);

	while (mThreadCount < c)
	{
		++mThreadCount;
		boost::thread t(boost::bind(&JobQueue::threadEntry, this));
		t.detach();
	}
	while (mThreadCount > c)
	{
		if (mJobCounts[jtDEATH] != 0)
			mJobCond.wait(sl);
		else
		{
			mJobSet.insert(Job(jtDEATH, 0));
			++mJobCounts[jtDEATH];
		}
	}
	mJobCond.notify_one(); // in case we sucked up someone else's signal
}

void JobQueue::threadEntry()
{ // do jobs until asked to stop
	boost::mutex::scoped_lock sl(mJobLock);
	while (1)
	{
		while (mJobSet.empty() && !mShuttingDown)
			mJobCond.wait(sl);

		if (mShuttingDown)
			break;

		std::set<Job>::iterator it = mJobSet.begin();
		Job job(*it);
		mJobSet.erase(it);
		--mJobCounts[job.getType()];

		if (job.getType() == jtDEATH)
			break;

		sl.unlock();
		cLog(lsDEBUG) << "Doing " << Job::toString(job.getType()) << " job";
		job.doJob();
		sl.lock();
	}
	--mThreadCount;
	mJobCond.notify_all();
}
