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

#ifndef RIPPLE_TESTOVERLAY_RESULTS_H_INCLUDED
#define RIPPLE_TESTOVERLAY_RESULTS_H_INCLUDED

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

    beast::String toString () const
    {
        using beast::String;
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
