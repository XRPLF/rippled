#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/PerfLog.h>
#include <xrpl/core/detail/Workers.h>

namespace xrpl {

Workers::Workers(
    Callback& callback,
    perf::PerfLog* perfLog,
    std::string const& threadNames,
    int numberOfThreads)
    : callback_(callback)
    , perfLog_(perfLog)
    , threadNames_(threadNames)
    , allPaused_(true)
    , semaphore_(0)
    , numberOfThreads_(0)
    , activeCount_(0)
    , pauseCount_(0)
    , runningTaskCount_(0)
{
    setNumberOfThreads(numberOfThreads);
}

Workers::~Workers()
{
    stop();

    deleteWorkers(everyone_);
}

int
Workers::getNumberOfThreads() const noexcept
{
    return numberOfThreads_;
}

// VFALCO NOTE if this function is called quickly to reduce then
//             increase the number of threads, it could result in
//             more paused threads being created than expected.
//
void
Workers::setNumberOfThreads(int numberOfThreads)
{
    static int kINSTANCE{0};
    if (numberOfThreads_ == numberOfThreads)
        return;

    if (perfLog_)
        perfLog_->resizeJobs(numberOfThreads);

    if (numberOfThreads > numberOfThreads_)
    {
        // Increasing the number of working threads
        int const amount = numberOfThreads - numberOfThreads_;

        for (int i = 0; i < amount; ++i)
        {
            // See if we can reuse a paused worker
            Worker* worker = paused_.pop_front();

            if (worker != nullptr)
            {
                // If we got here then the worker thread is at [1]
                // This will unblock their call to wait()
                //
                worker->notify();
            }
            else
            {
                worker = new Worker(*this, threadNames_, kINSTANCE++);
                everyone_.push_front(worker);
            }
        }
    }
    else
    {
        // Decreasing the number of working threads
        int const amount = numberOfThreads_ - numberOfThreads;

        for (int i = 0; i < amount; ++i)
        {
            ++pauseCount_;

            // Pausing a thread counts as one "internal task"
            semaphore_.notify();
        }
    }

    numberOfThreads_ = numberOfThreads;
}

void
Workers::stop()
{
    setNumberOfThreads(0);

    std::unique_lock<std::mutex> lk{mut_};
    cv_.wait(lk, [this] { return allPaused_; });
    lk.unlock();

    XRPL_ASSERT(numberOfCurrentlyRunningTasks() == 0, "xrpl::Workers::stop : zero running tasks");
}

void
Workers::addTask()
{
    semaphore_.notify();
}

int
Workers::numberOfCurrentlyRunningTasks() const noexcept
{
    return runningTaskCount_.load();
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

Workers::Worker::Worker(Workers& workers, std::string const& threadName, int const instance)
    : workers_{workers}
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
        if (++workers_.activeCount_ == 1)
        {
            std::lock_guard lk{workers_.mut_};
            workers_.allPaused_ = false;
        }

        for (;;)
        {
            // Put the name back in case the callback changed it
            beast::setCurrentThreadName(threadName_);

            // Acquire a task or "internal task."
            //
            workers_.semaphore_.wait();

            // See if there's a pause request. This
            // counts as an "internal task."
            //
            int pauseCount = workers_.pauseCount_.load();

            if (pauseCount > 0)
            {
                // Try to decrement
                pauseCount = --workers_.pauseCount_;

                if (pauseCount >= 0)
                {
                    // We got paused
                    break;
                }
                else
                {
                    // Undo our decrement
                    ++workers_.pauseCount_;
                }
            }

            // We couldn't pause so we must have gotten
            // unblocked in order to process a task.
            //
            ++workers_.runningTaskCount_;
            workers_.callback_.processTask(instance_);
            --workers_.runningTaskCount_;
        }

        // Any worker that goes into the paused list must
        // guarantee that it will eventually block on its
        // event object.
        //
        workers_.paused_.push_front(this);

        // Decrement the count of active workers, and if we
        // are the last one then signal the "all paused" event.
        //
        if (--workers_.activeCount_ == 0)
        {
            std::lock_guard lk{workers_.mut_};
            workers_.allPaused_ = true;
            workers_.cv_.notify_all();
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

}  // namespace xrpl
