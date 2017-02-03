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

#ifndef RIPPLE_TEST_CSF_UNL_H_INCLUDED
#define RIPPLE_TEST_CSF_UNL_H_INCLUDED

#include <boost/container/flat_set.hpp>
#include <boost/optional.hpp>
#include <chrono>
#include <numeric>
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

//< Unique identifier for each node in the network
using PeerID = std::uint32_t;

//< A unique node list defines a set of trusted peers used in consensus
using UNL = boost::container::flat_set<PeerID>;

/** Trust graph defining the consensus simulation

    Trust is a directed relationship from a node i to node j.
    If node i trusts node j, then node i has node j in its UNL.

    Note that each node implicitly trusts itself but that need not be
    explicitly modeled, e.g. UNLS[assignment
*/
class TrustGraph
{
    //< Unique UNLs for the network
    std::vector<UNL> UNLs_;

    std::vector<int> assignment_;

public:
    //< Constructor
    TrustGraph(std::vector<UNL> UNLs, std::vector<int> assignment)
        : UNLs_{UNLs}, assignment_{assignment}
    {
    }

    //< Whether node `i` trusts node `j`
    inline bool
    trusts(PeerID i, PeerID j) const
    {
        return unl(i).find(j) != unl(i).end();
    }

    //< Get the UNL for node `i`
    inline UNL const&
    unl(PeerID i) const
    {
        return UNLs_[assignment_[i]];
    }

    //< Check whether this trust graph satisfies the no forking condition
    bool
    canFork(double quorum) const;

    auto
    numPeers() const
    {
        return assignment_.size();
    }

    //< Save grapviz dot file reprentation of the trust graph
    void
    save_dot(std::string const& fileName);

    /** Generate a random trust graph based on random ranking of peers

        Generate a random trust graph by

            1. Randomly ranking the peers acording to RankPDF
            2. Generating `numUNL` random UNLs by sampling without replacement
               from the ranked nodes.
            3. Restricting the size of the random UNLs according to SizePDF

        @param size The number of nodes in the trust graph
        @param numUNLs The number of UNLs to create
        @param rankPDF Generates random positive real numbers to use as ranks
        @param unlSizePDF Generates random integeres between (0,size-1) to
                          restrict the size of generated PDF
        @param Generator The uniform random bit generator to use

        @note RankPDF/SizePDF can model the full RandomDistribution concept
              defined in the STL, but for the purposes of this function need
              only provide:

                   auto operator()(Generator & g)

              which should return the random sample.


    */
    template <class RankPDF, class SizePDF, class Generator>
    static TrustGraph
    makeRandomRanked(
        int size,
        int numUNLs,
        RankPDF rankPDF,
        SizePDF unlSizePDF,
        Generator& g)
    {
        // 1. Generate ranks
        std::vector<double> weights(size);
        std::generate(
            weights.begin(), weights.end(), [&]() { return rankPDF(g); });

        // 2. Generate UNLs based on sampling without replacement according
        //    to weights
        std::vector<UNL> unls(numUNLs);
        std::generate(unls.begin(), unls.end(), [&]() {
            std::vector<PeerID> ids(size);
            std::iota(ids.begin(), ids.end(), 0);
            auto res = random_weighted_shuffle(ids, weights, g);
            return UNL(res.begin(), res.begin() + unlSizePDF(g));
        });

        // 3. Assign membership
        std::vector<int> assignment(size);
        std::uniform_int_distribution<int> u(0, numUNLs - 1);
        std::generate(
            assignment.begin(), assignment.end(), [&]() { return u(g); });

        return TrustGraph(unls, assignment);
    }

    /** Generate a 2 UNL trust graph with some overlap.

        Generates a trust graph for `size` peers formed from
        two cliques with the given overlap.  Nodes in the overlap
        trust both all other nodes, while nodes outside the overlap
        only trust nodes in their clique.

        @param size The number of nodes in the trust graph
        @param overlap The number of nodes trusting both cliques
    */
    static TrustGraph
    makeClique(int size, int overlap);

    /** Generate a complete (fully-connect) trust graph

        Generatest a trust graph in which all peers trust all
        other peers.

        @param size The number of nodes in the trust graph
    */
    static TrustGraph
    makeComplete(int size);
};

//< Make the TrustGraph into a topology with delays given by DelayModel
template <class DelayModel>
auto
topology(TrustGraph const& tg, DelayModel const& d)
{
    return [&](PeerID i, PeerID j) {
        return tg.trusts(i, j) ? boost::make_optional(d(i, j)) : boost::none;
    };
}

class fixed
{
    std::chrono::nanoseconds d_;

public:
    fixed(std::chrono::nanoseconds const& d) : d_{d}
    {
    }

    inline std::chrono::nanoseconds
    operator()(PeerID const& i, PeerID const& j) const
    {
        return d_;
    }
};

}  // csf
}  // test
}  // ripple

#endif
