//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <xrpld/core/detail/Workers.h>
#include <xrpld/perflog/PerfLog.h>
#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/beast/utility/instrumentation.h>

namespace ripple {

Workers::Workers(
    Callback& callback,
    perf::PerfLog* perfLog,
    std::string const& threadNames,
    int numberOfThreads)
    : m_callback(callback)
    , perfLog_(perfLog)
    , m_threadNames(threadNames)
    , m_allPaused(true)
    , m_semaphore(0)
    , m_numberOfThreads(0)
    , m_activeCount(0)
    , m_pauseCount(0)
    , m_runningTaskCount(0)
{
    setNumberOfThreads(numberOfThreads);
}

Workers::~Workers()
{
    stop();

    deleteWorkers(m_everyone);
}

int
Workers::getNumberOfThreads() const noexcept
{
    return m_numberOfThreads;
}

// VFALCO NOTE if this function is called quickly to reduce then
//             increase the number of threads, it could result in
//             more paused threads being created than expected.
//
void
Workers::setNumberOfThreads(int numberOfThreads)
{
    static int instance{0};
    if (m_numberOfThreads == numberOfThreads)
        return;

    if (perfLog_)
        perfLog_->resizeJobs(numberOfThreads);

    if (numberOfThreads > m_numberOfThreads)
    {
        // Increasing the number of working threads
        int const amount = numberOfThreads - m_numberOfThreads;

        for (int i = 0; i < amount; ++i)
        {
            // See if we can reuse a paused worker
            Worker* worker = m_paused.pop_front();

            if (worker != nullptr)
            {
                // If we got here then the worker thread is at [1]
                // This will unblock their call to wait()
                //
                worker->notify();
            }
            else
            {
                worker = new Worker(*this, m_threadNames, instance++);
                m_everyone.push_front(worker);
            }
        }
    }
    else
    {
        // Decreasing the number of working threads
        int const amount = m_numberOfThreads - numberOfThreads;

        for (int i = 0; i < amount; ++i)
        {
            ++m_pauseCount;

            // Pausing a thread counts as one "internal task"
            m_semaphore.notify();
        }
    }

    m_numberOfThreads = numberOfThreads;
}

void
Workers::stop()
{
    setNumberOfThreads(0);

    std::unique_lock<std::mutex> lk{m_mut};
    m_cv.wait(lk, [this] { return m_allPaused; });
    lk.unlock();

    ASSERT(
        numberOfCurrentlyRunningTasks() == 0,
        "ripple::Workers::stop : zero running tasks");
}

void
Workers::addTask()
{
    m_semaphore.notify();
}

int
Workers::numberOfCurrentlyRunningTasks() const noexcept
{
    return m_runningTaskCount.load();
}

void
Workers::deleteWorkers(beast::LockFreeStack<Worker>& stack)
{
    for (;;)
    {
        Worker* const worker = stack.pop_front();

        if (worker != nullptr)
        {
            // This call blocks until the thread orderly exits
            delete worker;
        }
        else
        {
            break;
        }
    }
}

//------------------------------------------------------------------------------

Workers::Worker::Worker(
    Workers& workers,
    std::string const& threadName,
    int const instance)
    : m_workers{workers}
    , threadName_{threadName}
    , instance_{instance}
    , wakeCount_{0}
    , shouldExit_{false}
{
    thread_ = std::thread{&Workers::Worker::run, this};
}

Workers::Worker::~Worker()
{
    {
        std::lock_guard lock{mutex_};
        ++wakeCount_;
        shouldExit_ = true;
    }

    wakeup_.notify_one();
    thread_.join();
}

void
Workers::Worker::notify()
{
    std::lock_guard lock{mutex_};
    ++wakeCount_;
    wakeup_.notify_one();
}

void
Workers::Worker::run()
{
    bool shouldExit = true;
    do
    {
        // Increment the count of active workers, and if
        // we are the first one then reset the "all paused" event
        //
        if (++m_workers.m_activeCount == 1)
        {
            std::lock_guard lk{m_workers.m_mut};
            m_workers.m_allPaused = false;
        }

        for (;;)
        {
            // Put the name back in case the callback changed it
            beast::setCurrentThreadName(threadName_);

            // Acquire a task or "internal task."
            //
            m_workers.m_semaphore.wait();

            // See if there's a pause request. This
            // counts as an "internal task."
            //
            int pauseCount = m_workers.m_pauseCount.load();

            if (pauseCount > 0)
            {
                // Try to decrement
                pauseCount = --m_workers.m_pauseCount;

                if (pauseCount >= 0)
                {
                    // We got paused
                    break;
                }
                else
                {
                    // Undo our decrement
                    ++m_workers.m_pauseCount;
                }
            }

            // We couldn't pause so we must have gotten
            // unblocked in order to process a task.
            //
            ++m_workers.m_runningTaskCount;
            m_workers.m_callback.processTask(instance_);
            --m_workers.m_runningTaskCount;
        }

        // Any worker that goes into the paused list must
        // guarantee that it will eventually block on its
        // event object.
        //
        m_workers.m_paused.push_front(this);

        // Decrement the count of active workers, and if we
        // are the last one then signal the "all paused" event.
        //
        if (--m_workers.m_activeCount == 0)
        {
            std::lock_guard lk{m_workers.m_mut};
            m_workers.m_allPaused = true;
            m_workers.m_cv.notify_all();
        }

        // Set inactive thread name.
        beast::setCurrentThreadName("(" + threadName_ + ")");

        // [1] We will be here when the paused list is popped
        //
        // We block on our condition_variable, wakeup_, a requirement of being
        // put into the paused list.
        //
        // wakeup_ will get signaled by either Worker::notify() or ~Worker.
        {
            std::unique_lock<std::mutex> lock{mutex_};
            wakeup_.wait(lock, [this] { return this->wakeCount_ > 0; });

            shouldExit = shouldExit_;
            --wakeCount_;
        }
    } while (!shouldExit);
}

}  // namespace ripple
