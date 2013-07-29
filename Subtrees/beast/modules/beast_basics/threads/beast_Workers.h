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

    /** Set the desired number of threads.

        @note This function is not thread-safe.
    */
    void setNumberOfThreads (int numberOfThreads);

    /** Increment the number of tasks.

        The callback will be called for each task.

        @note This function is thread-safe.
    */
    void addTask ();

    //--------------------------------------------------------------------------

private:
    class Worker
        : public LockFreeStack <Worker>::Node
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
    Semaphore m_semaphore;
    int m_numberOfThreads;

    WaitableEvent m_allPaused;
    Atomic <int> m_activeCount;
    Atomic <int> m_pauseCount;
    LockFreeStack <Worker> m_active;
    LockFreeStack <Worker> m_paused;
};

#endif
