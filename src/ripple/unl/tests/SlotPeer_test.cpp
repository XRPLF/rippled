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
#include <beast/unit_test/suite.h>
#include <boost/container/flat_set.hpp>
#include <algorithm>
#include <random>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ripple {
namespace test {

struct Config
{
    static int const nStep = 100;           // # of steps
    static int const nPeer = 1001;          // # of peers
    static int const nDegree = 10;          // outdegree
    static int const nChurn = 5;            // churn per step
    static int const nValidator = 100;      // # of validators
    static int const nTrusted = 5;          // # of trusted validators
    static int const nAllowed = 5;          // # of allowed validators
    static int const nTrustedUplinks = 3;   // # of uplinks for trusted
    static int const nAllowedUplinks = 1;   // # of uplinks for allowed

    struct Peer
    {
        struct ValMsg
        {
            int id;
            int seq;
            ValMsg (int id_, int seq_)
                : id(id_), seq(seq_) { }
        };

        struct SquelchMsg
        {
            int id;
            SquelchMsg (int id_)
                : id(id_) { }
        };

        struct UnsquelchMsg
        {
            int id;
            UnsquelchMsg (int id_)
                : id(id_) { }
        };

        struct Policy
        {
            struct Slot
            {
                std::unordered_set<Peer*> up;
                std::unordered_set<Peer*> down;
            };

            std::unordered_set<int> allowed;
            std::unordered_map<int, Slot> slots;

            // Returns a slot or nullptr
            template <class Peers>
            Slot*
            get (int id, Peer& from,
                Peers const& peers)
            {
                auto iter = slots.find(id);
                if (iter != slots.end())
                    return &iter->second;
                if (id > nTrusted &&
                        allowed.size() >= nAllowed)
                    return nullptr;
                if (id > nTrusted)
                    allowed.insert(id);
                auto& slot = slots[id];
                for(auto& peer : peers)
                    if (&peer != &from)
                        slot.down.insert(&peer);
                return &slot;
            }

            // Returns `true` accepting Peer as uplink
            bool
            uplink (int id, Peer& from, Slot& slot)
            {
                if (slot.up.count(&from) > 0)
                    return true;
                if (id <= nTrusted)
                {
                    if (slot.up.size() >= nTrustedUplinks)
                        return false;
                    slot.up.insert(&from);
                    return true;
                }
                if (slot.up.size() >= nAllowedUplinks)
                    return false;
                slot.up.insert(&from);
                return true;
            }

            // Squelch a downlink
            void
            squelch (int id, Peer& from)
            {
                auto iter = slots.find(id);
                if (iter == slots.end())
                    return;
                iter->second.down.erase(&from);
            }

            // Unsquelch a downlink
            void
            unsquelch (int id, Peer& from)
            {
                auto iter = slots.find(id);
                if (iter == slots.end())
                    return;
                iter->second.down.insert(&from);
            }

            // Called when we hear a validation
            void
            heard (int id, int seq)
            {
            }
        };

        int id = 0; // validator id or 0
        int seq = 0;
        Policy policy;
        std::map<int, int> seen;
        std::chrono::milliseconds delay;

        // Called when a peer disconnects
        template <class Net>
        void
        disconnect (Net& net, Peer& from)
        {
            std::vector<int> v;
            for(auto const& item : policy.slots)
                if (item.second.up.count(&from) > 0)
                    v.push_back(item.first);
            for(auto id : v)
                for(auto& peer : net.peers(*this))
                    peer.send(net, *this,
                        UnsquelchMsg{ id });
        }

        // Send a message to this peer
        template <class Net, class Message>
        void
        send (Net& net, Peer& from, Message&& m)
        {
            ++net.sent;
            using namespace std::chrono;
            net.send (from, *this,
                std::forward<Message>(m));
        }

        // Relay a message to all links
        template <class Net, class Message>
        void
        relay (Net& net, Message&& m)
        {
            for(auto& peer : net.peers(*this))
                peer.send(net, *this,
                    std::forward<Message>(m));
        }

