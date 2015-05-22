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

#ifndef RIPPLE_PEERFINDER_SIM_GRAPHALGORITHMS_H_INCLUDED
#define RIPPLE_PEERFINDER_SIM_GRAPHALGORITHMS_H_INCLUDED

namespace ripple {
namespace PeerFinder {
namespace Sim {

template <typename Vertex>
struct VertexTraits;

/** Call a function for each vertex in a connected graph.
    Function will be called with this signature:
        void (Vertex&, std::size_t diameter);
*/

template <typename Vertex, typename Function>
void breadth_first_traverse (Vertex& start, Function f)
{
    using Traits = VertexTraits <Vertex>;
    using Edges  = typename Traits::Edges;
    using Edge   = typename Traits::Edge;

    using Probe = std::pair <Vertex*, int>;
    using Work = std::deque <Probe>;
    using Visited = std::set <Vertex*>;
    Work work;
    Visited visited;
    work.emplace_back (&start, 0);
    int diameter (0);
    while (! work.empty ())
    {
        Probe const p (work.front());
        work.pop_front ();
        if (visited.find (p.first) != visited.end ())
            continue;
        diameter = std::max (p.second, diameter);
        visited.insert (p.first);
        for (typename Edges::iterator iter (
            Traits::edges (*p.first).begin());
                iter != Traits::edges (*p.first).end(); ++iter)
        {
            Vertex* v (Traits::vertex (*iter));
            if (visited.find (v) != visited.end())
                continue;
            if (! iter->closed())
                work.emplace_back (v, p.second + 1);
        }
        f (*p.first, diameter);
    }
}


}
}
}

#endif
