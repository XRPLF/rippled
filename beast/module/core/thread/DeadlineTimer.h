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

namespace beast
{

/** Provides periodic or one time notifications at a specified time interval.
*/
class DeadlineTimer
    : public List <DeadlineTimer>::Node
    , public Uncopyable
{
public:
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
        virtual void onDeadlineTimer (DeadlineTimer&) { }
    };

public:
    /** Create a deadline timer with the specified listener attached.
    */
    explicit DeadlineTimer (Listener* listener);

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
        @param secondsUntilDeadline The number of seconds until the timer
                                    will send a notification. This must be
                                    greater than zero.
    */
    /** @{ */
    void setExpiration (double secondsUntilDeadline);

    template <class Rep, class Period>
    void setExpiration (std::chrono::duration <Rep, Period> const& amount)
    {
        setExpiration (std::chrono::duration_cast <
            std::chrono::duration <double>> (amount).count ());
    }
    /** @} */

    /** Set the timer to go off repeatedly with the specified frequency.
        If the timer is already active, this will reset it.
        @note If the timer is already active, the old one might go off
              before this function returns.
        @param secondsUntilDeadline The number of seconds until the timer
                                    will send a notification. This must be
                                    greater than zero.
    */
    void setRecurringExpiration (double secondsUntilDeadline);

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
    SharedPtr <SharedSingleton <Manager> > m_manager;
    bool m_isActive;
    RelativeTime m_notificationTime;
    double m_secondsRecurring; // non zero if recurring
};

} // beast

#endif
