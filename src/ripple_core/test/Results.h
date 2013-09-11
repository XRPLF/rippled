//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CORE_TEST_RESULTS_H_INCLUDED
#define RIPPLE_CORE_TEST_RESULTS_H_INCLUDED

namespace TestOverlay
{

/** Accumulates statistics on one or more simulation steps. */
struct Results
{
    Results ()
        : steps (0)
        , sent (0)
        , received (0)
        , dropped (0)
    {
    }

    Results (Results const& other)
        : steps (other.steps)
        , sent (other.sent)
        , received (other.received)
        , dropped (other.dropped)
    {
    }

    Results& operator= (Results const& other)
    {
        steps = other.steps;
        sent = other.sent;
        received = other.received;
        dropped = other.dropped;
        return *this;
    }

    String toString () const
    {
        String s;
        s =   "steps(" + String::fromNumber (steps) + ")"
            + ", sent(" + String::fromNumber (sent) + ")"
            + ", received(" + String::fromNumber (received) + ")"
            + ", dropped(" + String::fromNumber (dropped) + ")";
        return s;
    }

    Results& operator+= (Results const& other)
    {
        steps += other.steps;
        sent += other.sent;
        received += other.received;
        dropped += other.dropped;
        return *this;
    }

    Results operator+ (Results const& other)
    {
        Results results;
        results.steps =    steps + other.steps;
        results.sent =     sent + other.sent;
        results.received = received + other.received;
        results.dropped =  dropped + other.dropped;
        return results;
    }

    int steps;
    int sent;
    int received;
    int dropped;
};

}

#endif
