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

#ifndef RIPPLE_UNL_SIM4_H_INCLUDED
#define RIPPLE_UNL_SIM4_H_INCLUDED

#include <ripple/unl/tests/BasicNetwork.h>
#include <beast/container/aged_unordered_map.h>
#include <boost/container/flat_set.hpp>
#include <boost/optional.hpp>
#include <algorithm>
#include <memory>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace ripple {
namespace test {

template <class Log>
struct Sim4
{
    struct Config
    {
        int unl = 20;
        int peers = 100;
        int trials = 100;
        int rounds = 1;
    };

    static int const nDegree = 10;      // outdegree
    static int const nItem = 10;        // number of items
    enum
    {
        nUpdateMS = 700
    };

    using NodeKey = int;    // identifies a consensus participant
    using ItemKey = int;    // identifies a ballot item
    using ItemSet = boost::container::flat_set<ItemKey>;
    using clock_type = std::chrono::system_clock;
    using millis = std::chrono::milliseconds;

    struct Network;

    struct TxMsg
    {
        ItemKey id;
    };

    struct PosMsg
    {
        NodeKey id;
        std::size_t ord;
        std::size_t seq;
        ItemSet items;
        bool last;
    };

    // A pool of items
    // This is the equivalent of the "open ledger"
    class Pool
    {
    private:
        ItemSet items_;

    public:
        // Insert an item into the pool
        void
        insert (ItemKey id)
        {
            items_.insert(id);
        }

        // Returns the items in the pool
        ItemSet const&
        items() const
        {
            return items_;
        }
    };

    // A round of consensus.
    // Each round consists of a series of votes,
    // terminating when a supermajority is reached.
    struct Round
    {
        struct Pos
        {
            ItemSet items;
            bool last = false;
            std::size_t seq = 0;
        };

        int id_;
        Log& log_;
        std::size_t ord_;
        clock_type::time_point t0_;

        int thresh_ = 50;
        bool failed_ = false;
        bool consensus_ = false;
        std::size_t count_ = 0;
        std::unordered_map<NodeKey, Pos> pos_;

