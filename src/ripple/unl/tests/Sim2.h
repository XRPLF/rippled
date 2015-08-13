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

#ifndef RIPPLE_UNL_SIM2_H_INCLUDED
#define RIPPLE_UNL_SIM2_H_INCLUDED

#include <ripple/unl/tests/BasicNetwork.h>
#include <beast/container/aged_unordered_map.h>
#include <boost/container/flat_set.hpp>
#include <boost/optional.hpp>
#include <algorithm>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace ripple {
namespace test {

template <class Log>
struct Sim2
{
    struct Config
    {
    };

    static int const nPeer = 100;       // # of peers
    static int const nDegree = 10;      // outdegree
    static int const nTrial = 1000000;       // number of trials
    static int const nRound = 1;        // number of rounds
    static int const nUNLMin = 20;
    static int const nUNLMax = 30;
    static int const nPos = 10;

    using NodeKey = int;    // identifies a consensus participant
    using ItemKey = int;    // identifies a ballot item
    using ItemSet = boost::container::flat_set<ItemKey>;
    using clock_type = std::chrono::system_clock;

    struct Network;

    struct PosMsg
    {
        NodeKey id;
        std::size_t seq;
        ItemSet items;
        bool last;
    };

    // A round of consensus.
    // Each round consists of a series of votes,
    // terminating when a supermajority is reached.
    class Round
    {
    private:
        int thresh_ = 50;

    public:
        struct Pos
        {
            ItemSet items;
            bool last = false;
            std::size_t seq = 0;
        };

        int id_;
        bool failed_ = false;
        bool consensus_ = false;
        std::unordered_map<NodeKey, Pos> pos_;
        std::size_t count_ = 0;
        clock_type::time_point t0_;
        Log& log_;

    public:
        // Create a new round with initial position
        Round (NodeKey id, ItemSet&& pos,
                clock_type::time_point now, Log& log)
            : id_ (id)
            , t0_ (now)
            , log_ (log)
        {
            using namespace std;
            pos_[id].items = std::move(pos);
        }

        std::shared_ptr<PosMsg const>
        posMsg()
        {
            auto const iter = pos_.find(id_);
            auto m = std::make_shared<PosMsg>();
            m->id = id_;
            m->seq = ++iter->second.seq;
            m->items = iter->second.items;
            m->last = consensus_;
            return m;
        }

        ItemSet const&
        items() const
        {
            return pos_.find(id_)->second.items;
        }

        // Update a peer's position
        // Return `true` if we should relay
        bool
        receive (PosMsg const& m)
        {
            if (m.id == id_)
                return false;
            using namespace std;
            auto& pos = pos_[m.id];
            if (m.seq <= pos.seq)
                return false;
            pos.seq = m.seq;
            pos.last = m.last;
            pos.items = m.items;
            return true;
        }

        // Update our position
        // Returns `true` if we changed our position
        template <class UNL>
        bool
        update (UNL const& unl,
            clock_type::time_point const& now)
        {
            if (consensus_)
                return false;
            // count votes per item from unl
            boost::container::flat_map<
                ItemKey, std::size_t> votes;
            for(auto const& pos : pos_)
            {
                if (! unl.count(pos.first))
                    continue;
                for(auto const& item : pos.second.items)
                {
                    auto const result =
                        votes.emplace(item, 1);
                    if (! result.second)
                        ++result.first->second;
                }
            }
            // calculate our new position
            ItemSet items;
            {
                auto const needed =
                    (thresh_ * unl.size() + 50) / 100;
                for(auto const& v : votes)
                    if (v.second >= needed)
                        items.insert(v.first);
                thresh_ += 5;
            }
            // see if we reached a consensus
            std::size_t most = 0;
            std::size_t agree = 0;
            for(auto const& pos : pos_)
            {
                if (! unl.count(pos.first))
                    continue;
                if (pos.second.items == items)
                    ++agree;
                else if (! pos.second.last)
                    ++most;
            }
            //{
                auto const needed =
                    (80 * unl.size() + 50) / 100;
                if (agree >= needed)
                {
                    consensus_ = true;
                }
                else if (agree + most < needed)
                {
                    failed_ = true;
                    consensus_ = true;
                }
            //}
            if (now.time_since_epoch() >=
                std::chrono::seconds(7))
            {
                log_ <<
                    "agree = " << agree <<
                    ", most = " << most <<
                    ", needed = " << needed <<
                    ", thresh_ = " << thresh_ <<
                    ", items.size() = " << items.size();
            }
            auto const iter = pos_.find(id_);
            if (! consensus_ &&
                    iter->second.items == items)
                return false;
            iter->second.items = items;
            return true;
        }
    };

    //--------------------------------------------------------------------------

    class Peer
    {
    private:
        //beast::aged_unordered_map<

    public:
        NodeKey id_;
        std::set<NodeKey> unl_;
        Config const& config_;
        boost::optional<Round> round_;
        std::chrono::milliseconds delay_;
        Network& net_;

