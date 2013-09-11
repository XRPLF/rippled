//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

ThreadPoolJob::ThreadPoolJob (const String& name)
    : jobName (name),
      pool (nullptr),
      shouldStop (false),
      isActive (false),
      shouldBeDeleted (false)
{
}

ThreadPoolJob::~ThreadPoolJob()
{
    // you mustn't delete a job while it's still in a pool! Use ThreadPool::removeJob()
    // to remove it first!
    bassert (pool == nullptr || ! pool->contains (this));
}

String ThreadPoolJob::getJobName() const
{
    return jobName;
}

void ThreadPoolJob::setJobName (const String& newName)
{
    jobName = newName;
}

void ThreadPoolJob::signalJobShouldExit()
{
    shouldStop = true;
}

//==============================================================================
class ThreadPool::ThreadPoolThread
    : public Thread
    , LeakChecked <ThreadPoolThread>
{
public:
    ThreadPoolThread (ThreadPool& pool_)
        : Thread ("Pool"),
          pool (pool_)
    {
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            if (! pool.runNextJob())
                wait (500);
        }
    }

private:
    ThreadPool& pool;
};

//==============================================================================
ThreadPool::ThreadPool (const int numThreads)
{
    bassert (numThreads > 0); // not much point having a pool without any threads!

    createThreads (numThreads);
}

ThreadPool::ThreadPool()
{
    createThreads (SystemStats::getNumCpus());
}

ThreadPool::~ThreadPool()
{
    removeAllJobs (true, 5000);
    stopThreads();
}

void ThreadPool::createThreads (int numThreads)
{
    for (int i = bmax (1, numThreads); --i >= 0;)
        threads.add (new ThreadPoolThread (*this));

    for (int i = threads.size(); --i >= 0;)
        threads.getUnchecked(i)->startThread();
}

void ThreadPool::stopThreads()
{
    for (int i = threads.size(); --i >= 0;)
        threads.getUnchecked(i)->signalThreadShouldExit();

    for (int i = threads.size(); --i >= 0;)
        threads.getUnchecked(i)->stopThread (500);
}

void ThreadPool::addJob (ThreadPoolJob* const job, const bool deleteJobWhenFinished)
{
    bassert (job != nullptr);
    bassert (job->pool == nullptr);

    if (job->pool == nullptr)
    {
        job->pool = this;
        job->shouldStop = false;
        job->isActive = false;
        job->shouldBeDeleted = deleteJobWhenFinished;

        {
            const ScopedLock sl (lock);
            jobs.add (job);
        }

        for (int i = threads.size(); --i >= 0;)
            threads.getUnchecked(i)->notify();
    }
}

int ThreadPool::getNumJobs() const
{
    return jobs.size();
}

ThreadPoolJob* ThreadPool::getJob (const int index) const
{
    const ScopedLock sl (lock);
    return jobs [index];
}

bool ThreadPool::contains (const ThreadPoolJob* const job) const
{
    const ScopedLock sl (lock);
    return jobs.contains (const_cast <ThreadPoolJob*> (job));
}

bool ThreadPool::isJobRunning (const ThreadPoolJob* const job) const
{
    const ScopedLock sl (lock);
    return jobs.contains (const_cast <ThreadPoolJob*> (job)) && job->isActive;
}

bool ThreadPool::waitForJobToFinish (const ThreadPoolJob* const job,
                                     const int timeOutMs) const
{
    if (job != nullptr)
    {
        const uint32 start = Time::getMillisecondCounter();

        while (contains (job))
        {
            if (timeOutMs >= 0 && Time::getMillisecondCounter() >= start + (uint32) timeOutMs)
                return false;

            jobFinishedSignal.wait (2);
        }
    }

    return true;
}

bool ThreadPool::removeJob (ThreadPoolJob* const job,
                            const bool interruptIfRunning,
                            const int timeOutMs)
{
    bool dontWait = true;
    OwnedArray<ThreadPoolJob> deletionList;

    if (job != nullptr)
    {
        const ScopedLock sl (lock);

        if (jobs.contains (job))
        {
            if (job->isActive)
            {
                if (interruptIfRunning)
                    job->signalJobShouldExit();

                dontWait = false;
            }
            else
            {
                jobs.removeFirstMatchingValue (job);
                addToDeleteList (deletionList, job);
            }
        }
    }

    return dontWait || waitForJobToFinish (job, timeOutMs);
}

