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

#ifndef BEAST_CHRONO_CPUMETER_H_INCLUDED
#define BEAST_CHRONO_CPUMETER_H_INCLUDED

#include "RelativeTime.h"
#include "ScopedTimeInterval.h"
#include "../thread/SharedData.h"
#include "../Atomic.h"

namespace beast {

/** Measurements of CPU utilization. */
class CPUMeter
{
private:
    struct MeasureIdle
    {
        explicit MeasureIdle (CPUMeter& meter)
            : m_meter (&meter)
            { }
        void operator() (RelativeTime const& interval) const
            { m_meter->addIdleTime (interval); }
        CPUMeter* m_meter;
    };

    struct MeasureActive
    {
        explicit MeasureActive (CPUMeter& meter)
            : m_meter (&meter)
            { }
        void operator() (RelativeTime const& interval) const
            { m_meter->addActiveTime (interval); }
        CPUMeter* m_meter;
    };

public:
    /** The type of container that measures idle time. */
    typedef ScopedTimeInterval <MeasureIdle>    ScopedIdleTime;
    typedef ScopedTimeInterval <MeasureActive>  ScopedActiveTime;

    /** Returns the fraction of time that the CPU is being used. */
    double getCpuUsage () const
    {
        SharedState::ConstAccess state (m_state);
        double const seconds (state->usage.seconds());
        if (seconds > 0)
            return (state->usage.active.inSeconds() / seconds);
        return 0;
    }

private:
    enum
    {
        // The amount of time an aggregate must accrue before a swap
        secondsPerAggregate     = 3

        // The number of aggregates in the rolling history buffer
        ,numberOfAggregates     = 20
    };

    // Aggregated sample data
    struct Aggregate
    {
        RelativeTime idle;
        RelativeTime active;

        // Returns the total number of seconds in the aggregate
        double seconds () const
            { return idle.inSeconds() + active.inSeconds(); }

        // Reset the accumulated times
        void clear ()
            { idle = RelativeTime (0); active = RelativeTime (0); }

        Aggregate& operator+= (Aggregate const& other)
            { idle += other.idle; active += other.active; return *this; }

        Aggregate& operator-= (Aggregate const& other)
            { idle -= other.idle; active -= other.active; return *this; }
    };

    struct State
    {
        State () : index (0)
        {
        }

        // Returns a reference to the current aggregate
        Aggregate& front ()
        {
            return history [index];
        }

        // Checks the current aggregate to see if we should advance
        void update()
        {
            if (front().seconds() >= secondsPerAggregate)
                advance();
        }

        // Advance the index in the rolling history
        void advance ()
        {
            usage += history [index];
            index = (index+1) % numberOfAggregates;
            usage -= history [index];
            history [index].clear ();
        }

        // Index of the current aggregate we are accumulating
        int index;

        // Delta summed usage over the entire history buffer
        Aggregate usage;

        // The rolling history buffer
        Aggregate history [numberOfAggregates];
    };

    typedef SharedData <State> SharedState;

    SharedState m_state;

    void addIdleTime (RelativeTime const& interval)
    {
        SharedState::Access state (m_state);
        state->front().idle += interval;
        state->update();
    }

    void addActiveTime (RelativeTime const& interval)
    {
        SharedState::Access state (m_state);
        state->front().active += interval;
        state->update();
    }
};

}

#endif
