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

#ifndef RIPPLE_CORE_WORKERS_H_INCLUDED
#define RIPPLE_CORE_WORKERS_H_INCLUDED

#include <ripple/beast/core/LockFreeStack.h>
#include <ripple/core/impl/semaphore.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace ripple {

namespace perf {
class PerfLog;
}

/**
 * `Workers` is effectively a thread pool. The constructor takes a "callback"
 * that has a `void processTask(int instance)` method, and a number of
 * workers. It creates that many `Worker`s and then waits for calls to
 * `Workers::addTask()`. It holds a semaphore that counts the number of
 * pending "tasks", and a condition variable for the event when the last
 * worker pauses itself.
 *
 * A "task" is just a call to the callback's `processTask` method.
 * "Adding a task" means calling that method now, or remembering to call it in
 * the future.
 * This is implemented with a semaphore.
 * If there are any workers waiting when a task is added, then one will be
 * woken to claim the task.
 * If not, then the next worker to wait on the semaphore will claim the task.
 *
 * Creating a `Worker` creates a thread that calls `Worker::run()`. When that
 * thread enters `Worker::run`, it increments the count of active workers in
 * the parent `Workers` object and then tries to claim a task, which blocks if
 * there are none pending.
 * It will be unblocked whenever the semaphore is notified (i.e. when the
 * number of pending tasks is incremented).
 * That only happens in two circumstances: (1) when
 * `Workers::addTask` is called and (2) when `Workers` wants to pause some
 * workers ("pause one worker" is considered one task), which happens when
 * someone wants to stop the workers or shrink the threadpool. No worker
 * threads are ever destroyed until `Workers` is destroyed; it merely pauses
 * workers until then.
 *
 * When a waiting worker is woken, it checks whether `Workers` is trying to
 * pause workers. If so, it changes its status from active to paused and
 * blocks on
 * its own condition variable. If not, then it calls `processTask` on the
 * "callback" held by `Workers`.
 *
 * When a paused worker is woken, it checks whether it should exit. The signal
 * to exit is only set in the destructor of `Worker`, which unblocks the
 * paused thread and waits for it to exit. A `Worker::run` thread checks
 * whether it needs to exit only when it is woken from a pause (not when it is
 * woken from waiting). This is why the destructor for `Workers` pauses all
 * the workers before destroying them.
 */
class Workers
{
public:
    /** Called to perform tasks as needed. */
    struct Callback
    {
        virtual ~Callback() = default;
        Callback() = default;
        Callback(Callback const&) = delete;
        Callback&
        operator=(Callback const&) = delete;

        /** Perform a task.

            The call is made on a thread owned by Workers. It is important
            that you only process one task from inside your callback. Each
            call to addTask will result in exactly one call to processTask.

            @param instance The worker thread instance.

            @see Workers::addTask
        */
        virtual void
        processTask(int instance) = 0;
    };

    /** Create the object.

        A number of initial threads may be optionally specified. The
        default is to create one thread per CPU.

        @param threadNames The name given to each created worker thread.
    */
    explicit Workers(
        Callback& callback,
        perf::PerfLog* perfLog,
        std::string const& threadNames = "Worker",
        int numberOfThreads =
            static_cast<int>(std::thread::hardware_concurrency()));

    ~Workers();

    /** Retrieve the desired number of threads.

        This just returns the number of active threads that were requested. If
        there was a recent call to setNumberOfThreads, the actual number of
       active threads may be temporarily different from what was last requested.

        @note This function is not thread-safe.
    */
    int
    getNumberOfThreads() const noexcept;

    /** Set the desired number of threads.
        @note This function is not thread-safe.
    */
    void
    setNumberOfThreads(int numberOfThreads);

    /** Pause all threads and wait until they are paused.

        If a thread is processing a task it will pause as soon as the task
        completes. There may still be tasks signaled even after all threads
        have paused.

        @note This function is not thread-safe.
    */
    void
    stop();

    /** Add a task to be performed.

        Every call to addTask will eventually result in a call to
        Callback::processTask unless the Workers object is destroyed or
        the number of threads is never set above zero.

        @note This function is thread-safe.
    */
    void
    addTask();

    /** Get the number of currently executing calls of Callback::processTask.
        While this function is thread-safe, the value may not stay
        accurate for very long. It's mainly for diagnostic purposes.
    */
    int
    numberOfCurrentlyRunningTasks() const noexcept;

    //--------------------------------------------------------------------------

private:
    struct PausedTag
    {
        explicit PausedTag() = default;
    };

    /*  A Worker executes tasks on its provided thread.

        These are the states:

        Active: Running the task processing loop.
        Idle:   Active, but blocked on waiting for a task.
        Paused: Blocked waiting to exit or become active.
    */
    class Worker : public beast::LockFreeStack<Worker>::Node,
                   public beast::LockFreeStack<Worker, PausedTag>::Node
    {
    public:
        Worker(
            Workers& workers,
            std::string const& threadName,
            int const instance);

        ~Worker();

        void
        notify();

    private:
        void
        run();

    private:
        Workers& m_workers;
        std::string const threadName_;
        int const instance_;

        std::thread thread_;
        std::mutex mutex_;
        std::condition_variable wakeup_;
        int wakeCount_;  // how many times to un-pause
        bool shouldExit_;
    };

private:
    static void
    deleteWorkers(beast::LockFreeStack<Worker>& stack);

private:
    Callback& m_callback;
    perf::PerfLog* perfLog_;
    std::string m_threadNames;     // The name to give each thread
    std::condition_variable m_cv;  // signaled when all threads paused
    std::mutex m_mut;
    bool m_allPaused;
    semaphore m_semaphore;           // each pending task is 1 resource
    int m_numberOfThreads;           // how many we want active now
    std::atomic<int> m_activeCount;  // to know when all are paused
    std::atomic<int> m_pauseCount;   // how many threads need to pause now
    std::atomic<int>
        m_runningTaskCount;  // how many calls to processTask() active
    beast::LockFreeStack<Worker> m_everyone;  // holds all created workers
    beast::LockFreeStack<Worker, PausedTag>
        m_paused;  // holds just paused workers
};

}  // namespace ripple

#endif
