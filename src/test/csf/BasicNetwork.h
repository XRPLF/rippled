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

#include <ripple/basics/qalloc.h>
#include <ripple/beast/clock/manual_clock.h>
#include <ripple/beast/hash/hash_append.h>
#include <ripple/beast/hash/uhash.h>
#include <boost/container/flat_map.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <boost/range/iterator_range.hpp>
#include <boost/tuple/tuple.hpp>
#include <deque>
#include <memory>
#include <cassert>
#include <cstdint>
#include <iomanip>
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

    A timer may be set for a Peer. When the timer expires,
    a caller provided lambda is invoked. Timers may be canceled
    using a token returned when the timer is created.

    After creating the Peer set, constructing the network,
    and establishing connections, the caller uses one or more
    of the step, step_one, step_for, step_until and step_while
    functions to iterate the network,

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

    using clock_type =
        beast::manual_clock<
            std::chrono::steady_clock>;

    using duration =
        typename clock_type::duration;

    using time_point =
        typename clock_type::time_point;

private:
    struct by_to_tag {};
    struct by_from_tag {};
    struct by_when_tag {};

    using by_to_hook =
        boost::intrusive::list_base_hook<
            boost::intrusive::link_mode<
                boost::intrusive::normal_link>,
            boost::intrusive::tag<by_to_tag>>;

    using by_from_hook =
        boost::intrusive::list_base_hook<
            boost::intrusive::link_mode<
                boost::intrusive::normal_link>,
            boost::intrusive::tag<by_from_tag>>;

    using by_when_hook =
        boost::intrusive::set_base_hook<
            boost::intrusive::link_mode<
                boost::intrusive::normal_link>>;

    struct msg
        : by_to_hook, by_from_hook, by_when_hook
    {
        Peer to;
        Peer from;
        time_point when;

        msg (msg const&) = delete;
        msg& operator= (msg const&) = delete;
        virtual ~msg() = default;
        virtual void operator()() const = 0;

        msg (Peer const& from_, Peer const& to_,
                time_point when_)
            : to(to_), from(from_), when(when_)
        {
        }

        bool
        operator< (msg const& other) const
        {
            return when < other.when;
        }
    };

    template <class Handler>
    class msg_impl : public msg
    {
    private:
        Handler const h_;

    public:
        msg_impl (msg_impl const&) = delete;
        msg_impl& operator= (msg_impl const&) = delete;

        msg_impl (Peer const& from_, Peer const& to_,
                time_point when_, Handler&& h)
            : msg (from_, to_, when_)
            , h_ (std::move(h))
        {
        }

        msg_impl (Peer const& from_, Peer const& to_,
                time_point when_, Handler const& h)
            : msg (from_, to_, when_)
            , h_ (h)
        {
        }

        void operator()() const override
        {
            h_();
        }
    };

    class queue_type
    {
    private:
        using by_to_list = typename
            boost::intrusive::make_list<msg,
                boost::intrusive::base_hook<by_to_hook>,
                    boost::intrusive::constant_time_size<false>>::type;

        using by_from_list = typename
            boost::intrusive::make_list<msg,
                boost::intrusive::base_hook<by_from_hook>,
                    boost::intrusive::constant_time_size<false>>::type;

        using by_when_set = typename
            boost::intrusive::make_multiset<msg,
                boost::intrusive::constant_time_size<false>>::type;

        qalloc alloc_;
        by_when_set by_when_;
        std::unordered_map<Peer, by_to_list> by_to_;
        std::unordered_map<Peer, by_from_list> by_from_;

    public:
        using iterator =
            typename by_when_set::iterator;

        queue_type (queue_type const&) = delete;
        queue_type& operator= (queue_type const&) = delete;

        explicit
        queue_type (qalloc const& alloc);

        ~queue_type();

        bool
        empty() const;

        iterator
        begin();

        iterator
        end();

        template <class Handler>
        typename by_when_set::iterator
        emplace (Peer const& from, Peer const& to,
            time_point when, Handler&& h);

        void
        erase (iterator iter);

        void
        remove (Peer const& from, Peer const& to);
    };

    struct link_type
    {
        bool inbound;
        duration delay;

        link_type (bool inbound_, duration delay_)
            : inbound (inbound_)
            , delay (delay_)
        {
        }
    };

    using links_type =
        boost::container::flat_map<Peer, link_type>;

    class link_transform;

    qalloc alloc_;
    queue_type queue_;
    // VFALCO This is an ugly wart, aged containers
    //        want a non-const reference to a clock.
    clock_type mutable clock_;
    std::unordered_map<Peer, links_type> links_;

