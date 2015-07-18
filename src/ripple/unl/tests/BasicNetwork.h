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

#ifndef RIPPLE_SIM_BASICNETWORK_H_INCLUDED
#define RIPPLE_SIM_BASICNETWORK_H_INCLUDED

#include <beast/chrono/manual_clock.h>
#include <beast/hash/hash_append.h>
#include <beast/hash/uhash.h>
#include <boost/container/flat_map.hpp>
#include <boost/bimap.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <boost/range/iterator_range.hpp>
#include <boost/tuple/tuple.hpp>
#include <deque>
#include <beast/cxx14/memory.h> // <memory>
#include <cassert>
#include <unordered_map>
#include <unordered_set>

namespace ripple {
namespace test {

template <class Peer, class Derived>
class BasicNetwork
{
public:
    using peer_type = Peer;

    using clock_type =
        beast::manual_clock<
            std::chrono::system_clock>;

    using duration =
        typename clock_type::duration;

    using time_point =
        typename clock_type::time_point;

private:
    struct invoke
    {
        virtual ~invoke() = default;
        virtual void operator()(Peer& from) const = 0;
    };

    template <class Message>
    class invoke_impl;

    struct msg
    {
        msg (Peer& from_, Peer& to_, time_point when_,
                std::unique_ptr<invoke> op_)
            : to(&to_), from(&from_),
                when(when_), op(std::move(op_))
        {
        }

        Peer* to;
        Peer* from;
        time_point when;
        std::unique_ptr<invoke> mutable op;
    };

    struct link_type
    {
        bool inbound;
        duration delay;

        link_type (bool inbound_,
                duration delay_)
            : inbound (inbound_)
            , delay (delay_)
        {
        }
    };

    using msg_list = boost::multi_index_container<msg,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_non_unique<
                boost::multi_index::member<msg, Peer*, &msg::to>>,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::member<msg, Peer*, &msg::from>>,
            boost::multi_index::ordered_non_unique<
                boost::multi_index::member<msg, time_point, &msg::when>>>>;

    using links_type =
        boost::container::flat_map<Peer*, link_type>;

    class link_transform
    {
    public:
        using argument_type =
            typename links_type::value_type;

        class result_type
        {
        public:
            Peer& to;
            bool inbound;

            result_type (result_type const&) = default;

            result_type (BasicNetwork& net, Peer& from,
                    Peer& to_, bool inbound_)
                : to(to_)
                , inbound(inbound_)
                , net_(net)
                , from_(from)
            {
            }

            /** Disconnect this link.

                Effects:

                    The connection is removed at both ends.
            */
            void
            disconnect() const
            {
                net_.disconnect(from_, to);
            }

        private:
            BasicNetwork& net_;
            Peer& from_;
        };

        link_transform (BasicNetwork& net, Peer& from)
            : net_(net)
            , from_(from)
        {
        }

        result_type const
        operator()(argument_type const& v) const
        {
            return result_type(net_, from_,
                *v.first, v.second.inbound);
        }

    private:
        BasicNetwork& net_;
        Peer& from_;
    };

    struct peer_transform
    {
        using argument_type =
            typename links_type::value_type;

        using result_type = Peer&;

        result_type
        operator()(argument_type const& v) const
        {
            return *v.first;
        }
    };

    msg_list msgs_;
    clock_type clock_;
    std::unordered_map<Peer*, links_type> links_;

public:
    /** Connect two peers.

        The link is directed, with `from` establishing
        the outbound connection and `to` receiving the
        incoming connection.

        Preconditions:

            &from != &to (self connect disallowed).

            A link between from and to does not
            already exist (duplicates disallowed).

        Effects:

            Creates a link between from and to.

        @param `from` The source of the outgoing connection
        @param `to` The recipient of the incoming connection
        @param `delay` The time delay of all delivered messages
        @return `true` if a new connection was established
    */
    bool
    connect (Peer& from, Peer& to,
        duration const& delay = std::chrono::seconds{0});

