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

    class Network;
    class Peer;

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
        template <class Links>
        Slot*
        get (int id, Peer& from,
            Links const& links)
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
            for(auto& link : links)
                if (&link.to != &from)
                    slot.down.insert(&link.to);
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

    //--------------------------------------------------------------------------

    class Peer
    {
    private:
        Network& net_;

    public:
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

        int id = 0; // validator id or 0
        int seq = 0;
        Policy policy;
        std::map<int, int> seen;
        std::chrono::milliseconds delay;

        Peer (int id_, Network& net)
            : net_ (net)
            , id (id_)
            , delay (std::chrono::milliseconds(
                net.rand(5, 50)))
        {
        }

        // Called when a peer disconnects
        void
        disconnect (Peer& from)
        {
            std::vector<int> v;
            for(auto const& item : policy.slots)
                if (item.second.up.count(&from) > 0)
                    v.push_back(item.first);
            for(auto id : v)
                for(auto& link : net_.links(*this))
                    link.to.send(*this,
                        UnsquelchMsg{ id });
        }

        // Broadcast a validation
        void
        broadcast()
        {
            broadcast(ValMsg{ id, ++seq });
        }

        // Receive a validation
        void
        receive (Peer& from, ValMsg const& m)
        {
            if (m.id == id)
            {
                ++nth(net_.dup, m.id - 1);
                return from.send(*this,
                    SquelchMsg(m.id));
            }
            auto slot = policy.get(
                m.id, from, net_.links(*this));
            if (! slot || ! policy.uplink(
                    m.id, from, *slot))
                return from.send(*this,
                    SquelchMsg(m.id));
            auto& last = seen[m.id];
            if (last >= m.seq)
            {
                ++nth(net_.dup, m.id - 1);
                return;
            }
            last = m.seq;
            policy.heard(m.id, m.seq);
            ++nth(net_.heard, m.id - 1);
            for(auto peer : slot->down)
                peer->send(*this, m);
        }

        // Receive a squelch message
        void
        receive (Peer& from, SquelchMsg const& m)
        {
            policy.squelch (m.id, from);
        }

        // Receive an unsquelch message
        void
        receive (Peer& from, UnsquelchMsg const& m)
        {
            policy.unsquelch (m.id, from);
        }

        //----------------------------------------------------------------------

        // Send a message to this peer
        template <class Message>
        void
        send (Peer& from, Message&& m)
        {
            ++net_.sent;
            using namespace std::chrono;
            net_.send (from, *this,
                [&, m]() { receive(from, m); });
        }

        // Broadcast a message to all links
        template <class Message>
        void
        broadcast (Message const& m)
        {
            for(auto& link : net_.links(*this))
                link.to.send(*this, m);
        }
    };

    //--------------------------------------------------------------------------

    class Network : public BasicNetwork<Peer>
    {
    public:
        std::size_t sent = 0;
        std::vector<Peer> pv;
        std::vector<std::size_t> heard;
        std::vector<std::size_t> dup;

        Network()
        {
            using namespace std;
            using namespace std::chrono;
            pv.reserve(nPeer);
            for(std::size_t id = 1; id <=nPeer; ++id)
                pv.emplace_back(
                    id <= nValidator ? id : 0, *this);
            for (auto& peer : pv)
                for (auto i = 0; i < nDegree; ++i)
                    connect_one(peer);
        }

        // Add one random connection
        void
        connect_one(Peer& from)
        {
            using namespace std::chrono;
            auto const delay = from.delay +
                milliseconds(rand(5, 200));
            for(;;)
                if (connect(from,
                        pv[rand(pv.size())], delay))
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
            link.to.disconnect(peer);
            peer.disconnect(link.to);
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
        run (Log& log)
        {
            for (int i = nStep; i--;)
            {
                churn();
                for(int j = 0; j < nValidator; ++j)
                    pv[j].broadcast();
                step();
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
        net.run(log);
        std::size_t reach = 0;
        std::vector<int> dist;
        std::vector<int> degree;
        net.bfs(net.pv[0],
            [&](std::size_t d, Peer& peer)
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
