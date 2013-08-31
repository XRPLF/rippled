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

#ifndef BEAST_THREADWITHCALLQUEUE_H_INCLUDED
#define BEAST_THREADWITHCALLQUEUE_H_INCLUDED

/** An InterruptibleThread with a CallQueue.

    This combines an InterruptibleThread with a CallQueue, allowing functors to
    be queued for asynchronous execution on the thread.

    The thread runs an optional user-defined idle function, which must regularly
    check for an interruption using the InterruptibleThread interface. When an
    interruption is signaled, the idle function returns and the CallQueue is
    synchronized. Then, the idle function is resumed.

    When the ThreadWithCallQueue first starts up, an optional user-defined
    initialization function is executed on the thread. When the thread exits,
    a user-defined exit function may be executed on the thread.

    @see CallQueue

    @ingroup beast_concurrent
*/
class BEAST_API ThreadWithCallQueue
    : public CallQueue
    , private InterruptibleThread::EntryPoint
    , LeakChecked <ThreadWithCallQueue>
{
public:
    /** Entry points for a ThreadWithCallQueue.
    */
    class EntryPoints
    {
    public:
        virtual ~EntryPoints () { }

        virtual void threadInit () { }

        virtual void threadExit () { }

        virtual bool threadIdle ()
        {
            bool interrupted = false;

            return interrupted;
        }
    };

    /** Create a thread.

        @param name The name of the InterruptibleThread and CallQueue, used
                    for diagnostics when debugging.
    */
    explicit ThreadWithCallQueue (String name);

    /** Retrieve the default entry points.

        The default entry points do nothing.
    */
    static EntryPoints* getDefaultEntryPoints () noexcept;

    /** Destroy a ThreadWithCallQueue.

        If the thread is still running it is stopped. The destructor blocks
        until the thread exits cleanly.
    */
    ~ThreadWithCallQueue ();

    /** Start the thread, with optional entry points.

        If `entryPoints` is specified then the thread runs using those
        entry points. If ommitted, the default entry simply do nothing.
        This is useful for creating a thread whose sole activities are
        performed through the call queue.

        @param entryPoints An optional pointer to @ref EntryPoints.
    */
    void start (EntryPoints* const entryPoints = getDefaultEntryPoints ());

    /* Stop the thread.

       Stops the thread and optionally wait until it exits. It is safe to call
       this function at any time and as many times as desired.

       After a call to stop () the CallQueue is closed, and attempts to queue new
       functors will throw a runtime exception. Existing functors will still
       execute.

       Any listeners registered on the CallQueue need to be removed
       before stop is called

       @invariant The caller is not on the associated thread.

       @param wait `true` if the function should wait until the thread exits
                   before returning.
    */

    void stop (bool const wait);

    /** Determine if the thread needs interruption.

        Should be called periodically by the idle function. If interruptionPoint
        returns true or throws, it must not be called again until the idle function
        returns and is re-entered.

        @invariant No previous calls to interruptionPoint() made after the idle
                   function entry point returned `true`.

        @return `false` if the idle function may continue, or `true` if the
                idle function must return as soon as possible.
    */
    bool interruptionPoint ();

    /* Interrupts the idle function.
    */
    void interrupt ();

private:
    static void doNothing ();

    void signal ();

    void reset ();

    void doStop ();

    void threadRun ();

private:
    InterruptibleThread m_thread;
    EntryPoints* m_entryPoints;
    bool m_calledStart;
    bool m_calledStop;
    bool m_shouldStop;
    CriticalSection m_mutex;
};

#endif
