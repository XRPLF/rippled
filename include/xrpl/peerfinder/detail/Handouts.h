#pragma once

#include <xrpl/beast/container/aged_set.h>
#include <xrpl/beast/net/IPAddress.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/peerfinder/Types.h>
#include <xrpl/peerfinder/detail/SlotImp.h>
#include <xrpl/peerfinder/detail/Tuning.h>

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace xrpl::peer_finder {

namespace detail {

/**
 * Try to insert one object in the target.
 * When an item is handed out it is moved to the end of the container.
 * @return The number of objects inserted
 */
// VFALCO TODO specialization that handles std::list for SequenceContainer
//             using splice for optimization over erase/push_back
//
template <class Target, class HopContainer>
std::size_t
handoutOne(Target& t, HopContainer& h)
{
    XRPL_ASSERT(!t.full(), "xrpl::peer_finder::detail::handoutOne : target is not full");
    for (auto it = h.begin(); it != h.end(); ++it)
    {
        auto const& e = *it;
        if (t.tryInsert(e))
        {
            h.moveBack(it);
            return 1;
        }
    }
    return 0;
}

}  // namespace detail

/**
 * Distributes objects to targets according to business rules.
 * A best effort is made to evenly distribute items in the sequence
 * container list into the target sequence list.
 */
template <class TargetFwdIter, class SeqFwdIter>
void
handout(TargetFwdIter first, TargetFwdIter last, SeqFwdIter seqFirst, SeqFwdIter seqLast)
{
    for (;;)
    {
        std::size_t n(0);
        for (auto si = seqFirst; si != seqLast; ++si)
        {
            auto c = *si;
            bool allFull(true);
            for (auto ti = first; ti != last; ++ti)
            {
                auto& t = *ti;
                if (!t.full())
                {
                    n += detail::handoutOne(t, c);
                    allFull = false;
                }
            }
            if (allFull)
                return;
        }
        if (!n)
            break;
    }
}

//------------------------------------------------------------------------------

/**
 * Receives handouts for redirecting a connection.
 * An incoming connection request is redirected when we are full on slots.
 */
class RedirectHandouts
{
public:
    template <class = void>
    explicit RedirectHandouts(SlotImp::Ptr slot);

    template <class = void>
    bool
    tryInsert(Endpoint const& ep);

    [[nodiscard]] bool
    full() const
    {
        return list_.size() >= tuning::kRedirectEndpointCount;
    }

    [[nodiscard]] SlotImp::Ptr const&
    slot() const
    {
        return slot_;
    }

    std::vector<Endpoint>&
    list()
    {
        return list_;
    }

    [[nodiscard]] std::vector<Endpoint> const&
    list() const
    {
        return list_;
    }

private:
    SlotImp::Ptr slot_;
    std::vector<Endpoint> list_;
};

template <class>
RedirectHandouts::RedirectHandouts(SlotImp::Ptr slot) : slot_(std::move(slot))
{
    list_.reserve(tuning::kRedirectEndpointCount);
}

template <class>
bool
RedirectHandouts::tryInsert(Endpoint const& ep)
{
    if (full())
        return false;

    // VFALCO NOTE This check can be removed when we provide the
    //             addresses in a peer HTTP handshake instead of
    //             the tmENDPOINTS message.
    //
    if (ep.hops > tuning::kMaxHops)
        return false;

    // Don't send them our address
    if (ep.hops == 0)
        return false;

    // Don't send them their own address
    if (slot_->remoteEndpoint().address() == ep.address.address())
        return false;

    // Make sure the address isn't already in our list
    if (std::ranges::any_of(list_, [&ep](Endpoint const& other) {
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

/**
 * Receives endpoints for a slot during periodic handouts.
 */
class SlotHandouts
{
public:
    template <class = void>
    explicit SlotHandouts(SlotImp::Ptr slot);

    template <class = void>
    bool
    tryInsert(Endpoint const& ep);

    [[nodiscard]] bool
    full() const
    {
        return list_.size() >= tuning::kNumberOfEndpoints;
    }

    void
    insert(Endpoint const& ep)
    {
        list_.push_back(ep);
    }

    [[nodiscard]] SlotImp::Ptr const&
    slot() const
    {
        return slot_;
    }

    [[nodiscard]] std::vector<Endpoint> const&
    list() const
    {
        return list_;
    }

private:
    SlotImp::Ptr slot_;
    std::vector<Endpoint> list_;
};

template <class>
SlotHandouts::SlotHandouts(SlotImp::Ptr slot) : slot_(std::move(slot))
{
    list_.reserve(tuning::kNumberOfEndpoints);
}

template <class>
bool
SlotHandouts::tryInsert(Endpoint const& ep)
{
    if (full())
        return false;

    if (ep.hops > tuning::kMaxHops)
        return false;

    if (slot_->recent.filter(ep.address, ep.hops))
        return false;

    // Don't send them their own address
    if (slot_->remoteEndpoint().address() == ep.address.address())
        return false;

    // Make sure the address isn't already in our list
    if (std::ranges::any_of(list_, [&ep](Endpoint const& other) {
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

/**
 * Receives handouts for making automatic connections.
 */
class ConnectHandouts
{
public:
    // Keeps track of addresses we have made outgoing connections
    // to, for the purposes of not connecting to them too frequently.
    using Squelches = beast::AgedSet<beast::ip::Address>;

    using ListType = std::vector<beast::ip::Endpoint>;

private:
    std::size_t needed_;
    Squelches& squelches_;
    ListType list_;

public:
    template <class = void>
    ConnectHandouts(std::size_t needed, Squelches& squelches);

    template <class = void>
    bool
    tryInsert(beast::ip::Endpoint const& endpoint);

    [[nodiscard]] bool
    empty() const
    {
        return list_.empty();
    }

    [[nodiscard]] bool
    full() const
    {
        return list_.size() >= needed_;
    }

    bool
    tryInsert(Endpoint const& endpoint)
    {
        return tryInsert(endpoint.address);
    }

    ListType&
    list()
    {
        return list_;
    }

    [[nodiscard]] ListType const&
    list() const
    {
        return list_;
    }
};

template <class>
ConnectHandouts::ConnectHandouts(std::size_t needed, Squelches& squelches)
    : needed_(needed), squelches_(squelches)
{
    list_.reserve(needed);
}

template <class>
bool
ConnectHandouts::tryInsert(beast::ip::Endpoint const& endpoint)
{
    if (full())
        return false;

    // Make sure the address isn't already in our list
    if (std::ranges::any_of(list_, [&endpoint](beast::ip::Endpoint const& other) {
            // Ignore port for security reasons
            return other.address() == endpoint.address();
        }))
    {
        return false;
    }

    // Add to squelch list so we don't try it too often.
    // If its already there, then make try_insert fail.
    auto const result(squelches_.insert(endpoint.address()));
    if (!result.second)
        return false;

    list_.push_back(endpoint);

    return true;
}

}  // namespace xrpl::peer_finder
