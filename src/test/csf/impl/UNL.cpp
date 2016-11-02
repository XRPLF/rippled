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
#include <test/csf/UNL.h>
#include <boost/iterator/counting_iterator.hpp>
#include <fstream>
#include <algorithm>

namespace ripple {
namespace test {
namespace csf {

bool
TrustGraph::canFork(double quorum) const
{
    // Check the forking condition by looking at intersection
    // between all pairs of UNLs.

    // First check if some nodes uses a UNL they are not members of, since
    // this creates an implicit UNL with that ndoe.

    auto uniqueUNLs = UNLs_;

    for (int i = 0; i < assignment_.size(); ++i)
    {
        auto const & myUNL = UNLs_[assignment_[i]];
        if(myUNL.find(i) == myUNL.end())
        {
            auto myUNLcopy = myUNL;
            myUNLcopy.insert(i);
            uniqueUNLs.push_back(std::move(myUNLcopy));
        }
    }

    // Loop over all pairs of uniqueUNLs
    for (int i = 0; i < uniqueUNLs.size(); ++i)
    {
        for (int j = (i+1); j < uniqueUNLs.size(); ++j)
        {
            auto const & unlA = uniqueUNLs[i];
            auto const & unlB = uniqueUNLs[j];

            double rhs = 2.0*(1.-quorum) *
                std::max(unlA.size(), unlB.size() );

            int intersectionSize =  std::count_if(unlA.begin(), unlA.end(),
                [&](PeerID id)
                {
                    return unlB.find(id) != unlB.end();
                });

            if(intersectionSize < rhs)
                return true;
        }
    }
    return false;
}

TrustGraph
TrustGraph::makeClique(int size, int overlap)
{
    using bci = boost::counting_iterator<PeerID>;

    // Split network into two cliques with the given overlap
    // Clique A has nodes [0,endA) and Clique B has [startB,numPeers)
    // Note: Clique B will have an extra peer when numPeers - overlap
    //       is odd
    int endA = (size + overlap)/2;
    int startB = (size - overlap)/2;

    std::vector<UNL> unls;
    unls.emplace_back(bci(0), bci(endA));
    unls.emplace_back(bci(startB), bci(size));
    unls.emplace_back(bci(0), bci(size));

    std::vector<int> assignment(size,0);

    for (int i = 0; i < size; ++i)
    {
        if(i < startB)
            assignment[i] = 0;
        else if(i > endA)
            assignment[i] = 1;
        else
            assignment[i] = 2;
    }


    return TrustGraph(unls, assignment);
}

TrustGraph
TrustGraph::makeComplete(int size)
{
    UNL all{ boost::counting_iterator<PeerID>( 0 ),
             boost::counting_iterator<PeerID>( size ) };

    return TrustGraph(std::vector<UNL>(1,all),
                      std::vector<int>(size, 0));
}

inline void TrustGraph::save_dot(std::string const & fileName)
{
    std::ofstream out(fileName);
    out << "digraph {\n";
    for (int i = 0; i < assignment_.size(); ++i)
    {
        for (auto & j : UNLs_[assignment_[i]])
        {
            out << i << " -> " << j << ";\n";
        }

    }
    out << "}\n";

}

} // csf
} // test
} // ripple