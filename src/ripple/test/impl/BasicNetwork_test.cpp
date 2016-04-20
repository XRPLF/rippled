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
#include <ripple/test/BasicNetwork.h>
#include <ripple/beast/unit_test.h>
#include <set>
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

        Peer (Peer const&) = default;
        Peer (Peer&&) = default;

        explicit Peer(int id_)
            : id(id_)
        {
        }

        template <class Net>
        void start(Net& net)
        {
            using namespace std::chrono_literals;
            auto t = net.timer(1s,
                [&]{ set.insert(0); });
            if (id == 0)
            {
                for(auto const& link : net.links(this))
                    net.send(this, link.to,
                        [&, to = link.to]
                        {
                            to->receive(net, this, 1);
                        });
            }
            else
            {
                net.cancel(t);
            }
        }

        template <class Net>
        void receive(Net& net, Peer* from, int m)
        {
            set.insert(m);
            ++m;
            if (m < 5)
            {
                for(auto const& link : net.links(this))
                    net.send(this, link.to,
                        [&, mm = m, to = link.to]
                        {
                            to->receive(net, this, mm);
                        });
            }
        }
    };

    void run() override
    {
        using namespace std::chrono_literals;
        std::vector<Peer> pv;
        pv.emplace_back(0);
        pv.emplace_back(1);
        pv.emplace_back(2);
        BasicNetwork<Peer*> net;
        expect(net.rand(0, 1) == 0);
        expect(! net.connect(&pv[0], &pv[0]));
        expect(net.connect(&pv[0], &pv[1], 1s));
        expect(net.connect(&pv[1], &pv[2], 1s));
        expect(! net.connect(&pv[0], &pv[1]));
        std::size_t diameter = 0;
        net.bfs(&pv[0],
            [&](auto d, Peer*)
                { diameter = std::max(d, diameter); });
        expect(diameter == 2);
        for(auto& peer : pv)
            peer.start(net);
        expect(net.step_for(0s));
        expect(net.step_for(1s));
        expect(net.step());
        expect(! net.step());
        expect(! net.step_for(1s));
        net.send(&pv[0], &pv[1], []{});
        net.send(&pv[1], &pv[0], []{});
        expect(net.disconnect(&pv[0], &pv[1]));
        expect(! net.disconnect(&pv[0], &pv[1]));
        for(;;)
        {
            auto const links = net.links(&pv[1]);
            if(links.empty())
                break;
            expect(links[0].disconnect());
        }
        expect(pv[0].set ==
            std::set<int>({0, 2, 4}));
        expect(pv[1].set ==
            std::set<int>({1, 3}));
        expect(pv[2].set ==
            std::set<int>({2, 4}));
        net.timer(0s, []{});
    }
};

BEAST_DEFINE_TESTSUITE(BasicNetwork, test, ripple)

} // test
} // ripple

