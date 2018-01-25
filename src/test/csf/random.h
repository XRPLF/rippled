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

#ifndef RIPPLE_TEST_CSF_RANDOM_H_INCLUDED
#define RIPPLE_TEST_CSF_RANDOM_H_INCLUDED

#include <random>
#include <vector>

namespace ripple {
namespace test {
namespace csf {

/** Return a randomly shuffled copy of vector based on weights w.

    @param v  The set of values
    @param w  The set of weights of each value
    @param g  A pseudo-random number generator
    @return A vector with entries randomly sampled without replacement
            from the original vector based on the provided weights.
            I.e.  res[0] comes from sample v[i] with weight w[i]/sum_k w[k]
*/
template <class T, class G>
std::vector<T>
random_weighted_shuffle(std::vector<T> v, std::vector<double> w, G& g)
{
    using std::swap;

    for (int i = 0; i < v.size() - 1; ++i)
    {
        // pick a random item weighted by w
        std::discrete_distribution<> dd(w.begin() + i, w.end());
        auto idx = dd(g);
        std::swap(v[i], v[idx]);
        std::swap(w[i], w[idx]);
    }
    return v;
}

/** Generate a vector of random samples

    @param size the size of the sample
    @param dist the distribution to sample
    @param g the pseudo-random number generator

    @return vector of samples
*/
template <class RandomNumberDistribution, class Generator>
std::vector<typename RandomNumberDistribution::result_type>
sample( std::size_t size, RandomNumberDistribution dist, Generator& g)
{
    std::vector<typename RandomNumberDistribution::result_type> res(size);
    std::generate(res.begin(), res.end(), [&dist, &g]() { return dist(g); });
    return res;
}

/** Invocable that returns random samples from a range according to a discrete
    distribution

    Given a pair of random access iterators begin and end, each call to the
    instance of Selector returns a random entry in the range (begin,end)
    according to the weights provided at construction.
*/
template <class RAIter, class Generator>
class Selector
{
    RAIter first_, last_;
    std::discrete_distribution<> dd_;
    Generator g_;

public:
    /** Constructor
        @param first Random access iterator to the start of the range
        @param last Random access iterator to the end of the range
        @param w Vector of weights of size list-first
        @param g the pseudo-random number generator
    */
    Selector(RAIter first, RAIter last, std::vector<double> const& w,
            Generator& g)
      : first_{first}, last_{last}, dd_{w.begin(), w.end()}, g_{g}
    {
        using tag = typename std::iterator_traits<RAIter>::iterator_category;
        static_assert(
                std::is_same<tag, std::random_access_iterator_tag>::value,
                "Selector only supports random access iterators.");
        // TODO: Allow for forward iterators
    }

    typename std::iterator_traits<RAIter>::value_type
    operator()()
    {
        auto idx = dd_(g_);
        return *(first_ + idx);
    }
};

template <typename Iter, typename Generator>
Selector<Iter,Generator>
makeSelector(Iter first, Iter last, std::vector<double> const& w, Generator& g)
{
    return Selector<Iter, Generator>(first, last, w, g);
}

//------------------------------------------------------------------------------
// Additional distributions of interest not defined in in <random>

/** Constant "distribution" that always returns the same value
*/
class ConstantDistribution
{
    double t_;

public:
    ConstantDistribution(double const& t) : t_{t}
    {
    }

    template <class Generator>
    inline double
    operator()(Generator& )
    {
        return t_;
    }
};

/** Power-law distribution with PDF

        P(x) = (x/xmin)^-a

    for a >= 1 and xmin >= 1
 */
class PowerLawDistribution
{
    double xmin_;
    double a_;
    double inv_;
    std::uniform_real_distribution<double> uf_{0, 1};

public:

    using result_type = double;

    PowerLawDistribution(double xmin, double a) : xmin_{xmin}, a_{a}
    {
        inv_ = 1.0 / (1.0 - a_);
    }

    template <class Generator>
    inline double
    operator()(Generator& g)
    {
        // use inverse transform of CDF to sample
        // CDF is P(X <= x): 1 - (x/xmin)^(1-a)
        return xmin_ * std::pow(1 - uf_(g), inv_);
    }
};

}  // csf
}  // test
}  // ripple

#endif
