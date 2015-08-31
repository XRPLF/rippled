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

#ifndef RIPPLE_UNL_SIM3_H_INCLUDED
#define RIPPLE_UNL_SIM3_H_INCLUDED

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
struct Sim3
{
    struct Config
    {
        int unl;
        int peers = 100;
        int trial = 100;
    };

    static int const nDegree = 10;      // outdegree
    static int const nItem = 10;        // number of items
    static int const nUpdateMS = 700;

    using NodeKey = int;    // identifies a consensus participant
    using ItemKey = int;    // identifies a ballot item
    using ItemSet = boost::container::flat_set<ItemKey>;
    using clock_type = std::chrono::system_clock;
    using millis = std::chrono::milliseconds;

    class Network;

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

        NodeKey id_;
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
            #if 1
                thresh_ += 5;
            #endif
            #if 0
                // This causes occasional byzantine
                // failure in a large number of nodes
                if (thresh_ > 80)
                    thresh_ = 80;
            #endif
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

    class Peer
    {
    private:
        //beast::aged_unordered_map<

    public:
        NodeKey id_;
        std::set<NodeKey> unl_;
        Config const& config_;
        boost::optional<Round> round_;
        millis delay_;
        Network& net_;

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

        // Called to begin the round
        void
        start()
        {
            {
                ItemSet pos;
                for(int i = 0; i < nItem; ++i)
                    if (net_.rand(2))
                        pos.insert(i);
                round_.emplace(id_, std::move(pos),
                    net_.now(), net_.log);
            }
            broadcast(round_->posMsg());
            using namespace std::chrono;
            net_.timer(milliseconds(
                nUpdateMS + net_.rand(nUpdateMS)),
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
            net_.timer(milliseconds(nUpdateMS),
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
            this->rng.seed(seed);
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
                if (connect(from,
                        pv[this->rand(pv.size())], delay))
                    break;
        }

        // Execute one round of consensus
        Result
        run()
        {
            Result result;
            using namespace std::chrono;
            for(int i = 0; i < config_.peers; ++i)
                pv[i].start();
            auto const t0 = this->now();
            this->step();
            result.elapsed = duration_cast<
                millis>(this->now() - t0).count();
            for(auto const& p : pv)
            {
                if (p.round_->failed_)
                    ++result.failure;
                if (p.round_->consensus_)
                {
                    ++result.consensus;
                    result.sets.insert(p.round_->items());
                }
            }
            return result;
        }
    };

    static
    void
    report (Log& log, Result const& result,
        Config const& config)
    {
        log <<
            result.elapsed << "\t" <<
            result.failure << "\t" <<
            result.consensus << "\t" <<
            result.sets.size();
            ;
    }

    static
    void
    report (Log& log, Report const& report,
        Config const& config)
    {
        log <<
            report.perfect << "\t" <<
            report.elapsed_min << "\t" <<
            report.elapsed_max << "\t" <<
            config.peers << "\t" <<
            config.unl << "\t" <<
            config.trial
            ;
    }

    static
    void
    run (Log& log)
    {
        log << "Sim3" << ":";
#if 1
        log <<
            "perfect\t" <<
            "elapsed_min\t" <<
            "elapsed_max\t" <<
            "peers\t" <<
            "unl\t" <<
            "trial\t"
            ;
#else
        log <<
            "elapsed\t" <<
            "failure\t" <<
            "consensus\t" <<
            "positions\t"
            ;
#endif
        for (int unl = 40; unl > 5; --unl)
        {
            Results results;
            Config config;
            config.unl = unl;
            for(auto i = 1; i <= config.trial; ++i)
            {
                Network net(i, config, log);
                //report(log, net.run(), config);
                results.aggregate(net.run());
            }
            report(log, Report(results, config), config);
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
