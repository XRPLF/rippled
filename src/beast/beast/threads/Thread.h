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

#ifndef BEAST_THREADS_THREAD_H_INCLUDED
#define BEAST_THREADS_THREAD_H_INCLUDED

#include <beast/threads/RecursiveMutex.h>
#include <beast/threads/WaitableEvent.h>

#include <string>

namespace beast {

//==============================================================================
/**
    Encapsulates a thread.

    Subclasses derive from Thread and implement the run() method, in which they
    do their business. The thread can then be started with the startThread() method
    and controlled with various other methods.

    @see CriticalSection, WaitableEvent, Process, ThreadWithProgressWindow,
         MessageManagerLock
*/
class Thread
{
public:
    //==============================================================================
    /**
        Creates a thread.

        When first created, the thread is not running. Use the startThread()
        method to start it.
    */
    explicit Thread (std::string const& threadName);

    Thread (Thread const&) = delete;
    Thread& operator= (Thread const&) = delete;
    
    /** Destructor.

        If the thread has not been stopped first, this will generate a fatal error.
    */
    virtual ~Thread();

    //==============================================================================
    /** Must be implemented to perform the thread's actual code.

        Remember that the thread must regularly check the threadShouldExit()
        method whilst running, and if this returns true it should return from
        the run() method as soon as possible to avoid being forcibly killed.

        @see threadShouldExit, startThread
    */
    virtual void run() = 0;

    //==============================================================================
    // Thread control functions..

    /** Starts the thread running.

        This will start the thread's run() method.
        (if it's already started, startThread() won't do anything).

        @see stopThread
    */
    void startThread();

    /** Attempts to stop the thread running.

        This method will cause the threadShouldExit() method to return true
        and call notify() in case the thread is currently waiting.
    */
    void stopThread ();

    /** Stop the thread without blocking.
        This calls signalThreadShouldExit followed by notify.
    */
    void stopThreadAsync ();

    //==============================================================================
    /** Returns true if the thread is currently active */
    bool isThreadRunning() const;

    /** Sets a flag to tell the thread it should stop.

        Calling this means that the threadShouldExit() method will then return true.
        The thread should be regularly checking this to see whether it should exit.

        If your thread makes use of wait(), you might want to call notify() after calling
        this method, to interrupt any waits that might be in progress, and allow it
        to reach a point where it can exit.

        @see threadShouldExit
        @see waitForThreadToExit
    */
    void signalThreadShouldExit();

    /** Checks whether the thread has been told to stop running.

        Threads need to check this regularly, and if it returns true, they should
        return from their run() method at the first possible opportunity.

        @see signalThreadShouldExit
    */
    inline bool threadShouldExit() const { return shouldExit; }

    /** Waits for the thread to stop.

        This will waits until isThreadRunning() is false.
    */
    void waitForThreadToExit () const;

    //==============================================================================
    /** Makes the thread wait for a notification.

        This puts the thread to sleep until either the timeout period expires, or
        another thread calls the notify() method to wake it up.

        A negative time-out value means that the method will wait indefinitely.

        @returns    true if the event has been signalled, false if the timeout expires.
    */
    bool wait (int timeOutMilliseconds = -1) const;

    /** Wakes up the thread.

        If the thread has called the wait() method, this will wake it up.

        @see wait
    */
    void notify() const;

    //==============================================================================
    /** Returns the name of the thread.

        This is the name that gets set in the constructor.
    */
    std::string const& getThreadName() const { return threadName; }

    /** Changes the name of the caller thread.
        Different OSes may place different length or content limits on this name.
    */
    static void setCurrentThreadName (std::string const& newThreadName);


private:
    //==============================================================================
    std::string const threadName;
    void* volatile threadHandle;
    RecursiveMutex startStopLock;
    WaitableEvent startSuspensionEvent, defaultEvent;
    bool volatile shouldExit;

   #ifndef DOXYGEN
    friend void beast_threadEntryPoint (void*);
   #endif

    void launchThread();
    void closeThreadHandle();
    void threadEntryPoint();
};

}

#endif

