#ifndef RIPPLE_JOBQUEUE_H
#define RIPPLE_JOBQUEUE_H

class JobQueue
{
public:
	explicit JobQueue (boost::asio::io_service&);

	void addJob(JobType type, const std::string& name, const FUNCTION_TYPE<void(Job&)>& job);

	int getJobCount(JobType t);			// Jobs waiting at this priority
	int getJobCountTotal(JobType t);	// Jobs waiting plus running at this priority
	int getJobCountGE(JobType t);		// All waiting jobs at or greater than this priority
	std::vector< std::pair<JobType, std::pair<int, int> > > getJobCounts(); // jobs waiting, threads doing

	void shutdown();
	void setThreadCount(int c = 0);

	LoadEvent::pointer getLoadEvent(JobType t, const std::string& name)
	{
        return boost::make_shared<LoadEvent>(boost::ref(mJobLoads[t]), name, true);
    }

    LoadEvent::autoptr getLoadEventAP(JobType t, const std::string& name)
	{
        return LoadEvent::autoptr(new LoadEvent(mJobLoads[t], name, true));
    }

	int isOverloaded();
	Json::Value getJson(int c = 0);

private:
	void threadEntry();
	void IOThread(boost::mutex::scoped_lock&);

    boost::mutex					mJobLock;
	boost::condition_variable		mJobCond;

	uint64							mLastJob;
	std::set <Job>					mJobSet;
	LoadMonitor						mJobLoads [NUM_JOB_TYPES];
	int								mThreadCount;
	bool							mShuttingDown;

	int								mIOThreadCount;
	int								mMaxIOThreadCount;
	boost::asio::io_service&		mIOService;

	std::map<JobType, std::pair<int, int > >	mJobCounts;
};

#endif
