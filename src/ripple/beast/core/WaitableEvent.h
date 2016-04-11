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

#ifndef BEAST_THREADS_WAITABLEEVENT_H_INCLUDED
#define BEAST_THREADS_WAITABLEEVENT_H_INCLUDED

#include <ripple/beast/core/Config.h>

#if ! BEAST_WINDOWS
#include <pthread.h>
#endif

namespace beast {

/** Allows threads to wait for events triggered by other threads.
    A thread can call wait() on a WaitableEvent, and this will suspend the
    calling thread until another thread wakes it up by calling the signal()
    method.
*/
class WaitableEvent
{
public:
    //==============================================================================
    /** Creates a WaitableEvent object.

        @param manualReset  If this is false, the event will be reset automatically when the wait()
                            method is called. If manualReset is true, then once the event is signalled,
                            the only way to reset it will be by calling the reset() method.

        @param initiallySignaled If this is true then the event will be signaled when
                                 the constructor returns.
    */
    explicit WaitableEvent (bool manualReset = false, bool initiallySignaled = false);

    /** Destructor.

        If other threads are waiting on this object when it gets deleted, this
        can cause nasty errors, so be careful!
    */
    ~WaitableEvent();

    WaitableEvent (WaitableEvent const&) = delete;
    WaitableEvent& operator= (WaitableEvent const&) = delete;

    //==============================================================================
    /** Suspends the calling thread until the event has been signalled.

        This will wait until the object's signal() method is called by another thread,
        or until the timeout expires.

        After the event has been signalled, this method will return true and if manualReset
        was set to false in the WaitableEvent's constructor, then the event will be reset.

        @param timeOutMilliseconds  the maximum time to wait, in milliseconds. A negative
                                    value will cause it to wait forever.

        @returns    true if the object has been signalled, false if the timeout expires first.
        @see signal, reset
    */
    /** @{ */
    bool wait () const;                             // wait forever
    // VFALCO TODO Change wait() to seconds instead of millis
    bool wait (int timeOutMilliseconds) const; // DEPRECATED
    /** @} */

    //==============================================================================
    /** Wakes up any threads that are currently waiting on this object.

        If signal() is called when nothing is waiting, the next thread to call wait()
        will return immediately and reset the signal.

        If the WaitableEvent is manual reset, all current and future threads that wait upon this
        object will be woken, until reset() is explicitly called.

        If the WaitableEvent is automatic reset, and one or more threads is waiting upon the object,
        then one of them will be woken up. If no threads are currently waiting, then the next thread
        to call wait() will be woken up. As soon as a thread is woken, the signal is automatically
        reset.

        @see wait, reset
    */
    void signal() const;

    //==============================================================================
    /** Resets the event to an unsignalled state.

        If it's not already signalled, this does nothing.
    */
    void reset() const;

private:
#if BEAST_WINDOWS
    void* handle;
#else
    mutable pthread_cond_t condition;
    mutable pthread_mutex_t mutex;
    mutable bool triggered;
    mutable bool manualReset;
#endif
};

}

#endif
