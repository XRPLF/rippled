//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright 2015 Ripple Labs Inc.

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

#include <ripple/basics/BasicConfig.h>
#include <test/jtx/TestSuite.h>
#include <ripple/overlay/Cluster.h>
#include <ripple/overlay/ClusterNode.h>
#include <ripple/protocol/SecretKey.h>

namespace ripple {
namespace tests {

class cluster_test : public ripple::TestSuite
{
public:
    std::unique_ptr<Cluster>
    create (std::vector<PublicKey> const& nodes)
    {
        auto cluster = std::make_unique <Cluster> (beast::Journal ());

        for (auto const& n : nodes)
            cluster->update (n, "Test");

        return cluster;
    }

    PublicKey
    randomNode ()
    {
        return derivePublicKey (
            KeyType::secp256k1,
            randomSecretKey());
    }

    void
    testMembership ()
    {
        // The servers on the network
        std::vector<PublicKey> network;

        while (network.size () != 128)
            network.push_back (randomNode());

        {
            testcase ("Membership: Empty cluster");

            auto c = create ({});

            for (auto const& n : network)
                BEAST_EXPECT(!c->member (n));
        }

        {
            testcase ("Membership: Non-empty cluster and none present");

            std::vector<PublicKey> cluster;
            while (cluster.size () != 32)
                cluster.push_back (randomNode());

            auto c = create (cluster);

            for (auto const& n : network)
                BEAST_EXPECT(!c->member (n));
        }

        {
            testcase ("Membership: Non-empty cluster and some present");

            std::vector<PublicKey> cluster (
                network.begin (), network.begin () + 16);

            while (cluster.size () != 32)
                cluster.push_back (randomNode());

            auto c = create (cluster);

            for (auto const& n : cluster)
                BEAST_EXPECT(c->member (n));

            for (auto const& n : network)
            {
                auto found = std::find (
                    cluster.begin (), cluster.end (), n);
                BEAST_EXPECT(static_cast<bool>(c->member (n)) ==
                    (found != cluster.end ()));
            }
        }

        {
            testcase ("Membership: Non-empty cluster and all present");

            std::vector<PublicKey> cluster (
                network.begin (), network.begin () + 32);

            auto c = create (cluster);

            for (auto const& n : cluster)
                BEAST_EXPECT(c->member (n));

            for (auto const& n : network)
            {
                auto found = std::find (
                    cluster.begin (), cluster.end (), n);
                BEAST_EXPECT(static_cast<bool>(c->member (n)) ==
                    (found != cluster.end ()));
            }
        }
    }

    void
    testUpdating ()
    {
        testcase ("Updating");

        auto c = create ({});

        auto const node = randomNode ();
        auto const name = toBase58(
            TokenType::NodePublic,
            node);
        std::uint32_t load = 0;
        NetClock::time_point tick = {};

        // Initial update
        BEAST_EXPECT(c->update (node, "", load, tick));
        {
            auto member = c->member (node);
            BEAST_EXPECT(static_cast<bool>(member));
            BEAST_EXPECT(member->empty ());
        }

        // Updating too quickly: should fail
        BEAST_EXPECT(! c->update (node, name, load, tick));
        {
            auto member = c->member (node);
            BEAST_EXPECT(static_cast<bool>(member));
            BEAST_EXPECT(member->empty ());
        }

        using namespace std::chrono_literals;

        // Updating the name (empty updates to non-empty)
        tick += 1s;
        BEAST_EXPECT(c->update (node, name, load, tick));
        {
            auto member = c->member (node);
            BEAST_EXPECT(static_cast<bool>(member));
            BEAST_EXPECT(member->compare(name) == 0);
        }

        // Updating the name (non-empty doesn't go to empty)
        tick += 1s;
        BEAST_EXPECT(c->update (node, "", load, tick));
        {
            auto member = c->member (node);
            BEAST_EXPECT(static_cast<bool>(member));
            BEAST_EXPECT(member->compare(name) == 0);
        }

        // Updating the name (non-empty updates to new non-empty)
        tick += 1s;
        BEAST_EXPECT(c->update (node, "test", load, tick));
        {
            auto member = c->member (node);
            BEAST_EXPECT(static_cast<bool>(member));
            BEAST_EXPECT(member->compare("test") == 0);
        }
    }

    void
    testConfigLoad ()
    {
        testcase ("Config Load");

        auto c = std::make_unique <Cluster> (beast::Journal ());

        // The servers on the network
        std::vector<PublicKey> network;

        while (network.size () != 8)
            network.push_back (randomNode());

        auto format = [](
            PublicKey const &publicKey,
            char const* comment = nullptr)
        {
            auto ret = toBase58(
                TokenType::NodePublic,
                publicKey);

            if (comment)
                ret += comment;

            return ret;
        };

        Section s1;

        // Correct (empty) configuration
        BEAST_EXPECT(c->load (s1));
        BEAST_EXPECT(c->size() == 0);

        // Correct configuration
        s1.append (format (network[0]));
        s1.append (format (network[1], "    "));
        s1.append (format (network[2], " Comment"));
        s1.append (format (network[3], " Multi Word Comment"));
        s1.append (format (network[4], "  Leading Whitespace"));
        s1.append (format (network[5], " Trailing Whitespace  "));
        s1.append (format (network[6], "  Leading & Trailing Whitespace  "));
        s1.append (format (network[7], "  Leading,  Trailing  &  Internal  Whitespace  "));

        BEAST_EXPECT(c->load (s1));

        for (auto const& n : network)
            BEAST_EXPECT(c->member (n));

        // Incorrect configurations
        Section s2;
        s2.append ("NotAPublicKey");
        BEAST_EXPECT(!c->load (s2));

        Section s3;
        s3.append (format (network[0], "!"));
        BEAST_EXPECT(!c->load (s3));

        Section s4;
        s4.append (format (network[0], "!  Comment"));
        BEAST_EXPECT(!c->load (s4));

        // Check if we properly terminate when we encounter
        // a malformed or unparseable entry:
        auto const node1 = randomNode();
        auto const node2 = randomNode ();

        Section s5;
        s5.append (format (node1, "XXX"));
        s5.append (format (node2));
        BEAST_EXPECT(!c->load (s5));
        BEAST_EXPECT(!c->member (node1));
        BEAST_EXPECT(!c->member (node2));
    }

    void
    run() override
    {
        testMembership ();
        testUpdating ();
        testConfigLoad ();
    }
};

BEAST_DEFINE_TESTSUITE(cluster,overlay,ripple);

} // tests
} // ripple
