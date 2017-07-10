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

#ifndef RIPPLE_TEST_CSF_BASICNETWORK_H_INCLUDED
#define RIPPLE_TEST_CSF_BASICNETWORK_H_INCLUDED

#include <ripple/beast/hash/hash_append.h>
#include <ripple/beast/hash/uhash.h>
#include <boost/container/flat_map.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <boost/range/iterator_range.hpp>
#include <cassert>
#include <cstdint>
#include <deque>
#include <test/csf/Scheduler.h>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace ripple {
namespace test {
namespace csf {
/** Peer to peer network simulator.

    The network is formed from a set of Peer objects representing
    vertices and configurable connections representing edges.
    The caller is responsible for creating the Peer objects ahead
    of time.

    Peer objects cannot be destroyed once the BasicNetwork is
    constructed. To handle peers going online and offline,
    callers can simply disconnect all links and reconnect them
    later. Connections are directed, one end is the inbound
    Peer and the other is the outbound Peer.

    Peers may send messages along their connections. To simulate
    the effects of latency, these messages can be delayed by a
    configurable duration set when the link is established.
    Messages always arrive in the order they were sent on a
    particular connection.

    A message is modeled using a lambda function. The caller
    provides the code to execute upon delivery of the message.
    If a Peer is disconnected, all messages pending delivery
    at either end of the associated connection are discarded.

    When creating the Peer set, the caller needs to provide a Scheduler object
    for managing the the timing and delivery of messages. After constructing
    the network, and establishing connections, the caller uses the scheduler's
    step_* functions to iterate the network,

    Peer Requirements:

        Peer should be a lightweight type, cheap to copy
        and/or move. A good candidate is a simple pointer to
        the underlying user defined type in the simulation.

        Expression      Type        Requirements
        ----------      ----        ------------
        P               Peer
        u, v                        Values of type P
        P u(v)                      CopyConstructible
        u.~P()                      Destructible
        u == v          bool        EqualityComparable
        u < v           bool        LessThanComparable
        std::hash<P>    class       std::hash is defined for P
        ! u             bool        true if u is not-a-peer

*/
template <class Peer>
class BasicNetwork
{
public:
    using peer_type = Peer;

    using clock_type = Scheduler::clock_type;

    using duration = typename clock_type::duration;

    using time_point = typename clock_type::time_point;

private:
    struct link_type
    {
        bool inbound;
        duration delay;

        link_type(bool inbound_, duration delay_)
            : inbound(inbound_), delay(delay_)
        {
        }
    };

    using links_type = boost::container::flat_map<Peer, link_type>;

    class link_transform;

    Scheduler& scheduler;
    std::unordered_map<Peer, links_type> links_;

public:
    BasicNetwork(BasicNetwork const&) = delete;
    BasicNetwork&
    operator=(BasicNetwork const&) = delete;

    BasicNetwork(Scheduler& s);

    /** Connect two peers.

        The link is directed, with `from` establishing
        the outbound connection and `to` receiving the
        incoming connection.

        Preconditions:

            from != to (self connect disallowed).

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
    connect(
        Peer const& from,
        Peer const& to,
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
    disconnect(Peer const& peer1, Peer const& peer2);

    /** Check if peers are connected.

        This checks that peer1 has an outbound connection to peer2.

        @return `true` if peer1 is connected to peer2 and peer1 != peer2
    */
    bool
    connected(Peer const& peer1, Peer const& peer2);

    /** Return the range of active links.

        @return A random access range.
    */
    boost::transformed_range<link_transform, links_type>
    links(Peer const& from);

    /** Send a message to a peer.

        Preconditions:

            A link exists between from and to.

        Effects:

            If the link is not broken when the
            link's `delay` time has elapsed,
            the function will be invoked with
            no arguments.

        @note Its the caller's responsibility to
        ensure that the body of the function performs
        activity consistent with `from`'s receipt of
        a message from `to`.
    */
    template <class Function>
    void
    send(Peer const& from, Peer const& to, Function&& f);

