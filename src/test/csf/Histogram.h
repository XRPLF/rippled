#ifndef XRPL_TEST_CSF_HISTOGRAM_H_INCLUDED
#define XRPL_TEST_CSF_HISTOGRAM_H_INCLUDED

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <map>

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
    insert(T const& s)
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
        if (samples == 0)
            return tmp;
        // Since counts are sorted, shouldn't need to worry much about numerical
        // error
        for (auto const& [bin, count] : counts_)
        {
            tmp += bin * count;
        }
        return tmp / samples;
    }

    /** Calculate the given percentile of the distribution.

        @param p Percentile between 0 and 1, e.g. 0.50 is 50-th percentile
                 If the percentile falls between two bins, uses the nearest bin.
        @return The given percentile of the distribution
    */
    T
    percentile(float p) const
    {
        assert(p >= 0 && p <= 1);
        std::size_t pos = std::round(p * samples);

        if (counts_.empty())
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