    /** Break a link.

        Effects:

            If a connection is present, both ends are
            disconnected.
            
            Any pending messages on the connection
            are discarded.

        @return `true` if a connection was broken.
    */
    bool
    disconnect (Peer& peer1, Peer& peer2);

    /** Return the range of active links.

        @return A random access range.
    */
    boost::transformed_range<
        link_transform, links_type>
    links (Peer& from);

    /** Returns the range of connected peers.

        @return A random access range.
    */
    boost::transformed_range<
        peer_transform, links_type>
    peers (Peer& from);

    /** Send a message to a peer.

        Preconditions:

            A link exists between from and to.

        Effects:

            If the link is not broken when the 
            link's `delay` time has elapsed,
            to.receive() will be called with the
            message.

        The peer will be called with this signature:

            void Peer::receive(Net&, Peer& from, Message&&)
    */
    template <class Message>
    void
    send (Peer& from, Peer& to, Message&& m);

    /** Perform breadth-first search.
        
        Function will be called with this signature:

            void(Derived&, std::size_t, Peer&);

        The second argument is the distance of the
        peer from the start peer, in hops.
    */
    template <class Function>
    void
    bfs (Peer& start, Function&& f);

    /** Run the network for up to one message.

        Effects:

            The clock is advanced to the time
            of the last delivered message.

        @return `true` if a message was processed.
    */
    bool
    run_one();

    /** Run the network until no messages remain.

        Effects:

            The clock is advanced to the time
            of the last delivered message.

        @return `true` if any message was processed.
    */
    bool
    run();

    /** Run the network until the specified time.

        Effects:

            The clock is advanced to the
            specified time.

        @return `true` if any message was processed.
    */
    bool
    run_until (time_point const& until);

