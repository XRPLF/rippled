//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2014, Howard Hinnant <howard.hinnant@gmail.com>,
        Vinnie Falco <vinnie.falco@gmail.com

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

#ifndef BEAST_HASH_HASH_METRICS_H_INCLUDED
#define BEAST_HASH_HASH_METRICS_H_INCLUDED

#include <algorithm>
#include <cmath>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

namespace beast {
namespace hash_metrics {

// Metrics for measuring the quality of container hash functions

/** Returns the fraction of duplicate items in the sequence. */
template <class FwdIter>
float
collision_factor (FwdIter first, FwdIter last)
{
    std::set <typename FwdIter::value_type> s (first, last);
    return 1 - static_cast <float>(s.size()) / std::distance (first, last);
}

//------------------------------------------------------------------------------

/** Returns the deviation of the sequence from the ideal distribution. */
template <class FwdIter>
float
distribution_factor (FwdIter first, FwdIter last)
{
    typedef typename FwdIter::value_type value_type;
    static_assert (std::is_unsigned <value_type>::value, "");

    const unsigned nbits = CHAR_BIT * sizeof(std::size_t);
    const unsigned rows = nbits / 4;
    unsigned counts[rows][16] = {};
    std::for_each (first, last, [&](typename FwdIter::value_type h)
    {
        std::size_t mask = 0xF;
        for (unsigned i = 0; i < rows; ++i, mask <<= 4)
            counts[i][(h & mask) >> 4*i] += 1;
    });
    float mean_rows[rows] = {0};
    float mean_cols[16] = {0};
    for (unsigned i = 0; i < rows; ++i)
    {
        for (unsigned j = 0; j < 16; ++j)
        {
            mean_rows[i] += counts[i][j];
            mean_cols[j] += counts[i][j];
        }
    }
    for (unsigned i = 0; i < rows; ++i)
        mean_rows[i] /= 16;
    for (unsigned j = 0; j < 16; ++j)
        mean_cols[j] /= rows;
    std::pair<float, float> dev[rows][16];
    for (unsigned i = 0; i < rows; ++i)
    {
        for (unsigned j = 0; j < 16; ++j)
        {
            dev[i][j].first = std::abs(counts[i][j] - mean_rows[i]) / mean_rows[i];
            dev[i][j].second = std::abs(counts[i][j] - mean_cols[j]) / mean_cols[j];
        }
    }
    float max_err = 0;
    for (unsigned i = 0; i < rows; ++i)
    {
        for (unsigned j = 0; j < 16; ++j)
        {
            if (max_err < dev[i][j].first)
                max_err = dev[i][j].first;
            if (max_err < dev[i][j].second)
                max_err = dev[i][j].second;
        }
    }
    return max_err;
}

//------------------------------------------------------------------------------

namespace detail {

template <class T>
inline
T
sqr(T t)
{
    return t*t;
}

double
score (int const* bins, std::size_t const bincount, double const k)
{
    double const n = bincount;
    // compute rms^2 value
    double rms_sq = 0;
    for(std::size_t i = 0; i < bincount; ++i)
        rms_sq += sqr(bins[i]);;
    rms_sq /= n;
    // compute fill factor
    double const f = (sqr(k) - 1) / (n*rms_sq - k);
    // rescale to (0,1) with 0 = good, 1 = bad
    return 1 - (f / n);
}

template <class T>
std::uint32_t
window (T* blob, int start, int count )
{
    std::size_t const len = sizeof(T);
    static_assert((len & 3) == 0, "");
    if(count == 0)
        return 0;
    int const nbits = len * CHAR_BIT;
    start %= nbits;
    int ndwords = len / 4;
    std::uint32_t const* k = static_cast <
        std::uint32_t const*>(static_cast<void const*>(blob));
    int c = start & (32-1);
    int d = start / 32;
    if(c == 0)
        return (k[d] & ((1 << count) - 1));
    int ia = (d + 1) % ndwords;
    int ib = (d + 0) % ndwords;
    std::uint32_t a = k[ia];
    std::uint32_t b = k[ib];
    std::uint32_t t = (a << (32-c)) | (b >> c);
    t &= ((1 << count)-1);
    return t;
}

} // detail

/** Calculated a windowed metric using bins.
    TODO Need reference (SMHasher?)
*/
template <class FwdIter>
double
windowed_score (FwdIter first, FwdIter last)
{
    auto const size (std::distance (first, last));
    int maxwidth = 20;
    // We need at least 5 keys per bin to reliably test distribution biases
    // down to 1%, so don't bother to test sparser distributions than that
    while (static_cast<double>(size) / (1 << maxwidth) < 5.0)
        maxwidth--;
    double worst = 0;
    std::vector <int> bins (1 << maxwidth);
    int const hashbits = sizeof(std::size_t) * CHAR_BIT;
    for (int start = 0; start < hashbits; ++start)
    {
        int width = maxwidth;
        bins.assign (1 << width, 0);
        for (auto iter (first); iter != last; ++iter)
            ++bins[detail::window(&*iter, start, width)];
        // Test the distribution, then fold the bins in half,
        // repeat until we're down to 256 bins
        while (bins.size() >= 256)
        {
            double score (detail::score (
                bins.data(), bins.size(), size));
            worst = std::max(score, worst);
            if (--width < 8)
                break;
            for (std::size_t i = 0, j = bins.size() / 2; j < bins.size(); ++i, ++j)
                bins[i] += bins[j];
            bins.resize(bins.size() / 2);
        }
    }
    return worst;
}

} // hash_metrics
} // beast

#endif
