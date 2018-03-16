//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc.

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

#include <test/jtx.h>
#include <test/jtx/Env.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/overlay/Cluster.h>
#include <ripple/overlay/Overlay.h>
#include <unordered_map>

namespace ripple {

class Peers_test : public beast::unit_test::suite
{
    void testRequest()
    {
        testcase("Basic request");
        using namespace test::jtx;
        Env env {*this};

        // without modification of the cluster, expect an empty set
        // from this request
        auto peers = env.rpc("peers")[jss::result];
        BEAST_EXPECT(peers.isMember(jss::cluster) &&
            peers[jss::cluster].size() == 0 );
        BEAST_EXPECT(peers.isMember(jss::peers) &&
            peers[jss::peers].isNull());

        // insert some nodes in to the cluster
        std::unordered_map<std::string, std::string> nodes;
        for(auto i =0; i < 3; ++i)
        {

            auto kp = generateKeyPair (KeyType::secp256k1,
                generateSeed("seed" + std::to_string(i)));

            std::string name = "Node " + std::to_string(i);

            env.app().cluster().update(
                kp.first,
                name,
                200,
                env.timeKeeper().now() - 10s);
            nodes.insert( std::make_pair(
                toBase58(TokenType::NodePublic, kp.first), name));
        }

        // make request, verify nodes we created match
        // what is reported
        peers = env.rpc("peers")[jss::result];
        if(! BEAST_EXPECT(peers.isMember(jss::cluster)))
            return;
        if(! BEAST_EXPECT(peers[jss::cluster].size() == nodes.size()))
            return;
        for(auto it  = peers[jss::cluster].begin();
                 it != peers[jss::cluster].end();
                 ++it)
        {
            auto key = it.key().asString();
            auto search = nodes.find(key);
            if(! BEAST_EXPECTS(search != nodes.end(), key))
                continue;
            if(! BEAST_EXPECT((*it).isMember(jss::tag)))
                continue;
            auto tag = (*it)[jss::tag].asString();
            BEAST_EXPECTS((*it)[jss::tag].asString() == nodes[key], key);
        }
        BEAST_EXPECT(peers.isMember(jss::peers) && peers[jss::peers].isNull());
    }

public:
    void run ()
    {
        testRequest();
    }
};

BEAST_DEFINE_TESTSUITE (Peers, rpc, ripple);

}  // ripple
