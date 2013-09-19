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

#ifndef BEAST_INTERRUPTIBLETHREAD_H_INCLUDED
#define BEAST_INTERRUPTIBLETHREAD_H_INCLUDED

//==============================================================================
/**
    A thread with soft interruption support.

    The thread must periodically call interruptionPoint(), which returns `true`
    the first time an interruption has occurred since the last call to
    interruptionPoint().

    To create a thread, derive your class from InterruptibleThread::EntryPoint
    and implement the threadRun() function. Then, call run() with your object.

    @ingroup beast_core
*/
class BEAST_API InterruptibleThread
{
public:
    /** InterruptibleThread entry point.
    */
    class EntryPoint
    {
    public:
        virtual ~EntryPoint () { }

        virtual void threadRun () = 0;
    };

public:
    typedef Thread::ThreadID id;

    /** Construct an interruptible thread.

        The name is used for debugger diagnostics.

        @param name The name of the thread.
    */
    explicit InterruptibleThread (String name);

    /** Destroy the interruptible thread.

        This will signal an interrupt and wait until the thread exits.
    */
    ~InterruptibleThread ();

    /** Start the thread.
    */
    void start (EntryPoint* const entryPoint);

    /** Wait for the thread to exit.
    */
    void join ();

    /** Wait for interrupt.
        This call blocks until the thread is interrupted.
        May only be called by the thread of execution.
    */
    void wait ();

    /** Interrupt the thread of execution.

        This can be called from any thread.
    */
    void interrupt ();

    /** Determine if an interruption is requested.

        After the function returns `true`, the interrupt status is cleared.
        Subsequent calls will return `false` until another interrupt is requested.

        May only be called by the thread of execution.

        @see CurrentInterruptibleThread::interruptionPoint

        @return `true` if an interrupt was requested.
    */
    bool interruptionPoint ();

    /** Get the ID of the associated thread.

        @return The ID of the thread.
    */
    id getId () const;

    /** Determine if this is the thread of execution.

        @note The return value is undefined if the thread is not running.

        @return `true` if the caller is this thread of execution.
    */
    bool isTheCurrentThread () const;

    /** Adjust the thread priority.

        @note This only affects some platforms.

        @param priority A number from 0..10
    */
    void setPriority (int priority);

    /** Get the InterruptibleThread for the thread of execution.

        This will return `nullptr` when called from the message thread, or from
        a thread of execution that is not an InterruptibleThread.
    */
    static InterruptibleThread* getCurrentThread ();

    // private
    Thread& peekThread ()
    {
        return m_thread;
    }

private:
    class ThreadHelper : public Thread
    {
    public:
        ThreadHelper (String name, InterruptibleThread* owner);

        InterruptibleThread* getOwner () const;

        void run ();

    private:
        InterruptibleThread* const m_owner;
    };

    void run ();

    ThreadHelper m_thread;
    EntryPoint* m_entryPoint;
    WaitableEvent m_runEvent;
    id m_threadId;

    enum
    {
        stateRun,
        stateInterrupt,
        stateWait
    };

    AtomicState m_state;
};

//------------------------------------------------------------------------------

/** Global operations on the current InterruptibleThread.

    Calling members of the class from a thread of execution which is not an
    InterruptibleThread results in undefined behavior.
*/
class CurrentInterruptibleThread
{
public:
    /** Call the current thread's interrupt point function.
    */
    static bool interruptionPoint ();
};

#endif
