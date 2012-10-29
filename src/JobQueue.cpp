#include "JobQueue.h"

#include <boost/make_shared.hpp>
#include <boost/foreach.hpp>
#include <boost/bind.hpp>
#include <boost/thread.hpp>

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
		default:				assert(false); return "unknown";
	}
}

bool Job::operator<(const Job& j) const
{
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

void JobQueue::addJob(JobType type, const boost::function<void(void)>& jobFunc)
{
	assert(type != jtINVALID);

	boost::mutex::scoped_lock sl(mJobLock);

	mJobSet.insert(Job(type, ++mLastJob, jobFunc));	
	++mJobCounts[type];
	mJobCond.notify_one();
}

int JobQueue::getJobCount(JobType t)
{
	int ret = 0;

	boost::mutex::scoped_lock sl(mJobLock);

	typedef std::pair<JobType, int> jt_int_pair;
	BOOST_FOREACH(const jt_int_pair& it, mJobCounts)
		if (it.first >= t)
			ret += it.second;
	return ret;
}

std::vector< std::pair<JobType, int> > JobQueue::getJobCounts()
{
	std::vector< std::pair<JobType, int> > ret;

	boost::mutex::scoped_lock sl(mJobLock);
	ret.reserve(mJobCounts.size());

	typedef std::pair<JobType, int> jt_int_pair;
	BOOST_FOREACH(const jt_int_pair& it, mJobCounts)
		ret.push_back(it);

	return ret;
}

void JobQueue::shutdown()
{
	boost::mutex::scoped_lock sl(mJobLock);
	mShuttingDown = true;
	mJobCond.notify_all();
	while (mThreadCount != 0)
		mJobCond.wait(sl);
}

void JobQueue::setThreadCount(int c)
{
	boost::mutex::scoped_lock sl(mJobLock);
	while (mThreadCount != c)
	{
		if (mThreadCount < c)
		{
			++mThreadCount;
			boost::thread t(boost::bind(&JobQueue::threadEntry, this));
			t.detach();
		}
		if (mThreadCount > c)
		{
			if (mJobCounts[jtDEATH] != 0)
				mJobCond.wait(sl);
			else
			{
				mJobSet.insert(Job(jtDEATH, 0));
				++mJobCounts[jtDEATH];
			}
		}
	}
}

void JobQueue::threadEntry()
{
	boost::mutex::scoped_lock sl(mJobLock);
	while (!mShuttingDown)
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
		job.doJob();
		sl.lock();
	}
	--mThreadCount;
	mJobCond.notify_all();
}
