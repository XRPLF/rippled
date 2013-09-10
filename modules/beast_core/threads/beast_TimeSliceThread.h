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

#ifndef BEAST_TIMESLICETHREAD_H_INCLUDED
#define BEAST_TIMESLICETHREAD_H_INCLUDED

class TimeSliceThread;

//==============================================================================
/**
    Used by the TimeSliceThread class.

    To register your class with a TimeSliceThread, derive from this class and
    use the TimeSliceThread::addTimeSliceClient() method to add it to the list.

    Make sure you always call TimeSliceThread::removeTimeSliceClient() before
    deleting your client!

    @see TimeSliceThread
*/
class BEAST_API TimeSliceClient
{
public:
    /** Destructor. */
    virtual ~TimeSliceClient()   {}

    /** Called back by a TimeSliceThread.

        When you register this class with it, a TimeSliceThread will repeatedly call
        this method.

        The implementation of this method should use its time-slice to do something that's
        quick - never block for longer than absolutely necessary.

        @returns    Your method should return the number of milliseconds which it would like to wait before being called
                    again. Returning 0 will make the thread call again as soon as possible (after possibly servicing
                    other busy clients). If you return a value below zero, your client will be removed from the list of clients,
                    and won't be called again. The value you specify isn't a guaranteee, and is only used as a hint by the
                    thread - the actual time before the next callback may be more or less than specified.
                    You can force the TimeSliceThread to wake up and poll again immediately by calling its notify() method.
    */
    virtual int useTimeSlice() = 0;


private:
    friend class TimeSliceThread;
    Time nextCallTime;
};


//==============================================================================
/**
    A thread that keeps a list of clients, and calls each one in turn, giving them
    all a chance to run some sort of short task.

    @see TimeSliceClient, Thread
*/
class BEAST_API TimeSliceThread
    : public Thread 
    , LeakChecked <TimeSliceThread>
{
public:
    //==============================================================================
    /**
        Creates a TimeSliceThread.

        When first created, the thread is not running. Use the startThread()
        method to start it.
    */
    explicit TimeSliceThread (const String& threadName);

    /** Destructor.

        Deleting a Thread object that is running will only give the thread a
        brief opportunity to stop itself cleanly, so it's recommended that you
        should always call stopThread() with a decent timeout before deleting,
        to avoid the thread being forcibly killed (which is a Bad Thing).
    */
    ~TimeSliceThread();

    //==============================================================================
    /** Adds a client to the list.

        The client's callbacks will start after the number of milliseconds specified
        by millisecondsBeforeStarting (and this may happen before this method has returned).
    */
    void addTimeSliceClient (TimeSliceClient* client, int millisecondsBeforeStarting = 0);

    /** Removes a client from the list.

        This method will make sure that all callbacks to the client have completely
        finished before the method returns.
    */
    void removeTimeSliceClient (TimeSliceClient* client);

    /** If the given client is waiting in the queue, it will be moved to the front
        and given a time-slice as soon as possible.
        If the specified client has not been added, nothing will happen.
    */
    void moveToFrontOfQueue (TimeSliceClient* client);

    /** Returns the number of registered clients. */
    int getNumClients() const;

    /** Returns one of the registered clients. */
    TimeSliceClient* getClient (int index) const;

    //==============================================================================
   #ifndef DOXYGEN
    void run() override;
   #endif

    //==============================================================================
private:
    CriticalSection callbackLock, listLock;
    Array <TimeSliceClient*> clients;
    TimeSliceClient* clientBeingCalled;

    TimeSliceClient* getNextClient (int index) const;
};


#endif   // BEAST_TIMESLICETHREAD_H_INCLUDED
