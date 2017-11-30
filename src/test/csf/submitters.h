//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc

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
#ifndef RIPPLE_TEST_CSF_SUBMITTERS_H_INCLUDED
#define RIPPLE_TEST_CSF_SUBMITTERS_H_INCLUDED

#include <test/csf/SimTime.h>
#include <test/csf/Scheduler.h>
#include <test/csf/Peer.h>
#include <test/csf/Tx.h>
#include <type_traits>
namespace ripple {
namespace test {
namespace csf {

// Submitters are classes for simulating submission of transactions to the network

/** Represents rate as a count/duration */
struct Rate
{
    std::size_t count;
    SimDuration duration;

    double
    inv() const
    {
        return duration.count()/double(count);
    }
};

/** Submits transactions to a specified peer

    Submits successive transactions beginning at start, then spaced according
    to succesive calls of distribution(), until stop.

    @tparam Distribution is a `UniformRandomBitGenerator` from the STL that
            is used by random distributions to generate random samples
    @tparam Generator is an object with member

            T operator()(Generator &g)

            which generates the delay T in SimDuration units to the next
            transaction. For the current definition of SimDuration, this is
            currently the number of nanoseconds. Submitter internally casts
            arithmetic T to SimDuration::rep units to allow using standard
            library distributions as a Distribution.
*/
template <class Distribution, class Generator, class Selector>
class Submitter
{
    Distribution dist_;
    SimTime stop_;
    std::uint32_t nextID_ = 0;
    Selector selector_;
    Scheduler & scheduler_;
    Generator & g_;

    // Convert generated durations to SimDuration
    static SimDuration
    asDuration(SimDuration d)
    {
        return d;
    }

    template <class T>
    static
    std::enable_if_t<std::is_arithmetic<T>::value, SimDuration>
    asDuration(T t)
    {
        return SimDuration{static_cast<SimDuration::rep>(t)};
    }

    void
    submit()
    {
        selector_()->submit(Tx{nextID_++});
        if (scheduler_.now() < stop_)
        {
            scheduler_.in(asDuration(dist_(g_)), [&]() { submit(); });
        }
    }

public:
    Submitter(
        Distribution dist,
        SimTime start,
        SimTime end,
        Selector & selector,
        Scheduler & s,
        Generator & g)
        : dist_{dist}, stop_{end}, selector_{selector}, scheduler_{s}, g_{g}
    {
        scheduler_.at(start, [&]() { submit(); });
    }
};

template <class Distribution, class Generator, class Selector>
Submitter<Distribution, Generator, Selector>
makeSubmitter(
    Distribution dist,
    SimTime start,
    SimTime end,
    Selector& sel,
    Scheduler& s,
    Generator& g)
{
    return Submitter<Distribution, Generator, Selector>(
            dist, start ,end, sel, s, g);
}

}  // namespace csf
}  // namespace test
}  // namespace ripple

#endif
