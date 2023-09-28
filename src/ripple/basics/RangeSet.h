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

#include <ripple/beast/core/LexicalCast.h>

#include <boost/algorithm/string.hpp>
#include <boost/icl/closed_interval.hpp>
#include <boost/icl/interval_set.hpp>

#include <optional>
#include <string>
#include <vector>

namespace ripple {

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
std::string
to_string(ClosedInterval<T> const& ci)
{
    if (ci.first() == ci.last())
        return std::to_string(ci.first());
    return std::to_string(ci.first()) + "-" + std::to_string(ci.last());
}

/** Convert the given RangeSet to a styled string.

    The styled string representation is the set of disjoint intervals joined
    by commas.  The string "empty" is returned if the set is empty.

    @param rs The rangeset to convert
    @return The styled string
*/
template <class T>
std::string
to_string(RangeSet<T> const& rs)
{
    if (rs.empty())
        return "empty";

    std::string s;
    for (auto const& interval : rs)
        s += ripple::to_string(interval) + ",";
    s.pop_back();

    return s;
}

/** Convert the given styled string to a RangeSet.

    The styled string representation is the set
    of disjoint intervals joined by commas.

    @param rs The set to be populated
    @param s The styled string to convert
    @return True on successfully converting styled string
*/
template <class T>
[[nodiscard]] bool
from_string(RangeSet<T>& rs, std::string const& s)
{
    std::vector<std::string> intervals;
    std::vector<std::string> tokens;
    bool result{true};

    rs.clear();
    boost::split(tokens, s, boost::algorithm::is_any_of(","));
    for (auto const& t : tokens)
    {
        boost::split(intervals, t, boost::algorithm::is_any_of("-"));
        switch (intervals.size())
        {
            case 1: {
                T front;
                if (!beast::lexicalCastChecked(front, intervals.front()))
                    result = false;
                else
                    rs.insert(front);
                break;
            }
            case 2: {
                T front;
                if (!beast::lexicalCastChecked(front, intervals.front()))
                    result = false;
                else
                {
                    T back;
                    if (!beast::lexicalCastChecked(back, intervals.back()))
                        result = false;
                    else
                        rs.insert(range(front, back));
                }
                break;
            }
            default:
                result = false;
        }

        if (!result)
            break;
        intervals.clear();
    }

    if (!result)
        rs.clear();
    return result;
}

/** Find the largest value not in the set that is less than a given value.

    @param rs The set of interest
    @param t The value that must be larger than the result
    @param minVal (Default is 0) The smallest allowed value
    @return The largest v such that minV <= v < t and !contains(rs, v) or
            std::nullopt if no such v exists.
*/
template <class T>
std::optional<T>
prevMissing(RangeSet<T> const& rs, T t, T minVal = 0)
{
    if (rs.empty() || t == minVal)
        return std::nullopt;
    RangeSet<T> tgt{ClosedInterval<T>{minVal, t - 1}};
    tgt -= rs;
    if (tgt.empty())
        return std::nullopt;
    return boost::icl::last(tgt);
}

}  // namespace ripple

#endif