    /** Perform breadth-first search.

        Function will be called with this signature:

            void(std::size_t, Peer&);

        The second argument is the distance of the
        peer from the start peer, in hops.
    */
    template <class Function>
    void
    bfs(Peer const& start, Function&& f);
};

//------------------------------------------------------------------------------
template <class Peer>
class BasicNetwork<Peer>::link_transform
{
private:
    BasicNetwork& net_;
    Peer from_;

public:
    using argument_type = typename links_type::value_type;

    class result_type
    {
    public:
        Peer to;
        bool inbound;

        result_type(result_type const&) = default;

        result_type(
            BasicNetwork& net,
            Peer const& from,
            Peer const& to_,
            bool inbound_)
            : to(to_), inbound(inbound_), net_(net), from_(from)
        {
        }

        /** Disconnect this link.

            Effects:

                The connection is removed at both ends.
        */
        bool
        disconnect() const
        {
            return net_.disconnect(from_, to);
        }

    private:
        BasicNetwork& net_;
        Peer from_;
    };

    link_transform(BasicNetwork& net, Peer const& from) : net_(net), from_(from)
    {
    }

    result_type const
    operator()(argument_type const& v) const
    {
        return result_type(net_, from_, v.first, v.second.inbound);
    }
};

//------------------------------------------------------------------------------
template <class Peer>
BasicNetwork<Peer>::BasicNetwork(Scheduler& s) : scheduler(s)
{
}

template <class Peer>
bool
BasicNetwork<Peer>::connect(
    Peer const& from,
    Peer const& to,
    duration const& delay)
{
    if (to == from)
        return false;
    using namespace std;
    if (!links_[from].emplace(to, link_type{false, delay}).second)
        return false;
    auto const result = links_[to].emplace(from, link_type{true, delay});
    (void)result;
    assert(result.second);
    return true;
}

template <class Peer>
bool
BasicNetwork<Peer>::connected(Peer const& from, Peer const& to)
{
    auto& froms = links_[from];
    auto it = froms.find(to);
    return it != froms.end() && !it->second.inbound;
}

template <class Peer>
bool
BasicNetwork<Peer>::disconnect(Peer const& peer1, Peer const& peer2)
{
    if (links_[peer1].erase(peer2) == 0)
        return false;
    auto const n = links_[peer2].erase(peer1);
    (void)n;
    assert(n);
    auto it = scheduler.queue_.begin();
    while (it != scheduler.queue_.end())
    {
        if (it->drop())
            it = scheduler.queue_.erase(it);
        else
            ++it;
    }
    return true;
}

template <class Peer>
inline auto
BasicNetwork<Peer>::links(Peer const& from)
    -> boost::transformed_range<link_transform, links_type>
{
    return boost::adaptors::transform(
        links_[from], link_transform{*this, from});
}

template <class Peer>
template <class Function>
inline void
BasicNetwork<Peer>::send(Peer const& from, Peer const& to, Function&& f)
{
    auto const iter = links_[from].find(to);

    // Schedule with droppable condition on disconnect
    scheduler.queue_.emplace(
        scheduler.now() + iter->second.delay,
        std::forward<Function>(f),
        [from, to, this] { return !connected(from, to); });
}

template <class Peer>
template <class Function>
void
BasicNetwork<Peer>::bfs(Peer const& start, Function&& f)
{
    std::deque<std::pair<Peer, std::size_t>> q;
    std::unordered_set<Peer> seen;
    q.emplace_back(start, 0);
    seen.insert(start);
    while (!q.empty())
    {
        auto v = q.front();
        q.pop_front();
        f(v.second, v.first);
        for (auto const& link : links_[v.first])
        {
            auto const& w = link.first;
            if (seen.count(w) == 0)
            {
                q.emplace_back(w, v.second + 1);
                seen.insert(w);
            }
        }
    }
}

}  // namespace csf
}  // namespace test
}  // namespace ripple

#endif
