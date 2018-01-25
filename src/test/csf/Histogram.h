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
#ifndef RIPPLE_TEST_CSF_HISTOGRAM_H_INCLUDED
#define RIPPLE_TEST_CSF_HISTOGRAM_H_INCLUDED

#include <map>
#include <chrono>
#include <algorithm>

namespace ripple {
namespace test {
namespace csf {

/** Basic histogram.

    Histogram for a type `T` that satisfies
      - Default construction: T{}
      - Comparison : T a, b;  bool res = a < b
      - Addition: T a, b; T c = a + b;
      - Multiplication : T a, std::size_t b; T c = a * b;
      - Divison: T a; std::size_t b;  T c = a/b;


*/
template <class T, class Compare = std::less<T>>
class Histogram
{
    // TODO: Consider logarithimic bins around expected median if this becomes
    // unscaleable
    std::map<T, std::size_t, Compare> counts_;
    std::size_t samples = 0;
public:
    /** Insert an sample */
    void
    insert(T const & s)
    {
        ++counts_[s];
        ++samples;
    }

    /** The number of samples */
    std::size_t
    size() const
    {
        return samples;
    }

    /** The number of distinct samples (bins) */
    std::size_t
    numBins() const
    {
        return counts_.size();
    }

    /** Minimum observed value */
    T
    minValue() const
    {
        return counts_.empty() ? T{} : counts_.begin()->first;
    }

    /** Maximum observed value */
    T
    maxValue() const
    {
        return counts_.empty() ? T{} : counts_.rbegin()->first;
    }

    /** Histogram average */
    T
    avg() const
    {
        T tmp{};
        if(samples == 0)
            return tmp;
        // Since counts are sorted, shouldn't need to worry much about numerical
        // error
        for (auto const& it : counts_)
        {
            tmp += it.first * it.second;
        }
        return tmp/samples;
    }

    /** Calculate the given percentile of the distribution.

        @param p Percentile between 0 and 1, e.g. 0.50 is 50-th percentile
                 If the percentile falls between two bins, uses the nearest bin.
        @return The given percentile of the distribution
    */
    T
    percentile(float p) const
    {
        assert(p >= 0 && p <=1);
        std::size_t pos = std::round(p * samples);

        if(counts_.empty())
            return T{};

        auto it = counts_.begin();
        std::size_t cumsum = it->second;
        while (it != counts_.end() && cumsum < pos)
        {
            ++it;
            cumsum += it->second;
        }
        return it->first;
    }
};

}  // namespace csf
}  // namespace test
}  // namespace ripple

#endif
