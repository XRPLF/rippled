//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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
#include <ripple/unl/tests/BasicNetwork.h>
#include <ripple/unl/tests/metrics.h>
#include <beast/unit_test/suite.h>
#include <boost/optional.hpp>
#include <algorithm>
#include <random>
#include <sstream>
#include <vector>

namespace ripple {
namespace test {

class Net_test : public beast::unit_test::suite
{
public:
    struct Ping
    {
        int hops = 0;
    };

    struct InstantPeer
    {
        using Peer = InstantPeer;
        bool set = false;
        int hops = 0;

        InstantPeer() = default;

        template <class Net>
        std::chrono::seconds
        delay(Net&) const
        {
            return {};
        }

        template <class Net, class Message>
        void
        send (Net& net, Peer& from, Message&& m)
        {
            net.send (from, *this,
                [&, m]() { receive(net, from, m); });
        }

        template <class Net>
        void
        receive (Net& net, Peer& from, Ping p)
        {
            if (set)
                return;
            ++p.hops;
            set = true;
            hops = p.hops;
            for(auto& link : net.links(*this))
                link.to.send(net, *this, p);
        }
    };

    struct LatencyPeer
    {
        using Peer = LatencyPeer;
        int hops = 0;
        bool set = false;

        LatencyPeer() = default;

        template <class Net>
        std::chrono::milliseconds
        delay(Net& net) const
        {
            using namespace std::chrono;
            return milliseconds(net.rand(5, 200));
        }

        template <class Net, class Message>
        void
        send (Net& net, Peer& from, Message&& m)
        {
            net.send (from, *this,
                [&, m]() { receive(net, from, m); });
        }

        template <class Net>
        void
        receive (Net& net, Peer& from, Ping p)
        {
            if (set)
                return;
            ++p.hops;
            set = true;
            hops = p.hops;
            for(auto& link : net.links(*this))
                link.to.send(net, *this, p);
        }
    };

    template <class Peer>
    struct Network : BasicNetwork<Peer>
    {
        static std::size_t const nPeer = 10000;
        static std::size_t const nDegree = 10;

        std::vector<Peer> pv;
        std::mt19937_64 rng;

        Network()
        {
            pv.resize(nPeer);
            for (auto& peer : pv)
                for (auto i = 0; i < nDegree; ++i)
                    connect_one(peer);
        }

        // Return int in range [0, n)
        std::size_t
        rand (std::size_t n)
        {
            return std::uniform_int_distribution<
                std::size_t>(0, n - 1)(rng);
        }

        // Return int in range [base, base+n)
        std::size_t
        rand (std::size_t base, std::size_t n)
        {
            return std::uniform_int_distribution<
                std::size_t>(base, base + n - 1)(rng);
        }

        // Add one random connection
        void
        connect_one (Peer& peer)
        {
            using namespace std::chrono;
            for(;;)
                if (this->connect(peer, pv[rand(pv.size())],
                        peer.delay(*this)))
                    break;
        }
    };

    template <class Peer>
    void
    testDiameter(std::string const& name)
    {
        using Net = Network<Peer>;
        using namespace std::chrono;
        log << name << ":";
        Net net;
        net.pv[0].set = true;
        net.pv[0].hops = 0;
        for(auto& link : net.links(net.pv[0]))
            link.to.send(net, net.pv[0], Ping{});
        net.step();
        std::size_t reach = 0;
        std::vector<int> dist;
        std::vector<int> hops;
        std::vector<int> degree;
        for(auto& peer : net.pv)
        {
            hops.resize(std::max<std::size_t>(
                peer.hops + 1, hops.size()));
            ++hops[peer.hops];
        }
        net.bfs(net.pv[0],
            [&](std::size_t d, Peer& peer)
            {
                ++reach;
                dist.resize(std::max<std::size_t>(
                    d + 1, dist.size()));
                ++dist[d];
                auto const n = net.links(peer).size();
                degree.resize(std::max<std::size_t>(
                    n + 1, degree.size()));
                ++degree[n];
            });
        log << "reach:    " << net.pv.size();
        log << "size:     " << reach;
        log << "hops:     " << seq_string(hops);
        log << "dist:     " << seq_string(dist);
        log << "degree:   " << seq_string(degree);
        log << "diameter: " << diameter(dist);
        log << "hop diam: " << diameter(hops);
    }

    void
    run()
    {
        testDiameter<InstantPeer>("InstantPeer");
        testDiameter<LatencyPeer>("LatencyPeer");
        pass();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(Net,sim,ripple);

}
}