public:
    BasicNetwork (BasicNetwork const&) = delete;
    BasicNetwork& operator= (BasicNetwork const&) = delete;

    BasicNetwork();

    /** Return the allocator. */
    qalloc const&
    alloc() const;

    /** Return the clock. */
    clock_type&
    clock() const;

    /** Return the current network time.

        @note The epoch is unspecified
    */
    time_point
    now() const;

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
    connect (Peer const& from, Peer const& to,
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
    disconnect (Peer const& peer1, Peer const& peer2);

    /** Return the range of active links.

        @return A random access range.
    */
    boost::transformed_range<
        link_transform, links_type>
    links (Peer const& from);

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
    send (Peer const& from, Peer const& to,
        Function&& f);

    // Used to cancel timers
    struct cancel_token;

    /** Deliver a timer notification.

        Effects:

            When the network time is reached,
            the function will be called with
            no arguments.
    */
    template <class Function>
    cancel_token
    timer (time_point const& when,
        Function&& f);

    /** Deliver a timer notification.

        Effects:

            When the specified time has elapsed,
            the function will be called with
            no arguments.
    */
    template <class Function>
    cancel_token
    timer (duration const& delay,
        Function&& f);

    /** Cancel a timer.

        Preconditions:

            `token` was the return value of a call
            timer() which has not yet been invoked.
    */
    void
    cancel (cancel_token const& token);

    /** Perform breadth-first search.

        Function will be called with this signature:

            void(std::size_t, Peer&);

        The second argument is the distance of the
        peer from the start peer, in hops.
    */
    template <class Function>
    void
    bfs (Peer const& start, Function&& f);

    /** Run the network for up to one message.

        Effects:

            The clock is advanced to the time
            of the last delivered message.

        @return `true` if a message was processed.
    */
    bool
    step_one();

    /** Run the network until no messages remain.

        Effects:

            The clock is advanced to the time
            of the last delivered message.

        @return `true` if any message was processed.
    */
    bool
    step();

    /** Run the network while a condition is true.

        Function takes no arguments and will be called
        repeatedly after each message is processed to
        decide whether to continue.

        Effects:

            The clock is advanced to the time
            of the last delivered message.

        @return `true` if any message was processed.
    */
    template <class Function>
    bool
    step_while(Function && func);

    /** Run the network until the specified time.

        Effects:

            The clock is advanced to the
            specified time.

        @return `true` if any messages remain.
    */
    bool
    step_until (time_point const& until);

    /** Run the network until time has elapsed.

        Effects:

            The clock is advanced by the
            specified duration.

        @return `true` if any messages remain.
    */
    template <class Period, class Rep>
    bool
    step_for (std::chrono::duration<
        Period, Rep> const& amount);
};

//------------------------------------------------------------------------------

template <class Peer>
BasicNetwork<Peer>::queue_type::queue_type(
        qalloc const& alloc)
    : alloc_ (alloc)
{
}

template <class Peer>
BasicNetwork<Peer>::queue_type::~queue_type()
{
    for(auto iter = by_when_.begin();
        iter != by_when_.end();)
    {
        auto m = &*iter;
        ++iter;
        m->~msg();
        alloc_.dealloc(m, 1);
    }
}

