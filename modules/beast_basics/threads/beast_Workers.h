//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_WORKERS_H_INCLUDED
#define BEAST_WORKERS_H_INCLUDED

/** A group of threads that process tasks.
*/
class Workers
{
public:
    /** Called to perform tasks as needed. */
    struct Callback
    {
        /** Perform a task.
            The call is made on a thread owned by Workers.
        */
        virtual void processTask () = 0;
    };

    /** Create the object.

        A number of initial threads may be optionally specified. The
        default is to create one thread per CPU.
    */
    explicit Workers (Callback& callback, int numberOfThreads = SystemStats::getNumCpus ());

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

    /** Increment the number of tasks.
        The callback will be called for each task.
        @note This function is thread-safe.
    */
    void addTask ();

    //--------------------------------------------------------------------------

private:
    struct PausedTag { };

    /*  A Worker executes tasks on its provided thread.

        These are the states:

        Active: Running the task processing loop.
        Idle:   Active, but blocked on waiting for a task.
        Pausd:  Blocked waiting to exit or become active.
    */

    class Worker
        : public LockFreeStack <Worker>::Node
        , public LockFreeStack <Worker, PausedTag>::Node
        , public Thread
    {
    public:
        explicit Worker (Workers& workers);

        ~Worker ();

    private:
        void run ();

    private:
        Workers& m_workers;
    };

private:
    static void deleteWorkers (LockFreeStack <Worker>& stack);

private:
    Callback& m_callback;
    WaitableEvent m_allPaused;                   // signaled when all threads paused
    Semaphore m_semaphore;                       // each pending task is 1 resource
    int m_numberOfThreads;                       // how many we want active now
    Atomic <int> m_activeCount;                  // to know when all are paused
    Atomic <int> m_pauseCount;                   // how many threads need to pause now
    LockFreeStack <Worker> m_everyone;           // holds all created workers
    LockFreeStack <Worker, PausedTag> m_paused;  // holds just paused workers
};

#endif
