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

#ifndef RIPPLE_BASICS_RANGESET_H_INCLUDED
#define RIPPLE_BASICS_RANGESET_H_INCLUDED

#include <string>
#include <boost/optional.hpp>
#include <boost/icl/closed_interval.hpp>
#include <boost/icl/interval_set.hpp>
#include <boost/serialization/split_free.hpp>

namespace ripple
{

/** A closed interval over the domain T.

    For an instance ClosedInterval c, this represents the closed interval
    (c.first(), c.last()).  A single element interval has c.first() == c.last().

    This is simply a type-alias for boost interval container library interval
    set, so users should consult that documentation for available supporting
    member and free functions.
*/
template <class T>
using ClosedInterval = boost::icl::closed_interval<T>;

/** Create a closed range interval

    Helper function to create a closed range interval without having to qualify
    the template argument.
*/
template <class T>
ClosedInterval<T>
range(T low, T high)
{
    return ClosedInterval<T>(low, high);
}

/** A set of closed intervals over the domain T.

    Represents a set of values of the domain T using the minimum number
    of disjoint ClosedInterval<T>.  This is useful to represent ranges of
    T where a few instances are missing, e.g. the set 1-5,8-9,11-14.

    This is simply a type-alias for boost interval container library interval
    set, so users should consult that documentation for available supporting
    member and free functions.
*/
template <class T>
using RangeSet = boost::icl::interval_set<T, std::less, ClosedInterval<T>>;


/** Convert a ClosedInterval to a styled string

    The styled string is
        "c.first()-c.last()"  if c.first() != c.last()
        "c.first()" if c.first() == c.last()

    @param ci The closed interval to convert
    @return The style string
*/
template <class T>
std::string to_string(ClosedInterval<T> const & ci)
{
    if (ci.first() == ci.last())
        return std::to_string(ci.first());
    return std::to_string(ci.first()) + "-" + std::to_string(ci.last());
}

/** Convert the given RangeSet to a styled string.

    The styled string represention is the set of disjoint intervals joined by
    commas.  The string "empty" is returned if the set is empty.

    @param rs The rangeset to convert
    @return The styled string
*/
template <class T>
std::string to_string(RangeSet<T> const & rs)
{
    using ripple::to_string;

    if (rs.empty())
        return "empty";
    std::string res = "";
    for (auto const & interval : rs)
    {
        if (!res.empty())
            res += ",";
        res += to_string(interval);
    }
    return res;
}

/** Find the largest value not in the set that is less than a given value.

    @param rs The set of interest
    @param t The value that must be larger than the result
    @param minVal (Default is 0) The smallest allowed value
    @return The largest v such that minV <= v < t and !contains(rs, v) or
            boost::none if no such v exists.
*/
template <class T>
boost::optional<T>
prevMissing(RangeSet<T> const & rs, T t, T minVal = 0)
{
    if (rs.empty() || t == minVal)
        return boost::none;
    RangeSet<T> tgt{ ClosedInterval<T>{minVal, t - 1} };
    tgt -= rs;
    if (tgt.empty())
        return boost::none;
    return boost::icl::last(tgt);
}
}  // namespace ripple


// The boost serialization documents recommended putting free-function helpers
// in the boost serialization namespace

namespace boost {
namespace serialization {
template <class Archive, class T>
void
save(Archive& ar,
    ripple::ClosedInterval<T> const& ci,
    const unsigned int version)
{
    auto l = ci.lower();
    auto u = ci.upper();
    ar << l << u;
}

template <class Archive, class T>
void
load(Archive& ar, ripple::ClosedInterval<T>& ci, const unsigned int version)
{
    T low, up;
    ar >> low >> up;
    ci = ripple::ClosedInterval<T>{low, up};
}

template <class Archive, class T>
void
serialize(Archive& ar,
    ripple::ClosedInterval<T>& ci,
    const unsigned int version)
{
    split_free(ar, ci, version);
}

template <class Archive, class T>
void
save(Archive& ar, ripple::RangeSet<T> const& rs, const unsigned int version)
{
    auto s = rs.iterative_size();
    ar << s;
    for (auto const& r : rs)
        ar << r;
}

template <class Archive, class T>
void
load(Archive& ar, ripple::RangeSet<T>& rs, const unsigned int version)
{
    rs.clear();
    std::size_t intervals;
    ar >> intervals;
    for (std::size_t i = 0; i < intervals; ++i)
    {
        ripple::ClosedInterval<T> ci;
        ar >> ci;
        rs.insert(ci);
    }
}

template <class Archive, class T>
void
serialize(Archive& ar, ripple::RangeSet<T>& rs, const unsigned int version)
{
    split_free(ar, rs, version);
}

}  // serialization
}  // boost
#endif
