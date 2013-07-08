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

#ifndef BEAST_DEADLINETIMER_H_INCLUDED
#define BEAST_DEADLINETIMER_H_INCLUDED

/** Provides periodic or one time notifications at a specified time interval.
*/
class DeadlineTimer : public List <DeadlineTimer>::Node
{
public:
    /** Listener for a deadline timer.

        The listener is called on an auxiliary thread. It is suggested
        not to perform any time consuming operations during the call.
    */
    // VFALCO TODO Allow construction with a specific ThreadWithCallQueue&
    //             on which to notify the listener.
    class Listener
    {
    public:
        virtual void onDeadlineTimer () { }
    };

public:
    /** Create a deadline timer with the specified listener attached.
    */
    explicit DeadlineTimer (Listener* listener);

    ~DeadlineTimer ();

    /** Set the timer to go off once in the future.
    */
    void setExpiration (double secondsUntilDeadline);

    /** Set the timer to go off repeatedly with the specified frequency.
    */
    void setRecurringExpiration (double secondsUntilDeadline);

    /** Set the timer to go off at a specific time.

        @note If the time is in the past, the timer will go off
              immediately.
    */
    void setExpirationTime (Time absoluteDeadline);

    /** Reset the timer so that no more notifications are sent.
    */
    void reset ();

private:
    class Manager;

    Listener* const m_listener;
    ReferenceCountedObjectPtr <Manager> m_manager;
    bool m_isActive;
    Time m_notificationTime;
    double m_secondsRecurring; // non zero if recurring
};

#endif
