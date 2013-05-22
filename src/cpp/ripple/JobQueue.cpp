#include "JobQueue.h"

#include <boost/foreach.hpp>
#include <boost/bind.hpp>
#include <boost/thread.hpp>

#include "Log.h"
#include "Config.h"
#include "Application.h"

JobQueue::JobQueue(boost::asio::io_service& svc)
	: mLastJob(0), mThreadCount(0), mShuttingDown(false), mIOThreadCount(0), mMaxIOThreadCount(1), mIOService(svc)
{
	mJobLoads[jtPUBOLDLEDGER].setTargetLatency(10000, 15000);
	mJobLoads[jtVALIDATION_ut].setTargetLatency(2000, 5000);
	mJobLoads[jtPROOFWORK].setTargetLatency(2000, 5000);
	mJobLoads[jtTRANSACTION].setTargetLatency(250, 1000);
	mJobLoads[jtPROPOSAL_ut].setTargetLatency(500, 1250);
	mJobLoads[jtPUBLEDGER].setTargetLatency(3000, 4500);
	mJobLoads[jtWAL].setTargetLatency(1000, 2500);
	mJobLoads[jtVALIDATION_t].setTargetLatency(500, 1500);
	mJobLoads[jtWRITE].setTargetLatency(750, 1500);
	mJobLoads[jtTRANSACTION_l].setTargetLatency(100, 500);
	mJobLoads[jtPROPOSAL_t].setTargetLatency(100, 500);

	mJobLoads[jtCLIENT].setTargetLatency(2000, 5000);
	mJobLoads[jtPEER].setTargetLatency(200, 1250);
	mJobLoads[jtDISK].setTargetLatency(500, 1000);
	mJobLoads[jtACCEPTLEDGER].setTargetLatency(1000, 2500);
}


const char* Job::toString(JobType t)
{
	switch(t)
	{
		case jtINVALID:			return "invalid";
		case jtPACK:			return "makeFetchPack";
		case jtPUBOLDLEDGER:	return "publishAcqLedger";
		case jtVALIDATION_ut:	return "untrustedValidation";
		case jtPROOFWORK:		return "proofOfWork";
		case jtPROPOSAL_ut:		return "untrustedProposal";
		case jtLEDGER_DATA:		return "ledgerData";
		case jtUPDATE_PF:		return "updatePaths";
		case jtCLIENT:			return "clientCommand";
		case jtTRANSACTION:		return "transaction";
		case jtPUBLEDGER:		return "publishNewLedger";
		case jtVALIDATION_t:	return "trustedValidation";
		case jtWAL:				return "writeAhead";
		case jtWRITE:			return "writeObjects";
		case jtTRANSACTION_l:	return "localTransaction";
		case jtPROPOSAL_t:		return "trustedProposal";
		case jtADMIN:			return "administration";
		case jtDEATH:			return "jobOfDeath";

		case jtPEER:			return "peerCommand";
		case jtDISK:			return "diskAccess";
		case jtACCEPTLEDGER:	return "acceptLedger";
		case jtTXN_PROC:		return "processTransaction";
		case jtOB_SETUP:		return "orderBookSetup";
		case jtPATH_FIND:		return "pathFind";
		case jtHO_READ:			return "nodeRead";
		case jtHO_WRITE:		return "nodeWrite";
		default:				assert(false); return "unknown";
	}
}

bool Job::operator>(const Job& j) const
{ // These comparison operators make the jobs sort in priority order in the job set
	if (mType < j.mType)
		return true;
	if (mType > j.mType)
		return false;
	return mJobIndex > j.mJobIndex;
}

bool Job::operator>=(const Job& j) const
{
	if (mType < j.mType)
		return true;
	if (mType > j.mType)
		return false;
	return mJobIndex >= j.mJobIndex;
}

bool Job::operator<(const Job& j) const
{
	if (mType < j.mType)
		return false;
	if (mType > j.mType)
		return true;
	return mJobIndex < j.mJobIndex;
}

bool Job::operator<=(const Job& j) const
{
	if (mType < j.mType)
		return false;
	if (mType > j.mType)
		return true;
	return mJobIndex <= j.mJobIndex;
}

void JobQueue::addJob(JobType type, const std::string& name, const FUNCTION_TYPE<void(Job&)>& jobFunc)
{
	assert(type != jtINVALID);

	boost::mutex::scoped_lock sl(mJobLock);

	if (type != jtCLIENT) // FIXME: Workaround incorrect client shutdown ordering
		assert(mThreadCount != 0); // do not add jobs to a queue with no threads

	mJobSet.insert(Job(type, name, ++mLastJob, mJobLoads[type], jobFunc));
	++mJobCounts[type].first;
	mJobCond.notify_one();
}

int JobQueue::getJobCount(JobType t)
{
	boost::mutex::scoped_lock sl(mJobLock);

	std::map< JobType, std::pair<int, int> >::iterator c = mJobCounts.find(t);
	return (c == mJobCounts.end()) ? 0 : c->second.first;
}

int JobQueue::getJobCountTotal(JobType t)
{
	boost::mutex::scoped_lock sl(mJobLock);

	std::map< JobType, std::pair<int, int> >::iterator c = mJobCounts.find(t);
	return (c == mJobCounts.end()) ? 0 : (c->second.first + c->second.second);
}