template <class Peer>
inline
bool
BasicNetwork<Peer>::queue_type::empty() const
{
    return by_when_.empty();
}

template <class Peer>
inline
auto
BasicNetwork<Peer>::queue_type::begin() ->
    iterator
{
    return by_when_.begin();
}

template <class Peer>
inline
auto
BasicNetwork<Peer>::queue_type::end() ->
    iterator
{
    return by_when_.end();
}

template <class Peer>
template <class Handler>
auto
BasicNetwork<Peer>::queue_type::emplace(
    Peer const& from, Peer const& to, time_point when,
        Handler&& h) ->
            typename by_when_set::iterator
{
    using msg_type = msg_impl<
        std::decay_t<Handler>>;
    auto const p = alloc_.alloc<msg_type>(1);
    auto& m = *new(p) msg_type(from, to,
        when, std::forward<Handler>(h));
    if (to)
        by_to_[to].push_back(m);
    if (from)
        by_from_[from].push_back(m);
    return by_when_.insert(m);
}

template <class Peer>
void
BasicNetwork<Peer>::queue_type::erase(
    iterator iter)
{
    auto& m = *iter;
    if (iter->to)
    {
        auto& list = by_to_[iter->to];
        list.erase(list.iterator_to(m));
    }
    if (iter->from)
    {
        auto& list = by_from_[iter->from];
        list.erase(list.iterator_to(m));
    }
    by_when_.erase(iter);
    m.~msg();
    alloc_.dealloc(&m, 1);
}

template <class Peer>
void
BasicNetwork<Peer>::queue_type::remove(
    Peer const& from, Peer const& to)
{
    {
        auto& list = by_to_[to];
        for(auto iter = list.begin();
            iter != list.end();)
        {
            auto& m = *iter++;
            if (m.from == from)
                erase(by_when_.iterator_to(m));
        }
    }
    {
        auto& list = by_to_[from];
        for(auto iter = list.begin();
            iter != list.end();)
        {
            auto& m = *iter++;
            if (m.from == to)
                erase(by_when_.iterator_to(m));
        }
    }
}

//------------------------------------------------------------------------------

template <class Peer>
class BasicNetwork<Peer>::link_transform
{
private:
    BasicNetwork& net_;
    Peer from_;

public:
    using argument_type =
        typename links_type::value_type;

    class result_type
    {
    public:
        Peer to;
        bool inbound;

        result_type (result_type const&) = default;

        result_type (BasicNetwork& net,
            Peer const& from, Peer const& to_,
                bool inbound_)
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
        bool
        disconnect() const
        {
            return net_.disconnect(from_, to);
        }

    private:
        BasicNetwork& net_;
        Peer from_;
    };

    link_transform (BasicNetwork& net,
            Peer const& from)
        : net_(net)
        , from_(from)
    {
    }

    result_type const
    operator()(argument_type const& v) const
    {
        return result_type(net_, from_,
            v.first, v.second.inbound);
    }
};

//------------------------------------------------------------------------------

template <class Peer>
struct BasicNetwork<Peer>::cancel_token
{
private:
    typename queue_type::iterator iter_;

public:
    cancel_token() = delete;
    cancel_token (cancel_token const&) = default;
    cancel_token& operator= (cancel_token const&) = default;

private:
    friend class BasicNetwork;
    cancel_token(typename
            queue_type::iterator iter)
        : iter_ (iter)
    {
    }
};

//------------------------------------------------------------------------------

template <class Peer>
BasicNetwork<Peer>::BasicNetwork()
    : queue_ (alloc_)
{
}

template <class Peer>
inline
qalloc const&
BasicNetwork<Peer>::alloc() const
{
    return alloc_;
}

template <class Peer>
inline
auto
BasicNetwork<Peer>::clock() const ->
    clock_type&
{
    return clock_;
}

template <class Peer>
inline
auto
BasicNetwork<Peer>::now() const ->
    time_point
{
    return clock_.now();
}

