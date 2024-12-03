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

#ifndef RIPPLE_PEERFINDER_HANDOUTS_H_INCLUDED
#define RIPPLE_PEERFINDER_HANDOUTS_H_INCLUDED

#include <xrpld/peerfinder/detail/SlotImp.h>
#include <xrpld/peerfinder/detail/Tuning.h>
#include <xrpl/beast/container/aged_set.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <iterator>
#include <type_traits>

namespace ripple {
namespace PeerFinder {

namespace detail {

/** Try to insert one object in the target.
    When an item is handed out it is moved to the end of the container.
    @return The number of objects inserted
*/
// VFALCO TODO specialization that handles std::list for SequenceContainer
//             using splice for optimization over erase/push_back
//
template <class Target, class HopContainer>
std::size_t
handout_one(Target& t, HopContainer& h)
{
    ASSERT(
        !t.full(),
        "ripple::PeerFinder::detail::handout_one : target is not full");
    for (auto it = h.begin(); it != h.end(); ++it)
    {
        auto const& e = *it;
        if (t.try_insert(e))
        {
            h.move_back(it);
            return 1;
        }
    }
    return 0;
}

}  // namespace detail

/** Distributes objects to targets according to business rules.
    A best effort is made to evenly distribute items in the sequence
    container list into the target sequence list.
*/
template <class TargetFwdIter, class SeqFwdIter>
void
handout(
    TargetFwdIter first,
    TargetFwdIter last,
    SeqFwdIter seq_first,
    SeqFwdIter seq_last)
{
    for (;;)
    {
        std::size_t n(0);
        for (auto si = seq_first; si != seq_last; ++si)
        {
            auto c = *si;
            bool all_full(true);
            for (auto ti = first; ti != last; ++ti)
            {
                auto& t = *ti;
                if (!t.full())
                {
                    n += detail::handout_one(t, c);
                    all_full = false;
                }
            }
            if (all_full)
                return;
        }
        if (!n)
            break;
    }
}

//------------------------------------------------------------------------------

/** Receives handouts for redirecting a connection.
    An incoming connection request is redirected when we are full on slots.
*/
class RedirectHandouts
{
public:
    template <class = void>
    explicit RedirectHandouts(SlotImp::ptr const& slot);

    template <class = void>
    bool
    try_insert(Endpoint const& ep);

    bool
    full() const
    {
        return list_.size() >= Tuning::redirectEndpointCount;
    }

    SlotImp::ptr const&
    slot() const
    {
        return slot_;
    }

    std::vector<Endpoint>&
    list()
    {
        return list_;
    }

    std::vector<Endpoint> const&
    list() const
    {
        return list_;
    }

private:
    SlotImp::ptr slot_;
    std::vector<Endpoint> list_;
};

template <class>
RedirectHandouts::RedirectHandouts(SlotImp::ptr const& slot) : slot_(slot)
{
    list_.reserve(Tuning::redirectEndpointCount);
}

template <class>
bool
RedirectHandouts::try_insert(Endpoint const& ep)
{
    if (full())
        return false;

    // VFALCO NOTE This check can be removed when we provide the
    //             addresses in a peer HTTP handshake instead of
    //             the tmENDPOINTS message.
    //
    if (ep.hops > Tuning::maxHops)
        return false;

    // Don't send them our address
    if (ep.hops == 0)
        return false;

    // Don't send them their own address
    if (slot_->remote_endpoint().address() == ep.address.address())
        return false;

    // Make sure the address isn't already in our list
    if (std::any_of(list_.begin(), list_.end(), [&ep](Endpoint const& other) {
            // Ignore port for security reasons
            return other.address.address() == ep.address.address();
        }))
    {
        return false;
    }

    list_.emplace_back(ep.address, ep.hops);

    return true;
}

//------------------------------------------------------------------------------

/** Receives endpoints for a slot during periodic handouts. */
class SlotHandouts
{
public:
    template <class = void>
    explicit SlotHandouts(SlotImp::ptr const& slot);

    template <class = void>
    bool
    try_insert(Endpoint const& ep);

    bool
    full() const
    {
        return list_.size() >= Tuning::numberOfEndpoints;
    }

    void
    insert(Endpoint const& ep)
    {
        list_.push_back(ep);
    }

    SlotImp::ptr const&
    slot() const
    {
        return slot_;
    }

    std::vector<Endpoint> const&
    list() const
    {
        return list_;
    }

private:
    SlotImp::ptr slot_;
    std::vector<Endpoint> list_;
};

template <class>
SlotHandouts::SlotHandouts(SlotImp::ptr const& slot) : slot_(slot)
{
    list_.reserve(Tuning::numberOfEndpoints);
}

template <class>
bool
SlotHandouts::try_insert(Endpoint const& ep)
{
    if (full())
        return false;

    if (ep.hops > Tuning::maxHops)
        return false;

    if (slot_->recent.filter(ep.address, ep.hops))
        return false;

    // Don't send them their own address
    if (slot_->remote_endpoint().address() == ep.address.address())
        return false;

    // Make sure the address isn't already in our list
    if (std::any_of(list_.begin(), list_.end(), [&ep](Endpoint const& other) {
            // Ignore port for security reasons
            return other.address.address() == ep.address.address();
        }))
        return false;

    list_.emplace_back(ep.address, ep.hops);

    // Insert into this slot's recent table. Although the endpoint
    // didn't come from the slot, adding it to the slot's table
    // prevents us from sending it again until it has expired from
    // the other end's cache.
    //
    slot_->recent.insert(ep.address, ep.hops);

    return true;
}

//------------------------------------------------------------------------------

/** Receives handouts for making automatic connections. */
class ConnectHandouts
{
public:
    // Keeps track of addresses we have made outgoing connections
    // to, for the purposes of not connecting to them too frequently.
    using Squelches = beast::aged_set<beast::IP::Address>;

    using list_type = std::vector<beast::IP::Endpoint>;

private:
    std::size_t m_needed;
    Squelches& m_squelches;
    list_type m_list;

public:
    template <class = void>
    ConnectHandouts(std::size_t needed, Squelches& squelches);

    template <class = void>
    bool
    try_insert(beast::IP::Endpoint const& endpoint);

    bool
    empty() const
    {
        return m_list.empty();
    }

    bool
    full() const
    {
        return m_list.size() >= m_needed;
    }

    bool
    try_insert(Endpoint const& endpoint)
    {
        return try_insert(endpoint.address);
    }

    list_type&
    list()
    {
        return m_list;
    }

    list_type const&
    list() const
    {
        return m_list;
    }
};

template <class>
ConnectHandouts::ConnectHandouts(std::size_t needed, Squelches& squelches)
    : m_needed(needed), m_squelches(squelches)
{
    m_list.reserve(needed);
}

template <class>
bool
ConnectHandouts::try_insert(beast::IP::Endpoint const& endpoint)
{
    if (full())
        return false;

    // Make sure the address isn't already in our list
    if (std::any_of(
            m_list.begin(),
            m_list.end(),
            [&endpoint](beast::IP::Endpoint const& other) {
                // Ignore port for security reasons
                return other.address() == endpoint.address();
            }))
    {
        return false;
    }

    // Add to squelch list so we don't try it too often.
    // If its already there, then make try_insert fail.
    auto const result(m_squelches.insert(endpoint.address()));
    if (!result.second)
        return false;

    m_list.push_back(endpoint);

    return true;
}

}  // namespace PeerFinder
}  // namespace ripple

#endif
