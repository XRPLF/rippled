#include <test/csf/Digraph.h>

#include <xrpl/beast/unit_test.h>

#include <string>
#include <vector>

namespace ripple {
namespace test {

class Digraph_test : public beast::unit_test::suite
{
public:
    void
    run() override
    {
        using namespace csf;
        using Graph = Digraph<char, std::string>;
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

            for (auto const& edge : graph.outEdges('a'))
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
        std::vector<char> expected = {'b', 'c'};

        BEAST_EXPECT((graph.outVertices('a') == expected));
        BEAST_EXPECT(graph.outVertices('b').size() == 0);
        BEAST_EXPECT(graph.outVertices('c').size() == 0);
        BEAST_EXPECT(graph.outVertices('r').size() == 0);

        std::stringstream ss;
        graph.saveDot(ss, [](char v) { return v; });
        std::string expectedDot =
            "digraph {\n"
            "a -> b;\n"
            "a -> c;\n"
            "}\n";
        BEAST_EXPECT(ss.str() == expectedDot);
    }
};

BEAST_DEFINE_TESTSUITE(Digraph, csf, ripple);

}  // namespace test
}  // namespace ripple