template <class Peer>
bool
BasicNetwork<Peer>::connect(
    Peer const& from, Peer const& to,
        duration const& delay)
{
    if (to == from)
        return false;
    using namespace std;
    if (! links_[from].emplace(to,
            link_type{ false, delay }).second)
        return false;
    auto const result = links_[to].emplace(
        from, link_type{ true, delay });
    (void)result;
    assert(result.second);
    return true;
}

template <class Peer>
bool
BasicNetwork<Peer>::disconnect(
    Peer const& peer1, Peer const& peer2)
{
    if (links_[peer1].erase(peer2) == 0)
        return false;
    auto const n =
        links_[peer2].erase(peer1);
    (void)n;
    assert(n);
    queue_.remove(peer1, peer2);
    return true;
}

template <class Peer>
inline
auto
BasicNetwork<Peer>::links(Peer const& from) ->
    boost::transformed_range<
        link_transform, links_type>
{
    return boost::adaptors::transform(
        links_[from],
            link_transform{ *this, from });
}

template <class Peer>
template <class Function>
inline
void
BasicNetwork<Peer>::send(
    Peer const& from, Peer const& to,
        Function&& f)
{
    using namespace std;
    auto const iter =
        links_[from].find(to);
    queue_.emplace(from, to,
        clock_.now() + iter->second.delay,
            forward<Function>(f));
}

template <class Peer>
template <class Function>
inline
auto
BasicNetwork<Peer>::timer(
    time_point const& when, Function&& f) ->
        cancel_token
{
    using namespace std;
    return queue_.emplace(
        nullptr, nullptr, when,
            forward<Function>(f));
}

template <class Peer>
template <class Function>
inline
auto
BasicNetwork<Peer>::timer(
    duration const& delay, Function&& f) ->
        cancel_token
{
    return timer(clock_.now() + delay,
        std::forward<Function>(f));
}

template <class Peer>
inline
void
BasicNetwork<Peer>::cancel(
    cancel_token const& token)
{
    queue_.erase(token.iter_);
}

template <class Peer>
bool
BasicNetwork<Peer>::step_one()
{
    if (queue_.empty())
        return false;
    auto const iter = queue_.begin();
    clock_.set(iter->when);
    (*iter)();
    queue_.erase(iter);
    return true;
}

template <class Peer>
bool
BasicNetwork<Peer>::step()
{
    if (! step_one())
        return false;
    for(;;)
        if (! step_one())
            break;
    return true;
}

template <class Peer>
template <class Function>
bool
BasicNetwork<Peer>::step_while(Function && f)
{
    bool ran = false;
    while (f() && step_one())
        ran = true;
    return ran;
}

template <class Peer>
bool
BasicNetwork<Peer>::step_until(
    time_point const& until)
{
    // VFALCO This routine needs optimizing
    if(queue_.empty())
    {
        clock_.set(until);
        return false;
    }
    auto iter = queue_.begin();
    if(iter->when > until)
    {
        clock_.set(until);
        return true;
    }
    do
    {
        step_one();
        iter = queue_.begin();
    }
    while(iter != queue_.end() &&
        iter->when <= until);
    clock_.set(until);
    return iter != queue_.end();
}

template <class Peer>
template <class Period, class Rep>
inline
bool
BasicNetwork<Peer>::step_for(
    std::chrono::duration<Period, Rep> const& amount)
{
    return step_until(now() + amount);
}

template <class Peer>
template <class Function>
void
BasicNetwork<Peer>::bfs(
    Peer const& start, Function&& f)
{
    std::deque<std::pair<Peer, std::size_t>> q;
    std::unordered_set<Peer> seen;
    q.emplace_back(start, 0);
    seen.insert(start);
    while(! q.empty())
    {
        auto v = q.front();
        q.pop_front();
        f(v.second, v.first);
        for(auto const& link : links_[v.first])
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

} // csf
} // test
} // ripple

#endif
