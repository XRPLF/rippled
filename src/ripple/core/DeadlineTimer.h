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

#ifndef RIPPLE_CORE_DEADLINETIMER_H_INCLUDED
#define RIPPLE_CORE_DEADLINETIMER_H_INCLUDED

#include <ripple/beast/core/List.h>
#include <chrono>

namespace ripple {

/** Provides periodic or one time notifications at a specified time interval.
*/
class DeadlineTimer
    : public beast::List <DeadlineTimer>::Node
{
public:
    using clock = std::chrono::steady_clock;
    using duration = std::chrono::milliseconds;
    using time_point = std::chrono::time_point<clock, duration>;

    /** Listener for a deadline timer.

        The listener is called on an auxiliary thread. It is suggested
        not to perform any time consuming operations during the call.
    */
    // VFALCO TODO Perhaps allow construction using a ServiceQueue to use
    //             for notifications.
    //
    class Listener
    {
    public:
        virtual void onDeadlineTimer (DeadlineTimer&) = 0;
    };

public:
    /** Create a deadline timer with the specified listener attached.
    */
    explicit DeadlineTimer (Listener* listener);

    DeadlineTimer (DeadlineTimer const&) = delete;
    DeadlineTimer& operator= (DeadlineTimer const&) = delete;

    ~DeadlineTimer ();

    /** Cancel all notifications.
        It is okay to call this on an inactive timer.
        @note It is guaranteed that no notifications will occur after this
              function returns.
    */
    void cancel ();

    /** Set the timer to go off once in the future.
        If the timer is already active, this will reset it.
        @note If the timer is already active, the old one might go off
              before this function returns.
        @param delay duration until the timer will send a notification.
                     This must be greater than zero.
    */
    void setExpiration (duration delay);

    /** Set the timer to go off repeatedly with the specified frequency.
        If the timer is already active, this will reset it.
        @note If the timer is already active, the old one might go off
              before this function returns.
        @param interval duration until the timer will send a notification.
                        This must be greater than zero.
    */
    void setRecurringExpiration (duration interval);

    /** Equality comparison.
        Timers are equal if they have the same address.
    */
    inline bool operator== (DeadlineTimer const& other) const
    {
        return this == &other;
    }

    /** Inequality comparison. */
    inline bool operator!= (DeadlineTimer const& other) const
    {
        return this != &other;
    }

private:
    class Manager;

    Listener* const m_listener;
    bool m_isActive;

    time_point notificationTime_;
    duration recurring_; // > 0ms if recurring.
};

}

#endif