        // Broadcast a validation
        template <class Net>
        void
        broadcast (Net& net)
        {
            relay(net, ValMsg{ id, ++seq });
        }

        // Receive a validation
        template <class Net>
        void
        receive (Net& net, Peer& from, ValMsg const& m)
        {
            if (m.id == id)
            {
                ++nth(net.dup, m.id - 1);
                return from.send(net, *this,
                    SquelchMsg(m.id));
            }
            auto slot = policy.get(
                m.id, from, net.peers(*this));
            if (! slot || ! policy.uplink(
                    m.id, from, *slot))
                return from.send(net, *this,
                    SquelchMsg(m.id));
            auto& last = seen[m.id];
            if (last >= m.seq)
            {
                ++nth(net.dup, m.id - 1);
                return;
            }
            last = m.seq;
            policy.heard(m.id, m.seq);
            ++nth(net.heard, m.id - 1);
            for(auto peer : slot->down)
                peer->send(net, *this, m);
        }

        // Receive a squelch message
        template <class Net>
        void
        receive (Net& net, Peer& from, SquelchMsg const& m)
        {
            policy.squelch (m.id, from);
        }

        // Receive an unsquelch message
        template <class Net>
        void
        receive (Net& net, Peer& from, UnsquelchMsg const& m)
        {
            policy.unsquelch (m.id, from);
        }
    };

    //--------------------------------------------------------------------------

    struct Network
        : BasicNetwork<Peer, Network>
    {
        std::size_t sent = 0;
        std::vector<Peer> pv;
        std::mt19937_64 rng;
        std::vector<std::size_t> heard;
        std::vector<std::size_t> dup;

        Network()
        {
            pv.resize(nPeer);
            for (int i = 0; i < nValidator; ++i)
                pv[i].id = i + 1;
            for (auto& peer : pv)
            {
                using namespace std::chrono;
                peer.delay =
                    milliseconds(rand(5, 45));
                for (auto i = 0; i < nDegree; ++i)
                    connect_one(peer);
            }
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
                if (connect(peer, pv[rand(pv.size())],
                        peer.delay + milliseconds(rand(5, 200))))
                    break;
        }

        // Redo one random connection
        void
        churn_one()
        {
            auto& peer = pv[rand(pv.size())];
            auto const link = links(peer)[
                rand(links(peer).size())];
            link.disconnect();
            link.to.disconnect(*this, peer);
            peer.disconnect(*this, link.to);
            link.to.disconnect(*this, peer);
            // preserve outbound counts, otherwise
            // the outdegree invariant will break.
            if (link.inbound)
                connect_one (link.to);
            else
                connect_one (peer);
        }

        // Redo several random connections
        void
        churn()
        {
            auto n = nChurn;
            while(n--)
                churn_one();
        }

        // Iterate the network
        template <class Log>
        void
        step (Log& log)
        {
            for (int i = nStep; i--;)
            {
                churn();
                for(int j = 0; j < nValidator; ++j)
                    pv[j].broadcast(*this);
                run();
            }
        }
    };
};

//------------------------------------------------------------------------------

class SlotPeer_test : public beast::unit_test::suite
{
public:
    void
    test(std::string const& name)
    {
        log << name << ":";
        using Peer = Config::Peer;
        using Network = Config::Network;
        Network net;
        net.step(log);
        std::size_t reach = 0;
        std::vector<int> dist;
        std::vector<int> degree;
        net.bfs(net.pv[0],
            [&](Network& net, std::size_t d, Peer& peer)
            {
                ++reach;
                ++nth(dist, d);
                ++nth(degree, net.links(peer).size());
            });
        log << "reach:    " << net.pv.size();
        log << "size:     " << reach;
        log << "sent:     " << net.sent;
        log << "diameter: " << diameter(dist);
        log << "dist:     " << seq_string(dist);
        log << "heard:    " << seq_string(net.heard);
        log << "dup:      " << seq_string(net.dup);
        log << "degree:   " << seq_string(degree);
    }

    void
    run()
    {
        test("SlotPeer");
        pass();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(SlotPeer,sim,ripple);

}
}