bool ThreadPool::removeAllJobs (const bool interruptRunningJobs, const int timeOutMs,
                                ThreadPool::JobSelector* selectedJobsToRemove)
{
    Array <ThreadPoolJob*> jobsToWaitFor;

    {
        OwnedArray<ThreadPoolJob> deletionList;

        {
            const ScopedLock sl (lock);

            for (int i = jobs.size(); --i >= 0;)
            {
                ThreadPoolJob* const job = jobs.getUnchecked(i);

                if (selectedJobsToRemove == nullptr || selectedJobsToRemove->isJobSuitable (job))
                {
                    if (job->isActive)
                    {
                        jobsToWaitFor.add (job);

                        if (interruptRunningJobs)
                            job->signalJobShouldExit();
                    }
                    else
                    {
                        jobs.remove (i);
                        addToDeleteList (deletionList, job);
                    }
                }
            }
        }
    }

    const uint32 start = Time::getMillisecondCounter();

    for (;;)
    {
        for (int i = jobsToWaitFor.size(); --i >= 0;)
        {
            ThreadPoolJob* const job = jobsToWaitFor.getUnchecked (i);

            if (! isJobRunning (job))
                jobsToWaitFor.remove (i);
        }

        if (jobsToWaitFor.size() == 0)
            break;

        if (timeOutMs >= 0 && Time::getMillisecondCounter() >= start + (uint32) timeOutMs)
            return false;

        jobFinishedSignal.wait (20);
    }

    return true;
}

StringArray ThreadPool::getNamesOfAllJobs (const bool onlyReturnActiveJobs) const
{
    StringArray s;
    const ScopedLock sl (lock);

    for (int i = 0; i < jobs.size(); ++i)
    {
        const ThreadPoolJob* const job = jobs.getUnchecked(i);
        if (job->isActive || ! onlyReturnActiveJobs)
            s.add (job->getJobName());
    }

    return s;
}

bool ThreadPool::setThreadPriorities (const int newPriority)
{
    bool ok = true;

    for (int i = threads.size(); --i >= 0;)
        if (! threads.getUnchecked(i)->setPriority (newPriority))
            ok = false;

    return ok;
}

ThreadPoolJob* ThreadPool::pickNextJobToRun()
{
    OwnedArray<ThreadPoolJob> deletionList;

    {
        const ScopedLock sl (lock);

        for (int i = 0; i < jobs.size(); ++i)
        {
            ThreadPoolJob* job = jobs[i];

            if (job != nullptr && ! job->isActive)
            {
                if (job->shouldStop)
                {
                    jobs.remove (i);
                    addToDeleteList (deletionList, job);
                    --i;
                    continue;
                }

                job->isActive = true;
                return job;
            }
        }
    }

    return nullptr;
}

bool ThreadPool::runNextJob()
{
    ThreadPoolJob* const job = pickNextJobToRun();

    if (job == nullptr)
        return false;

    ThreadPoolJob::JobStatus result = ThreadPoolJob::jobHasFinished;

    result = job->runJob();

    OwnedArray<ThreadPoolJob> deletionList;

    {
        const ScopedLock sl (lock);

        if (jobs.contains (job))
        {
            job->isActive = false;

            if (result != ThreadPoolJob::jobNeedsRunningAgain || job->shouldStop)
            {
                jobs.removeFirstMatchingValue (job);
                addToDeleteList (deletionList, job);

                jobFinishedSignal.signal();
            }
            else
            {
                // move the job to the end of the queue if it wants another go
                jobs.move (jobs.indexOf (job), -1);
            }
        }
    }

    return true;
}

void ThreadPool::addToDeleteList (OwnedArray<ThreadPoolJob>& deletionList, ThreadPoolJob* const job) const
{
    job->shouldStop = true;
    job->pool = nullptr;

    if (job->shouldBeDeleted)
        deletionList.add (job);
}
