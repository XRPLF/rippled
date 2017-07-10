//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2015 Ripple Labs Inc.

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
#include <ripple/beast/unit_test.h>
#include <set>
#include <test/csf/BasicNetwork.h>
#include <test/csf/Scheduler.h>
#include <vector>

namespace ripple {
namespace test {

class BasicNetwork_test : public beast::unit_test::suite
{
public:
    struct Peer
    {
        int id;
        std::set<int> set;

        Peer(Peer const&) = default;
        Peer(Peer&&) = default;

        explicit Peer(int id_) : id(id_)
        {
        }

        template <class Net>
        void
        start(csf::Scheduler& scheduler, Net& net)
        {
            using namespace std::chrono_literals;
            auto t = scheduler.in(1s, [&] { set.insert(0); });
            if (id == 0)
            {
                for (auto const& link : net.links(this))
                    net.send(this, link.to, [&, to = link.to ] {
                        to->receive(net, this, 1);
                    });
            }
            else
            {
                scheduler.cancel(t);
            }
        }

        template <class Net>
        void
        receive(Net& net, Peer* from, int m)
        {
            set.insert(m);
            ++m;
            if (m < 5)
            {
                for (auto const& link : net.links(this))
                    net.send(this, link.to, [&, mm = m, to = link.to ] {
                        to->receive(net, this, mm);
                    });
            }
        }
    };

    void
    testNetwork()
    {
        using namespace std::chrono_literals;
        std::vector<Peer> pv;
        pv.emplace_back(0);
        pv.emplace_back(1);
        pv.emplace_back(2);
        csf::Scheduler scheduler;
        csf::BasicNetwork<Peer*> net(scheduler);
        BEAST_EXPECT(!net.connect(&pv[0], &pv[0]));
        BEAST_EXPECT(net.connect(&pv[0], &pv[1], 1s));
        BEAST_EXPECT(net.connect(&pv[1], &pv[2], 1s));
        BEAST_EXPECT(!net.connect(&pv[0], &pv[1]));
        std::size_t diameter = 0;
        net.bfs(
            &pv[0], [&](auto d, Peer*) { diameter = std::max(d, diameter); });
        BEAST_EXPECT(diameter == 2);
        for (auto& peer : pv)
            peer.start(scheduler, net);
        BEAST_EXPECT(scheduler.step_for(0s));
        BEAST_EXPECT(scheduler.step_for(1s));
        BEAST_EXPECT(scheduler.step());
        BEAST_EXPECT(!scheduler.step());
        BEAST_EXPECT(!scheduler.step_for(1s));
        net.send(&pv[0], &pv[1], [] {});
        net.send(&pv[1], &pv[0], [] {});
        BEAST_EXPECT(net.disconnect(&pv[0], &pv[1]));
        BEAST_EXPECT(!net.disconnect(&pv[0], &pv[1]));
        for (;;)
        {
            auto const links = net.links(&pv[1]);
            if (links.empty())
                break;
            BEAST_EXPECT(links[0].disconnect());
        }
        BEAST_EXPECT(pv[0].set == std::set<int>({0, 2, 4}));
        BEAST_EXPECT(pv[1].set == std::set<int>({1, 3}));
        BEAST_EXPECT(pv[2].set == std::set<int>({2, 4}));
    }

    void
    testDisconnect()
    {
        using namespace std::chrono_literals;
        csf::Scheduler scheduler;
        csf::BasicNetwork<int> net(scheduler);
        BEAST_EXPECT(net.connect(0, 1, 1s));
        BEAST_EXPECT(net.connect(0, 2, 2s));

        std::set<int> delivered;
        net.send(0, 1, [&]() { delivered.insert(1); });
        net.send(0, 2, [&]() { delivered.insert(2); });

        scheduler.in(1000ms, [&]() { BEAST_EXPECT(net.disconnect(0, 2)); });
        scheduler.in(1100ms, [&]() { BEAST_EXPECT(net.connect(0, 2)); });

        scheduler.step();

        // only the first message is delivered because the disconnect at 1 s
        // purges all pending messages from 0 to 2
        BEAST_EXPECT(delivered == std::set<int>({1}));
    }

    void
    run() override
    {
        testNetwork();
        testDisconnect();

    }
};

BEAST_DEFINE_TESTSUITE(BasicNetwork, test, ripple);

}  // namespace test
}  // namespace ripple
