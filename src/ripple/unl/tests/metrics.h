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

#ifndef RIPPLE_SIM_METRICS_H_INCLUDED
#define RIPPLE_SIM_METRICS_H_INCLUDED

#include <iomanip>
#include <sstream>
#include <string>

namespace ripple {
namespace test {

template <class FwdRange>
std::string
seq_string (FwdRange const& r, int width = 0)
{
    std::stringstream ss;
    auto iter = std::begin(r);
    if (iter == std::end(r))
        return ss.str();
    ss << std::setw(width) << *iter++;
    while(iter != std::end(r))
        ss << ", " <<
            std::setw(width) << *iter++;
    return ss.str();
}

template <class FwdRange>
typename FwdRange::value_type
seq_sum (FwdRange const& r)
{
    typename FwdRange::value_type sum = 0;
    for (auto const& n : r)
        sum += n;
    return sum;
}

template <class RanRange>
double
diameter (RanRange const& r)
{
    if (r.empty())
        return 0;
    if (r.size() == 1)
        return r.front();
    auto h0 = *(r.end() - 2);
    auto h1 = r.back();
    return (r.size() - 2) +
        double(h1) / (h0 + h1);
}

template <class Container>
typename Container::value_type&
nth (Container& c, std::size_t n)
{
    c.resize(std::max(c.size(), n + 1));
    return c[n];
}

template <class Hist, class FwdRange>
void
hist_accum (Hist& h, FwdRange const& r)
{
    for(auto const& v : r)
        ++nth(h, v);
}

//------------------------------------------------------------------------------

template <class = void>
inline
std::string
pad (std::string s, std::size_t n)
{
    if (s.size() < n)
        s.insert(0, n - s.size(), ' ');
    return s;
}

} // test
} // ripple

#endif