        // Create a new round with initial position
        Round (NodeKey id, std::size_t ord,
                ItemSet const& items,
                    clock_type::time_point now,
                Log& log)
            : id_ (id)
            , log_ (log)
            , ord_ (ord)
            , t0_ (now)
        {
            using namespace std;
            pos_[id].items = items;
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
                if (pos.first == id_ ||
                        pos.second.items == items)
                    ++agree;
                else if (! pos.second.last)
                    ++most;
            }
            {
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

    struct Peer
    {
        //beast::aged_unordered_map<

        NodeKey id_;
        std::size_t ord_ = 0;
        std::set<NodeKey> unl_;
        Config const& config_;
        boost::optional<Round> round_;
        millis delay_;
        Network& net_;
        Pool pool_;
        std::unordered_map<ItemKey,
            boost::container::flat_set<Peer*>> item_tab_;

        Peer (int id, Config const& config,
                Network& net)
            : id_ (id)
            , config_ (config)
            , delay_ (millis(
                net.rand(5, 50)))
            , net_ (net)
        {
            unl_.insert(id_); // self
            while(unl_.size() <= config_.unl)
                unl_.insert(net_.rand(config_.peers));
        }

        void
        init()
        {
            net_.timer(millis(2000),
                [&]() { on_close(); });
        }

        // Broadcast a new item
        void
        inject (ItemKey id)
        {
            item_tab_[id].insert(this);
            TxMsg m;
            m.id = id;
            broadcast(m);
        }

        // Closes the pool and starts the round
        void
        on_close()
        {
            round_.emplace(id_, ++ord_, pool_.items(),
                net_.now(), net_.log);
            broadcast(round_->posMsg());
            net_.timer(millis(
                nUpdateMS + net_.rand(nUpdateMS)),
                    [=]() { on_update(); });
        }

        // Updates our position during the round
        void
        on_update()
        {
            if (round_->update(unl_, net_.now()))
                broadcast(round_->posMsg());
            if (round_->consensus_)
                return;
            using namespace std::chrono;
            net_.timer(millis(nUpdateMS),
                [=]() { on_update(); });
        }

        // Called when a transaction is received
        void
        receive (Peer& from, TxMsg const& m)
        {
            auto& seen = item_tab_[m.id];
            if(! seen.empty())
            {
                ++net_.dup;
                return;
            }
            seen.insert(&from);
            net_.timer(net_.now() +
                    millis(net_.rand(200, 600)),
                [&, m]()
                {
                    pool_.insert(m.id);
                    for(auto& link : net_.links(*this))
                        if (seen.count(&link.to) == 0)
                            link.to.send(*this, m);
                });
        }

        // Called when a position is received
        void
        receive (Peer& from,
            std::shared_ptr<PosMsg const> const& m)
        {
            if (round_->receive(*m))
                relay(from, m);
            else
                ++net_.dup;
        }

        //----------------------------------------------------------------------

        // Send a message to this peer
        template <class Message>
        void
        send (Peer& from, Message const& m)
        {
            ++net_.sent;
            net_.send (from, *this,
                [&, m]() { receive(from, m); });
        }

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

        // Broadcast a message to all links
        template <class Message>
        void
        broadcast (Message const& m)
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

        // Relay a message to all links
        template <class Message>
        void
        relay (Peer& from, Message const& m)
        {
            for(auto& link : net_.links(*this))
                if (&link.to != &from)
                    link.to.send(*this, m);
        }
    };

    //--------------------------------------------------------------------------

    // The result of one round
    struct Result
    {
        std::size_t elapsed;
        std::size_t failure = 0;
        std::size_t consensus = 0;
        std::set<ItemSet> sets;
    };

    // The results of several rounds
    struct Results
    {
        std::size_t rounds = 0;
        std::size_t perfect = 0;
        std::vector<std::size_t> elapsed;
        std::vector<std::size_t> failure;
        std::vector<std::size_t> consensus;

        void
        aggregate (Result const& result)
        {
            ++rounds;
            perfect += result.sets.size() == 1;
            elapsed.push_back(result.elapsed);
            failure.push_back(result.failure);
            consensus.push_back(result.consensus);
        }
    };

    struct Report
    {
        std::size_t perfect;
        std::size_t elapsed_min;
        std::size_t elapsed_max;

        Report (Results& results, Config const& config)
        {
            perfect = results.perfect;
#if 0
            std::sort(
                results.elapsed.begin(), results.elapsed.end());
            std::sort(
                results.consensus.begin(), results.consensus.end(),
                    std::greater<std::size_t>{});
#endif
            elapsed_min = results.elapsed.front();
            elapsed_max = results.elapsed.back();
        }
    };

    class Network : public BasicNetwork<Peer>
    {
    private:
        Config const& config_;
        ItemKey seq_ = 0;

    public:
        std::size_t dup = 0;        // total dup
        std::size_t sent = 0;       // total sent
        std::vector<Peer> pv;
        Log& log;

        Network (std::size_t seed,
                Config const& config, Log& log_)
            : config_ (config)
            , log (log_)
        {
            this->rng().seed(seed);
            using namespace std;
            using namespace std::chrono;
            pv.reserve(config.peers);
            for(std::size_t id = 0; id < config_.peers; ++id)
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
                if (this->connect(from,
                        pv[this->rand(pv.size())], delay))
                    break;
        }

        void
        report (std::size_t n,
            millis ms, Log& log)
        {
            std::size_t failed = 0;
            std::size_t consensus = 0;
            std::set<ItemSet> unique;
            for(auto const& p : pv)
            {
                if (! p.round_)
                    continue;
                unique.insert(p.round_->items());
                if (p.round_->consensus_)
                    ++consensus;
                if (p.round_->failed_)
                    ++failed;
            }
            log <<
                n << "\t" <<
                unique.size() << "\t" <<
                consensus << "\t" <<
                failed << "\t" <<
                ms.count() << "ms\t" <<
                sent << "\t" <<
                dup;
        }

        // Inject a random item
        void
        inject()
        {
            pv[this->rand(pv.size())].inject(++seq_);
        }

        void
        on_timer()
        {
            inject();
            if(this->now().time_since_epoch() <=
                    std::chrono::seconds(4))
                this->timer(millis(250),
                    [&]() { on_timer(); });
        }

        // Execute a round of consensus
        void
        run (std::size_t n)
        {
            using namespace std::chrono;
            for(int i = 0; i < config_.peers; ++i)
                pv[i].init();
            inject();
            this->timer(millis(250),
                [&]() { on_timer(); });
            auto const t0 = this->now();
    #if 0
            do
            {
                report(n, duration_cast<
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
        log << "Sim4" << ":";
        log <<
            "n\t" <<
            "unique\t" <<
            "consensus\t" <<
            "failed\t" <<
            "time\t" <<
            "sent\t" <<
            "dup";
        Config config;
        for(auto i = 1; i <= config.trials; ++i)
        {
            Network net(i, config, log);
            for(auto j = 1; j <= config.rounds; ++j)
                net.run(i);
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
