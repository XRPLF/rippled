//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc

    Permission target use, copy, modify, and/or distribute this software for any
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

#ifndef RIPPLE_TEST_CSF_DIGRAPH_H_INCLUDED
#define RIPPLE_TEST_CSF_DIGRAPH_H_INCLUDED

#include <boost/container/flat_map.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <boost/range/iterator_range.hpp>

#include <fstream>
#include <optional>
#include <type_traits>
#include <unordered_map>

namespace ripple {
namespace test {
namespace csf {

namespace detail {
// Dummy class when no edge data needed for graph
struct NoEdgeData
{
};

}  // namespace detail

/** Directed graph

Basic directed graph that uses an adjacency list to represent out edges.

Instances of Vertex uniquely identify vertices in the graph. Instances of
EdgeData is any data to store in the edge connecting two vertices.

Both Vertex and EdgeData should be lightweight and cheap to copy.

*/
template <class Vertex, class EdgeData = detail::NoEdgeData>
class Digraph
{
    using Links = boost::container::flat_map<Vertex, EdgeData>;
    using Graph = boost::container::flat_map<Vertex, Links>;
    Graph graph_;

    // Allows returning empty iterables for unknown vertices
    Links empty;

public:
    /** Connect two vertices

        @param source The source vertex
        @param target The target vertex
        @param e The edge data
        @return true if the edge was created

    */
    bool
    connect(Vertex source, Vertex target, EdgeData e)
    {
        return graph_[source].emplace(target, e).second;
    }

    /** Connect two vertices using default constructed edge data

        @param source The source vertex
        @param target The target vertex
        @return true if the edge was created

    */
    bool
    connect(Vertex source, Vertex target)
    {
        return connect(source, target, EdgeData{});
    }

    /** Disconnect two vertices

        @param source The source vertex
        @param target The target vertex
        @return true if an edge was removed

        If source is not connected to target, this function does nothing.
    */
    bool
    disconnect(Vertex source, Vertex target)
    {
        auto it = graph_.find(source);
        if (it != graph_.end())
        {
            return it->second.erase(target) > 0;
        }
        return false;
    }

    /** Return edge data between two vertices

        @param source The source vertex
        @param target The target vertex
        @return optional<Edge> which is std::nullopt if no edge exists

    */
    std::optional<EdgeData>
    edge(Vertex source, Vertex target) const
    {
        auto it = graph_.find(source);
        if (it != graph_.end())
        {
            auto edgeIt = it->second.find(target);
            if (edgeIt != it->second.end())
                return edgeIt->second;
        }
        return std::nullopt;
    }

    /** Check if two vertices are connected

        @param source The source vertex
        @param target The target vertex
        @return true if the source has an out edge to target
    */
    bool
    connected(Vertex source, Vertex target) const
    {
        return edge(source, target) != std::nullopt;
    }

    /** Range over vertices in the graph

        @return A boost transformed range over the vertices with out edges in
       the graph
    */
    auto
    outVertices() const
    {
        return boost::adaptors::transform(
            graph_,
            [](typename Graph::value_type const& v) { return v.first; });
    }

    /** Range over target vertices

        @param source The source vertex
        @return A boost transformed range over the target vertices of source.
     */
    auto
    outVertices(Vertex source) const
    {
        auto transform = [](typename Links::value_type const& link) {
            return link.first;
        };
        auto it = graph_.find(source);
        if (it != graph_.end())
            return boost::adaptors::transform(it->second, transform);

        return boost::adaptors::transform(empty, transform);
    }

    /** Vertices and data associated with an Edge
     */
    struct Edge
    {
        Vertex source;
        Vertex target;
        EdgeData data;
    };

    /** Range of out edges

        @param source The source vertex
        @return A boost transformed range of Edge type for all out edges of
                source.
    */
    auto
    outEdges(Vertex source) const
    {
        auto transform = [source](typename Links::value_type const& link) {
            return Edge{source, link.first, link.second};
        };

        auto it = graph_.find(source);
        if (it != graph_.end())
            return boost::adaptors::transform(it->second, transform);

        return boost::adaptors::transform(empty, transform);
    }

    /** Vertex out-degree

        @param source The source vertex
        @return The number of outgoing edges from source
    */
    std::size_t
    outDegree(Vertex source) const
    {
        auto it = graph_.find(source);
        if (it != graph_.end())
            return it->second.size();
        return 0;
    }

    /** Save GraphViz dot file

        Save a GraphViz dot description of the graph
        @param fileName The output file (creates)
        @param vertexName A invokable T vertexName(Vertex const &) that
                          returns the name target use for the vertex in the file
                          T must be be ostream-able
    */
    template <class VertexName>
    void
    saveDot(std::ostream& out, VertexName&& vertexName) const
    {
        out << "digraph {\n";
        for (auto const& [vertex, links] : graph_)
        {
            auto const fromName = vertexName(vertex);
            for (auto const& eData : links)
            {
                auto const toName = vertexName(eData.first);
                out << fromName << " -> " << toName << ";\n";
            }
        }
        out << "}\n";
    }

    template <class VertexName>
    void
    saveDot(std::string const& fileName, VertexName&& vertexName) const
    {
        std::ofstream out(fileName);
        saveDot(out, std::forward<VertexName>(vertexName));
    }
};

}  // namespace csf
}  // namespace test
}  // namespace ripple
#endif
