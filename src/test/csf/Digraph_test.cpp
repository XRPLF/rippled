//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

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

#include <ripple/beast/unit_test.h>
#include <test/csf/Digraph.h>
#include <vector>
#include <string>

namespace ripple {
namespace test {

class Digraph_test : public beast::unit_test::suite
{
public:

    void
    run() override
    {
        using namespace csf;
        using Graph = Digraph<char,std::string>;
        Graph graph;

        BEAST_EXPECT(!graph.connected('a', 'b'));
        BEAST_EXPECT(!graph.edge('a', 'b'));
        BEAST_EXPECT(!graph.disconnect('a', 'b'));

        BEAST_EXPECT(graph.connect('a', 'b', "foobar"));
        BEAST_EXPECT(graph.connected('a', 'b'));
        BEAST_EXPECT(*graph.edge('a', 'b') == "foobar");

        BEAST_EXPECT(!graph.connect('a', 'b', "repeat"));
        BEAST_EXPECT(graph.disconnect('a', 'b'));
        BEAST_EXPECT(graph.connect('a', 'b', "repeat"));
        BEAST_EXPECT(graph.connected('a', 'b'));
        BEAST_EXPECT(*graph.edge('a', 'b') == "repeat");


        BEAST_EXPECT(graph.connect('a', 'c', "tree"));

        {
            std::vector<std::tuple<char, char, std::string>> edges;

            for (auto const & edge : graph.outEdges('a'))
            {
                edges.emplace_back(edge.source, edge.target, edge.data);
            }

            std::vector<std::tuple<char, char, std::string>> expected;
            expected.emplace_back('a', 'b', "repeat");
            expected.emplace_back('a', 'c', "tree");
            BEAST_EXPECT(edges == expected);
            BEAST_EXPECT(graph.outDegree('a') == expected.size());
        }

        BEAST_EXPECT(graph.outEdges('r').size() == 0);
        BEAST_EXPECT(graph.outDegree('r') == 0);
        BEAST_EXPECT(graph.outDegree('c') == 0);

        // only 'a' has out edges
        BEAST_EXPECT(graph.outVertices().size() == 1);
        std::vector<char> expected = {'b','c'};

        BEAST_EXPECT((graph.outVertices('a') == expected));
        BEAST_EXPECT(graph.outVertices('b').size() == 0);
        BEAST_EXPECT(graph.outVertices('c').size() == 0);
        BEAST_EXPECT(graph.outVertices('r').size() == 0);

        std::stringstream ss;
        graph.saveDot(ss, [](char v) { return v;});
        std::string expectedDot = "digraph {\n"
        "a -> b;\n"
        "a -> c;\n"
        "}\n";
        BEAST_EXPECT(ss.str() == expectedDot);


    }
};

BEAST_DEFINE_TESTSUITE(Digraph, test, ripple);

}  // namespace test
}  // namespace ripple
