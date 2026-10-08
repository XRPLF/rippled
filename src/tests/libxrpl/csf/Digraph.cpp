#include <csf/Digraph.h>

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace xrpl::test {

TEST(DigraphTest, digraph)
{
    using namespace csf;
    using Graph = Digraph<char, std::string>;
    Graph graph;

    EXPECT_TRUE(!graph.connected('a', 'b'));
    EXPECT_TRUE(!graph.edge('a', 'b'));
    EXPECT_TRUE(!graph.disconnect('a', 'b'));

    EXPECT_TRUE(graph.connect('a', 'b', "foobar"));
    EXPECT_TRUE(graph.connected('a', 'b'));
    EXPECT_TRUE(*graph.edge('a', 'b') == "foobar");  // NOLINT(bugprone-unchecked-optional-access)

    EXPECT_TRUE(!graph.connect('a', 'b', "repeat"));
    EXPECT_TRUE(graph.disconnect('a', 'b'));
    EXPECT_TRUE(graph.connect('a', 'b', "repeat"));
    EXPECT_TRUE(graph.connected('a', 'b'));
    EXPECT_TRUE(*graph.edge('a', 'b') == "repeat");  // NOLINT(bugprone-unchecked-optional-access)

    EXPECT_TRUE(graph.connect('a', 'c', "tree"));

    {
        std::vector<std::tuple<char, char, std::string>> edges;

        for (auto const& edge : graph.outEdges('a'))
        {
            edges.emplace_back(edge.source, edge.target, edge.data);
        }

        std::vector<std::tuple<char, char, std::string>> expected;
        expected.emplace_back('a', 'b', "repeat");
        expected.emplace_back('a', 'c', "tree");
        EXPECT_TRUE(edges == expected);
        EXPECT_TRUE(graph.outDegree('a') == expected.size());
    }

    EXPECT_TRUE(graph.outEdges('r').size() == 0);
    EXPECT_TRUE(graph.outDegree('r') == 0);
    EXPECT_TRUE(graph.outDegree('c') == 0);

    // only 'a' has out edges
    EXPECT_TRUE(graph.outVertices().size() == 1);
    std::vector<char> const expected = {'b', 'c'};

    EXPECT_TRUE((graph.outVertices('a') == expected));
    EXPECT_TRUE(graph.outVertices('b').size() == 0);
    EXPECT_TRUE(graph.outVertices('c').size() == 0);
    EXPECT_TRUE(graph.outVertices('r').size() == 0);

    std::stringstream ss;
    graph.saveDot(ss, [](char v) { return v; });
    std::string const expectedDot =
        "digraph {\n"
        "a -> b;\n"
        "a -> c;\n"
        "}\n";
    EXPECT_TRUE(ss.str() == expectedDot);
}

}  // namespace xrpl::test
