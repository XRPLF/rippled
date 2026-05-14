#include <test/jtx/Env.h>

#include <xrpld/overlay/Cluster.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>

#include <string>
#include <unordered_map>
#include <utility>

namespace xrpl {

class Peers_test : public beast::unit_test::Suite
{
    void
    testRequest()
    {
        testcase("Basic request");
        using namespace test::jtx;
        Env env{*this};

        // without modification of the cluster, expect an empty set
        // from this request
        auto peers = env.rpc("peers")[jss::result];
        BEAST_EXPECT(peers.isMember(jss::cluster) && peers[jss::cluster].size() == 0);
        BEAST_EXPECT(peers.isMember(jss::peers) && peers[jss::peers].isNull());

        // insert some nodes in to the cluster
        std::unordered_map<std::string, std::string> nodes;
        for (auto i = 0; i < 3; ++i)
        {
            auto kp = generateKeyPair(KeyType::Secp256k1, generateSeed("seed" + std::to_string(i)));

            std::string const name = "Node " + std::to_string(i);

            using namespace std::chrono_literals;
            env.app().getCluster().update(kp.first, name, 200, env.timeKeeper().now() - 10s);
            nodes.insert(std::make_pair(toBase58(TokenType::NodePublic, kp.first), name));
        }

        // make request, verify nodes we created match
        // what is reported
        peers = env.rpc("peers")[jss::result];
        if (!BEAST_EXPECT(peers.isMember(jss::cluster)))
            return;
        if (!BEAST_EXPECT(peers[jss::cluster].size() == nodes.size()))
            return;
        for (auto it = peers[jss::cluster].begin(); it != peers[jss::cluster].end(); ++it)
        {
            auto key = it.key().asString();
            auto search = nodes.find(key);
            if (!BEAST_EXPECTS(search != nodes.end(), key))
                continue;
            if (!BEAST_EXPECT((*it).isMember(jss::tag)))
                continue;
            auto const tag = (*it)[jss::tag].asString();
            BEAST_EXPECTS(tag == nodes[key], key);
        }
        BEAST_EXPECT(peers.isMember(jss::peers) && peers[jss::peers].isNull());
    }

public:
    void
    run() override
    {
        testRequest();
    }
};

BEAST_DEFINE_TESTSUITE(Peers, rpc, xrpl);

}  // namespace xrpl
