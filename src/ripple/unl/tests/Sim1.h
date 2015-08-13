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

#ifndef RIPPLE_UNL_SIM1_H_INCLUDED
#define RIPPLE_UNL_SIM1_H_INCLUDED

#include <ripple/unl/tests/BasicNetwork.h>
#include <boost/optional.hpp>
#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace ripple {
namespace test {

struct Sim1
{
    struct Config
    {
    };

    static int const nPeer = 100;       // # of peers
    static int const nDegree = 10;      // outdegree
    static int const nTrial = 10;       // number of trials
    static int const nRound = 1;        // number of rounds
    static int const nUNLMin = 20;
    static int const nUNLMax = 30;

    using clock_type = std::chrono::system_clock;

    struct Network;

    // A round of consensus.
    // Each round consists of a series of votes,
    // terminating when a supermajority is reached.
    class Round
    {
    public:
        static int const nPercent = 80;     // % of agreement

        int id_;
        bool consensus_ = false;
        std::unordered_map<int, std::pair<
            std::size_t, bool>> pos_;
        std::size_t count_ = 0;
        clock_type::time_point t0_;

    public:
        // Create a new round with initial position
        Round (int id, bool value,
                    clock_type::time_point now)
            : id_ (id)
            , t0_ (now)
        {
            pos_.emplace(std::make_pair(id,
                std::make_pair(0, value)));
        }

        // Returns our value
        bool
        value() const
        {
            auto const iter = pos_.find(id_);
            return iter->second.second;
        }

        // Return our position
        // This increments the sequence number
        std::pair<std::size_t, bool>
        pos()
        {
            auto const iter = pos_.find(id_);
            return { ++iter->second.first,
                iter->second.second };
        }

        // Update a peer's position
        // Return `true` if we should relay
        bool
        receive (int id,
            std::size_t seq, bool value)
        {
            if (id == id_)
                return false;
            auto const result = pos_.emplace(
                std::make_pair(id,
                    std::make_pair(seq, value)));
            if (! result.second && seq <=
                    result.first->second.first)
                return false;
            result.first->second.first = seq;
            result.first->second.second = value;
            return true;
        }

        // Update our position
        // Return `true` if we changed our position
        template <class UNL>
        bool
        update (UNL const& unl,
            clock_type::time_point const& now)
        {
            if (consensus_)
                return false;
            ++count_;
            std::array<std::size_t, 2> v;
            v.fill(0);
            for(auto const& p : pos_)
                if (p.first == id_ ||
                        unl.count(p.first) > 0)
                    ++v[p.second.second];
            using namespace std::chrono;
            auto const iter = pos_.find(id_);
            auto const super =
                ((unl.size() * nPercent) + 50) / 100;
            if (v[0] >= super || v[1] >= super)
                consensus_ = true;
            // agree to disagree
            v[0] += duration_cast<milliseconds>(
                now - t0_).count() / 250;
            if (v[0] >= v[1])
            {
                if (iter->second.second != false)
                {
                    iter->second.second = false;
                    return true;
                }
            }
            else
            {
                if (iter->second.second != true)
                {
                    iter->second.second = true;
                    return true;
                }
            }
            return false;
        }
    };

    //--------------------------------------------------------------------------

    class Peer
    {
    private:
        struct PosMsg
        {
            int id;
            std::size_t seq;
            bool value;        // position
        };
        
    public:
        int id_;
        std::set<int> unl_;
        Config const& config_;
        boost::optional<Round> round_;
        std::chrono::milliseconds delay_;
        Network& net_;

        Peer (int id, Config const& config,
                Network& net)
            : id_(id)
            , config_ (config)
            , delay_(std::chrono::milliseconds(
                net.rand(5, 50)))
            , net_(net)
        {
            auto const size = net_.rand(
                nUNLMin, nUNLMax + 1);
            while(unl_.size() < size)
            {
                unl_.insert(net_.rand(nPeer));
                unl_.erase(id_);
            }
        }

        // Called to begin the round
        void
        start()
        {
            round_.emplace(id_,
                !(id_%3), net_.now());
            ++round_->count_;
            PosMsg m;
            m.id = id_;
            std::tie(m.seq, m.value) =
                round_->pos();
            broadcast(m);
            using namespace std::chrono;
            net_.timer(milliseconds(
                700 + net_.rand(700)),
                    [=]() { timer(); });
        }

        void
        receive (Peer& from, PosMsg const& m)
        {
            if (round_->receive(m.id,
                    m.seq, m.value))
                relay(from, m);
            else
                ++net_.dup;
        }

        void
        timer()
        {
            if (round_->update(unl_, net_.now()))
            {
                PosMsg m;
                m.id = id_;
                std::tie(m.seq, m.value) =
                    round_->pos();
                broadcast(m);
            }
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

    struct Network : BasicNetwork<Peer>
    {
        std::size_t dup = 0;        // total dup
        std::size_t sent = 0;       // total sent
        std::vector<Peer> pv;

        Network (std::size_t seed,
            Config const& config)
        {
            this->rng().seed(seed);
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
                milliseconds(rand(5, 200));
            for(;;)
                if (connect(from,
                        pv[rand(pv.size())], delay))
                    break;
        }

        template <class Log>
        void
        report (std::chrono::milliseconds ms, Log& log)
        {
            std::array<std::size_t, 2> n;
            std::vector<std::size_t> count;
            n.fill(0);
            std::size_t consensus = 0;
            for(auto const& p : pv)
            {
                ++n[p.round_->value()];
                ++nth(count, p.round_->count_);
                if (p.round_->consensus_)
                    ++consensus;
            }
            log <<
                n[1] << "/" << n[0] << ", " <<
                "consensus: " << consensus << " in " <<
                ms.count() << "ms, " <<
                "sent: " << sent << ", " <<
                "dup: " << dup << ", " <<
                "count: " << seq_string(count);
        }

        // Execute a round of consensus
        template <class Log>
        void
        round (Log& log)
        {
            using namespace std::chrono;
            for(int i = 0; i < nPeer; ++i)
                pv[i].start();
            auto const t0 = now();
#if 0
            do
            {
                report(duration_cast<
                    milliseconds>(now() - t0), log);
            }
            while (step_for(milliseconds(50)));
#else
            step();
#endif
            report(duration_cast<
                milliseconds>(now() - t0), log);
        }
    };

    template <class Log>
    static
    void
    run (Log& log)
    {
        log << "Sim1" << ":";
        Config config;
        for(auto i = 1; i <= nTrial; ++i)
        {
            Network net(i, config);
            for(auto j = 1; j <= nRound; ++j)
                net.round(log);
        }
    }
};

} // test
} // ripple

#endif
