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

#include <test/csf/Scheduler.h>
#include <test/csf/Digraph.h>

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
    at either end of the connection will not be delivered.

    When creating the Peer set, the caller needs to provide a
    Scheduler object for managing the the timing and delivery
    of messages. After constructing the network, and establishing
    connections, the caller uses the scheduler's step_* functions
    to drive messages through the network.

    The graph of peers and connections is internally represented
    using Digraph<Peer,BasicNetwork::link_type>. Clients have
    const access to that graph to perform additional operations not
    directly provided by BasicNetwork.

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
    using peer_type = Peer;

    using clock_type = Scheduler::clock_type;

    using duration = typename clock_type::duration;

    using time_point = typename clock_type::time_point;

    struct link_type
    {
        bool inbound = false;
        duration delay{};
        time_point established{};
        link_type() = default;
        link_type(bool inbound_, duration delay_, time_point established_)
            : inbound(inbound_), delay(delay_), established(established_)
        {
        }
    };

    Scheduler& scheduler;
    Digraph<Peer, link_type> links_;

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


    /** Return the range of active links.

        @return A random access range over Digraph::Edge instances
    */
    auto
    links(Peer const& from)
    {
        return links_.outEdges(from);
    }

    /** Return the underlying digraph
    */
    Digraph<Peer, link_type> const &
    graph() const
    {
        return links_;
    }
};
//------------------------------------------------------------------------------
template <class Peer>
BasicNetwork<Peer>::BasicNetwork(Scheduler& s) : scheduler(s)
{
}

template <class Peer>
inline bool
BasicNetwork<Peer>::connect(
    Peer const& from,
    Peer const& to,
    duration const& delay)
{
    if (to == from)
        return false;
    time_point const now = scheduler.now();
    if(!links_.connect(from, to, link_type{false, delay, now}))
        return false;
    auto const result = links_.connect(to, from, link_type{true, delay, now});
    (void)result;
    assert(result);
    return true;
}

template <class Peer>
inline bool
BasicNetwork<Peer>::disconnect(Peer const& peer1, Peer const& peer2)
{
    if (! links_.disconnect(peer1, peer2))
        return false;
    bool r = links_.disconnect(peer2, peer1);
    (void)r;
    assert(r);
    return true;
}

template <class Peer>
template <class Function>
inline void
BasicNetwork<Peer>::send(Peer const& from, Peer const& to, Function&& f)
{
    auto link = links_.edge(from,to);
    if(!link)
        return;
    time_point const sent = scheduler.now();
    scheduler.in(
        link->delay,
        [ from, to, sent, f = std::forward<Function>(f), this ] {
            // only process if still connected and connection was
            // not broken since the message was sent
            auto link = links_.edge(from, to);
            if (link && link->established <= sent)
                f();
        });
}

}  // namespace csf
}  // namespace test
}  // namespace ripple

#endif