    /** Run the network until time has elapsed.

        Effects:

            The clock is advanced by the
            specified duration.

        @return `true` if any message was processed.
    */
    template <class Period, class Rep>
    bool
    run_for (std::chrono::duration<
        Period, Rep> const& amount);

private:
    Derived&
    derived()
    {
        return *static_cast<Derived*>(this);
    }
};

//------------------------------------------------------------------------------

template <class Peer, class Derived>
template <class Message>
class BasicNetwork<Peer,Derived>::invoke_impl
    : public invoke
{
public:
    invoke_impl (Derived& net,
            Peer& peer, Message&& m)
        : peer_(peer)
        , net_(net)
        , m_(std::forward<Message>(m))
    {
    }

    void
    operator()(Peer& from) const override
    {
        peer_.receive(
            net_, from, std::move(m_));
    };

private:
    Peer& peer_;
    Derived& net_;
    std::decay_t<Message> mutable m_;
};

//------------------------------------------------------------------------------

template <class Peer, class Derived>
bool
BasicNetwork<Peer, Derived>::connect(
    Peer& from, Peer& to, duration const& delay)
{
    if (&to == &from)
        return false;
    using namespace std;
    if (! links_[&from].emplace(&to,
            link_type{ false, delay }).second)
        return false;
    auto const result = links_[&to].emplace(
        &from, link_type{ true, delay });
    (void)result;
    assert(result.second);
    return true;
}

template <class Peer, class Derived>
bool
BasicNetwork<Peer, Derived>::disconnect(
    Peer& peer1, Peer& peer2)
{
    if (links_[&peer1].erase(&peer2) == 0)
        return false;
    auto const n =
        links_[&peer2].erase(&peer1);
    (void)n;
    assert(n);
    using boost::multi_index::get;
    {
        auto& by_to = get<0>(msgs_);
        auto const r = by_to.equal_range(&peer1);
        by_to.erase(r.first, r.second);
    }
    {
        auto& by_from = get<1>(msgs_);
        auto const r = by_from.equal_range(&peer2);
        by_from.erase(r.first, r.second);
    }
    return true;
}

template <class Peer, class Derived>
inline
auto
BasicNetwork<Peer, Derived>::links(
    Peer& from) ->
        boost::transformed_range<
            link_transform, links_type>
{
    return boost::adaptors::transform(
        links_[&from],
            link_transform{ *this, from });
}

template <class Peer, class Derived>
inline
auto
BasicNetwork<Peer, Derived>::peers(
    Peer& from) ->
        boost::transformed_range<
            peer_transform, links_type>
{
    return boost::adaptors::transform(
        links_[&from], peer_transform{});
}

template <class Peer, class Derived>
template <class Message>
inline
void
BasicNetwork<Peer, Derived>::send(
    Peer& from, Peer& to, Message&& m)
{
    auto const iter = links_[&from].find(&to);
    msgs_.emplace(from, to, clock_.now() + iter->second.delay,
        std::make_unique<invoke_impl<Message>>(
            derived(), to, std::forward<Message>(m)));
}

template <class Peer, class Derived>
bool
BasicNetwork<Peer, Derived>::run_one()
{
    using boost::multi_index::get;
    auto& by_when = get<2>(msgs_);
    if (by_when.empty())
        return false;
    auto const iter = by_when.begin();
    auto const from = iter->from;
    auto const op = std::move(iter->op);
    clock_.set(iter->when);
    by_when.erase(iter);
    (*op)(*from);
    return true;
}

template <class Peer, class Derived>
bool
BasicNetwork<Peer, Derived>::run()
{
    if (! run_one())
        return false;
    for(;;)
        if (! run_one())
            break;
    return true;
}

template <class Peer, class Derived>
bool
BasicNetwork<Peer, Derived>::run_until(
    time_point const& until)
{
    using boost::multi_index::get;
    auto& by_when = get<2>(msgs_);
    if(by_when.empty() ||
        by_when.begin()->when > until)
    {
        clock_.set(until);
        return false;
    }
    do
    {
        run_one();
    }
    while(! by_when.empty() &&
        by_when.begin()->when <= until);
    clock_.set(until);
    return true;
}

template <class Peer, class Derived>
template <class Period, class Rep>
bool
BasicNetwork<Peer, Derived>::run_for(
    std::chrono::duration<Period, Rep> const& amount)
{
    return run_until(
        clock_.now() + amount);
}

template <class Peer, class Derived>
template <class Function>
void
BasicNetwork<Peer, Derived>::bfs(
    Peer& start, Function&& f)
{
    std::deque<std::pair<Peer*, std::size_t>> q;
    std::unordered_set<Peer*> seen;
    q.emplace_back(&start, 0);
    seen.insert(&start);
    while(! q.empty())
    {
        auto v = q.front();
        q.pop_front();
        f(derived(), v.second, *v.first);
        for(auto const& link : links_[v.first])
        {
            auto w = link.first;
            if (seen.count(w) == 0)
            {
                q.emplace_back(w, v.second + 1);
                seen.insert(w);
            }
        }
    }
}

//------------------------------------------------------------------------------

template <class Peer>
struct SimpleNetwork
    : BasicNetwork<Peer, SimpleNetwork<Peer>>
{
};

template <class FwdRange>
std::string
seq_string (FwdRange const& r)
{
    std::stringstream ss;
    auto iter = std::begin(r);
    if (iter == std::end(r))
        return ss.str();
    ss << *iter++;
    while(iter != std::end(r))
        ss << ", " << *iter++;
    return ss.str();
}

template <class FwdRange>
typename FwdRange::value_type
seq_sum (FwdRange const& r)
{
    typename FwdRange::value_type sum = 0;
    for (auto const& n : r)
        sum += n;
    return sum;
}

template <class RanRange>
double
diameter (RanRange const& r)
{
    if (r.empty())
        return 0;
    if (r.size() == 1)
        return r.front();
    auto h0 = *(r.end() - 2);
    auto h1 = r.back();
    return (r.size() - 2) +
        double(h1) / (h0 + h1);
}

template <class Container>
typename Container::value_type&
nth (Container& c, std::size_t n)
{
    c.resize(std::max(c.size(), n + 1));
    return c[n];
}

} // test
} // ripple

#endif