        Peer (int id, Config const& config,
                Network& net)
            : id_ (id)
            , config_ (config)
            , delay_ (std::chrono::milliseconds(
                net.rand(5, 50)))
            , net_ (net)
        {
            auto const size = 1 + net_.rand(
                nUNLMin, nUNLMax + 1);
            unl_.insert(id_); // self
            while(unl_.size() < size)
                unl_.insert(net_.rand(nPeer));
        }

        // Called to begin the round
        void
        start()
        {
            {
                ItemSet pos;
                for(int i = 0; i < nPos; ++i)
                    if (net_.rand(2))
                        pos.insert(i);
                round_.emplace(id_, std::move(pos),
                    net_.now(), net_.log);
            }
            broadcast(round_->posMsg());
            using namespace std::chrono;
            net_.timer(milliseconds(
                700 + net_.rand(700)),
                    [=]() { timer(); });
        }

        void
        receive (Peer& from,
            std::shared_ptr<PosMsg const> const& m)
        {
            if (round_->receive(*m))
                relay(from, m);
            else
                ++net_.dup;
        }

        void
        timer()
        {
            if (round_->update(unl_, net_.now()))
                broadcast(round_->posMsg());
            if (round_->consensus_)
                return;
            using namespace std::chrono;
            net_.timer(milliseconds(700),
                [=]() { timer(); });
        }

        //----------------------------------------------------------------------

        // Send a message to this peer
        template <class Message>
        void
        send (Peer& from,
            std::shared_ptr<Message const> const& m)
        {
            ++net_.sent;
            net_.send (from, *this,
                [&, m]() { receive(from, m); });
        }

        // Broadcast a message to all links
        template <class Message>
        void
        broadcast (std::shared_ptr<
            Message const> const& m)
        {
            for(auto& link : net_.links(*this))
                link.to.send(*this, m);
        }

        // Relay a message to all links
        template <class Message>
        void
        relay (Peer& from,
            std::shared_ptr<Message const> const& m)
        {
            for(auto& link : net_.links(*this))
                if (&link.to != &from)
                    link.to.send(*this, m);
        }
    };

    //--------------------------------------------------------------------------

    struct Network : BasicNetwork<Peer>
    {
        std::size_t dup = 0;        // total dup
        std::size_t sent = 0;       // total sent
        std::vector<Peer> pv;
        Log& log;

        Network (std::size_t seed,
                Config const& config, Log& log_)
            : log (log_)
        {
            this->rng.seed(seed);
            using namespace std;
            using namespace std::chrono;
            pv.reserve(nPeer);
            for(std::size_t id = 0; id < nPeer; ++id)
                pv.emplace_back(id, config, *this);
            for(auto& peer : pv)
                for(int i = 0; i < nDegree; ++i)
                    connect_one(peer);
        }

        // Add one random connection
        void
        connect_one(Peer& from)
        {
            using namespace std::chrono;
            auto const delay = from.delay_ +
                milliseconds(this->rand(5, 200));
            for(;;)
                if (connect(from,
                        pv[this->rand(pv.size())], delay))
                    break;
        }

        void
        report (std::size_t n,
            std::chrono::milliseconds ms, Log& log)
        {
            std::size_t failed = 0;
            std::size_t consensus = 0;
            std::vector<std::size_t> hist;
            hist.resize(nPos);
            for(auto const& p : pv)
            {
                hist_accum(hist, p.round_->items());
                if (p.round_->consensus_)
                    ++consensus;
                if (p.round_->failed_)
                    ++failed;
            }
            log <<
                ((n > 0) ? "#" + std::to_string(n) + " " : "") <<
                seq_string(hist, 3) << "   " <<
                "consensus: " << consensus - failed << " in " <<
                ms.count() << "ms, " <<
                "sent: " << sent << ", " <<
                "dup: " << dup;
        }

        // Execute a round of consensus
        void
        round (std::size_t n)
        {
            using namespace std::chrono;
            for(int i = 0; i < nPeer; ++i)
                pv[i].start();
            auto const t0 = this->now();
    #if 0
            do
            {
                report(0, duration_cast<
                    milliseconds>(now() - t0), log);
            }
            while (this->step_for(milliseconds(50)));
    #else
            this->step();
    #endif
            report(n, duration_cast<
                milliseconds>(this->now() - t0), log);
        }
    };

    static
    void
    run (Log& log)
    {
        log << "Sim2" << ":";
        Config config;
        for(auto i = 1; i <= nTrial; ++i)
        {
            //log << "Trial " << i;
            Network net(i, config, log);
            for(auto j = 1; j <= nRound; ++j)
                net.round(i);
            //log << "\n";
        }
    }
};

} // test
} // ripple

#endif

/*

Try limiting threshold to 80
Try slower increase of threshold
Increase UNL sizes

*/