int JobQueue::getJobCountGE(JobType t)
{ // return the number of jobs at this priority level or greater
	int ret = 0;

	boost::mutex::scoped_lock sl(mJobLock);

	typedef std::map< JobType, std::pair<int, int> >::value_type jt_int_pair;
	BOOST_FOREACH(const jt_int_pair& it, mJobCounts)
		if (it.first >= t)
			ret += it.second.first;
	return ret;
}

std::vector< std::pair<JobType, std::pair<int, int> > > JobQueue::getJobCounts()
{ // return all jobs at all priority levels
	std::vector< std::pair<JobType, std::pair<int, int> > > ret;

	boost::mutex::scoped_lock sl(mJobLock);
	ret.reserve(mJobCounts.size());

	typedef std::map< JobType, std::pair<int, int> >::value_type jt_int_pair;
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
		uint64 count, latencyAvg, latencyPeak;
		int jobCount, threadCount;
		bool isOver;
		mJobLoads[i].getCountAndLatency(count, latencyAvg, latencyPeak, isOver);
		std::map< JobType, std::pair<int, int> >::iterator it = mJobCounts.find(static_cast<JobType>(i));
		if (it == mJobCounts.end())
		{
			jobCount = 0;
			threadCount = 0;
		}
		else
		{
			jobCount = it->second.first;
			threadCount = it->second.second;
		}

		if ((count != 0) || (jobCount != 0) || (latencyPeak != 0) || (threadCount != 0))
		{
			Json::Value pri(Json::objectValue);
			if (isOver)
				pri["over_target"] = true;
			pri["job_type"] = Job::toString(static_cast<JobType>(i));
			if (jobCount != 0)
				pri["waiting"] = jobCount;
			if (count != 0)
				pri["per_second"] = static_cast<int>(count);
			if (latencyPeak != 0)
				pri["peak_time"] = static_cast<int>(latencyPeak);
			if (latencyAvg != 0)
				pri["avg_time"] = static_cast<int>(latencyAvg);
			if (threadCount != 0)
				pri["in_progress"] = threadCount;
			priorities.append(pri);
		}
	}
	ret["job_types"] = priorities;

	return ret;
}

int JobQueue::isOverloaded()
{
	int count = 0;
	boost::mutex::scoped_lock sl(mJobLock);
	for (int i = 0; i < NUM_JOB_TYPES; ++i)
		if (mJobLoads[i].isOver())
			++count;
	return count;
}

void JobQueue::shutdown()
{ // shut down the job queue without completing pending jobs
	WriteLog (lsINFO, JobQueue) << "Job queue shutting down";
	boost::mutex::scoped_lock sl(mJobLock);
	mShuttingDown = true;
	mJobCond.notify_all();
	while (mThreadCount != 0)
		mJobCond.wait(sl);
}

void JobQueue::setThreadCount(int c)
{ // set the number of thread serving the job queue to precisely this number
	if (theConfig.RUN_STANDALONE)
		c = 1;
	else if (c == 0)
	{
		c = boost::thread::hardware_concurrency();
		if (c < 0)
			c = 2;
		if (c > 4) // I/O will bottleneck
			c = 4;
		c += 2;
		WriteLog (lsINFO, JobQueue) << "Auto-tuning to " << c << " validation/transaction/proposal threads";
	}

	boost::mutex::scoped_lock sl(mJobLock);

	mMaxIOThreadCount = 1 + (c / 3);

	while (mJobCounts[jtDEATH].first != 0)
		mJobCond.wait(sl);

	while (mThreadCount < c)
	{
		++mThreadCount;
		boost::thread(BIND_TYPE(&JobQueue::threadEntry, this)).detach();
	}
	while (mThreadCount > c)
	{
		if (mJobCounts[jtDEATH].first != 0)
			mJobCond.wait(sl);
		else
		{
			mJobSet.insert(Job(jtDEATH, 0));
			++(mJobCounts[jtDEATH].first);
		}
	}
	mJobCond.notify_one(); // in case we sucked up someone else's signal
}

void JobQueue::IOThread(boost::mutex::scoped_lock& sl)
{ // call with a lock
	++mIOThreadCount;
	sl.unlock();
	NameThread("IO+");
	try
	{
		mIOService.poll();
	}
	catch (...)
	{
		WriteLog (lsWARNING, JobQueue) << "Exception in IOThread";
	}
	NameThread("waiting");
	sl.lock();
	--mIOThreadCount;
}

void JobQueue::threadEntry()
{ // do jobs until asked to stop
	boost::mutex::scoped_lock sl(mJobLock);
	while (1)
	{
		NameThread("waiting");
//		bool didIO = false;
		while (mJobSet.empty() && !mShuttingDown)
		{
//			if ((mIOThreadCount < mMaxIOThreadCount) && !didIO && !theApp->isShutdown())
//			{
//				IOThread(sl);
//				didIO = true;
//			}
//			else
//			{
				mJobCond.wait(sl);
//				didIO = false;
//			}
		}

		if (mJobSet.empty())
			break;

		JobType type;
		std::set<Job>::iterator it = mJobSet.begin();
		{
			Job job(*it);
			mJobSet.erase(it);

			type = job.getType();
			--(mJobCounts[type].first);

			if (type == jtDEATH)
				break;

			++(mJobCounts[type].second);
			sl.unlock();
			NameThread(Job::toString(type));
			WriteLog (lsTRACE, JobQueue) << "Doing " << Job::toString(type) << " job";
			job.doJob();
		} // must destroy job without holding lock
		sl.lock();
		--(mJobCounts[type].second);
	}
	--mThreadCount;
	mJobCond.notify_all();
}

// vim:ts=4
