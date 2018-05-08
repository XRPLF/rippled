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

#include <ripple/core/impl/semaphore.h>
#include <ripple/beast/core/LockFreeStack.h>
#include <ripple/beast/core/WaitableEvent.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace ripple {

namespace perf
{
    class PerfLog;
}

/** A group of threads that process tasks.
*/
class Workers
{
public:
    /** Called to perform tasks as needed. */
    struct Callback
    {
        virtual ~Callback () = default;
        Callback() = default;
        Callback(Callback const&) = delete;
        Callback& operator=(Callback const&) = delete;

        /** Perform a task.

            The call is made on a thread owned by Workers. It is important
            that you only process one task from inside your callback. Each
            call to addTask will result in exactly one call to processTask.

            @param instance The worker thread instance.

            @see Workers::addTask
        */
        virtual void processTask (int instance) = 0;
    };

    /** Create the object.

        A number of initial threads may be optionally specified. The
        default is to create one thread per CPU.

        @param threadNames The name given to each created worker thread.
    */
    explicit Workers (Callback& callback,
                      perf::PerfLog& perfLog,
                      std::string const& threadNames = "Worker",
                      int numberOfThreads =
                         static_cast<int>(std::thread::hardware_concurrency()));

    ~Workers ();

    /** Retrieve the desired number of threads.

        This just returns the number of active threads that were requested. If
        there was a recent call to setNumberOfThreads, the actual number of active
        threads may be temporarily different from what was last requested.

        @note This function is not thread-safe.
    */
    int getNumberOfThreads () const noexcept;

    /** Set the desired number of threads.
        @note This function is not thread-safe.
    */
    void setNumberOfThreads (int numberOfThreads);

    /** Pause all threads and wait until they are paused.

        If a thread is processing a task it will pause as soon as the task
        completes. There may still be tasks signaled even after all threads
        have paused.

        @note This function is not thread-safe.
    */
    void pauseAllThreadsAndWait ();

    /** Add a task to be performed.

        Every call to addTask will eventually result in a call to
        Callback::processTask unless the Workers object is destroyed or
        the number of threads is never set above zero.

        @note This function is thread-safe.
    */
    void addTask ();

    /** Get the number of currently executing calls of Callback::processTask.
        While this function is thread-safe, the value may not stay
        accurate for very long. It's mainly for diagnostic purposes.
    */
    int numberOfCurrentlyRunningTasks () const noexcept;

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
    class Worker
        : public beast::LockFreeStack <Worker>::Node
        , public beast::LockFreeStack <Worker, PausedTag>::Node
    {
    public:
        Worker (Workers& workers,
            std::string const& threadName,
            int const instance);

        ~Worker ();

        void notify ();

    private:
        void run ();

    private:
        Workers& m_workers;
        std::string const threadName_;
        int const instance_;

        std::thread thread_;
        std::mutex mutex_;
        std::condition_variable wakeup_;
        int wakeCount_;               // how many times to un-pause
        bool shouldExit_;
    };

private:
    static void deleteWorkers (beast::LockFreeStack <Worker>& stack);

private:
    Callback& m_callback;
    perf::PerfLog& perfLog_;
    std::string m_threadNames;                   // The name to give each thread
    beast::WaitableEvent m_allPaused;            // signaled when all threads paused
    semaphore m_semaphore;                       // each pending task is 1 resource
    int m_numberOfThreads;                       // how many we want active now
    std::atomic <int> m_activeCount;             // to know when all are paused
    std::atomic <int> m_pauseCount;              // how many threads need to pause now
    std::atomic <int> m_runningTaskCount;        // how many calls to processTask() active
    beast::LockFreeStack <Worker> m_everyone;           // holds all created workers
    beast::LockFreeStack <Worker, PausedTag> m_paused;  // holds just paused workers
};

} // beast

#endif
