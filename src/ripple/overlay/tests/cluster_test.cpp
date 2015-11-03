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

#include <BeastConfig.h>
#include <ripple/basics/TestSuite.h>
#include <ripple/overlay/Cluster.h>
#include <ripple/overlay/ClusterNode.h>

namespace ripple {
namespace tests {

class cluster_test : public ripple::TestSuite
{
public:
    std::unique_ptr<Cluster>
    make_Cluster (std::vector<RippleAddress> const& nodes)
    {
        auto cluster = std::make_unique <Cluster> (beast::Journal ());

        for (auto const& n : nodes)
            cluster->update (n, n.humanNodePublic());

        return cluster;
    }

    RippleAddress
    randomNode ()
    {
        return RippleAddress::createNodePublic (
            RippleAddress::createSeedRandom ());
    }

    void
    testMembership ()
    {
        // The servers on the network
        std::vector<RippleAddress> network;

        while (network.size () != 128)
            network.push_back (randomNode());

        {
            testcase ("Membership: Empty cluster");

            auto c = make_Cluster ({});

            for (auto const& n : network)
                expect (!c->member (n));
        }

        {
            testcase ("Membership: Non-empty cluster and none present");

            std::vector<RippleAddress> cluster;
            while (cluster.size () != 32)
                cluster.push_back (randomNode());

            auto c = make_Cluster (cluster);

            for (auto const& n : network)
                expect (!c->member (n));
        }

        {
            testcase ("Membership: Non-empty cluster and some present");

            std::vector<RippleAddress> cluster (
                network.begin (), network.begin () + 16);

            while (cluster.size () != 32)
                cluster.push_back (randomNode());

            auto c = make_Cluster (cluster);

            for (auto const& n : cluster)
                expect (c->member (n));

            for (auto const& n : network)
            {
                auto found = std::find (
                    cluster.begin (), cluster.end (), n);
                expect (static_cast<bool>(c->member (n)) ==
                    (found != cluster.end ()));
            }
        }

        {
            testcase ("Membership: Non-empty cluster and all present");

            std::vector<RippleAddress> cluster (
                network.begin (), network.begin () + 32);

            auto c = make_Cluster (cluster);

            for (auto const& n : cluster)
                expect (c->member (n));

            for (auto const& n : network)
            {
                auto found = std::find (
                    cluster.begin (), cluster.end (), n);
                expect (static_cast<bool>(c->member (n)) ==
                    (found != cluster.end ()));
            }
        }
    }

    void
    testUpdating ()
    {
        testcase ("Updating");

        auto c = make_Cluster ({});

        auto const node = randomNode ();
        std::uint32_t load = 0;
        std::uint32_t tick = 0;

        // Initial update
        expect (c->update (node, "", load, tick));
        {
            auto member = c->member (node);
            expect (static_cast<bool>(member));
            expect (member->empty ());
        }

        // Updating too quickly: should fail
        expect (! c->update (node, node.humanNodePublic (), load, tick));
        {
            auto member = c->member (node);
            expect (static_cast<bool>(member));
            expect (member->empty ());
        }

        // Updating the name (empty updates to non-empty)
        expect (c->update (node, node.humanNodePublic (), load, ++tick));
        {
            auto member = c->member (node);
            expect (static_cast<bool>(member));
            expect (member->compare(node.humanNodePublic ()) == 0);
        }

        // Updating the name (non-empty doesn't go to empty)
        expect (c->update (node, "", load, ++tick));
        {
            auto member = c->member (node);
            expect (static_cast<bool>(member));
            expect (member->compare(node.humanNodePublic ()) == 0);
        }

        // Updating the name (non-empty updates to new non-empty)
        expect (c->update (node, "test", load, ++tick));
        {
            auto member = c->member (node);
            expect (static_cast<bool>(member));
            expect (member->compare("test") == 0);
        }
    }

    void
    run() override
    {
        testMembership ();
        testUpdating ();
    }
};

BEAST_DEFINE_TESTSUITE(cluster,overlay,ripple);

} // tests
} // ripple
