#pragma once

#include <xrpld/peerfinder/detail/SlotImp.h>
#include <xrpld/peerfinder/detail/Tuning.h>

#include <xrpl/beast/container/aged_set.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <utility>

namespace xrpl::PeerFinder {

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
handoutOne(Target& t, HopContainer& h)
{
    XRPL_ASSERT(!t.full(), "xrpl::PeerFinder::detail::handoutOne : target is not full");
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

/** Distributes objects to targets according to business rules.
    A best effort is made to evenly distribute items in the sequence
    container list into the target sequence list.
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

/** Receives handouts for redirecting a connection.
    An incoming connection request is redirected when we are full on slots.
*/
class RedirectHandouts
{
public:
    template <class = void>
    explicit RedirectHandouts(SlotImp::ptr slot);

    template <class = void>
    bool
    tryInsert(Endpoint const& ep);

    [[nodiscard]] bool
    full() const
    {
        return list_.size() >= Tuning::kREDIRECT_ENDPOINT_COUNT;
    }

    [[nodiscard]] SlotImp::ptr const&
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
    SlotImp::ptr slot_;
    std::vector<Endpoint> list_;
};

template <class>
RedirectHandouts::RedirectHandouts(SlotImp::ptr slot) : slot_(std::move(slot))
{
    list_.reserve(Tuning::kREDIRECT_ENDPOINT_COUNT);
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
    if (ep.hops > Tuning::kMAX_HOPS)
        return false;

    // Don't send them our address
    if (ep.hops == 0)
        return false;

    // Don't send them their own address
    if (slot_->remoteEndpoint().address() == ep.address.address())
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
    explicit SlotHandouts(SlotImp::ptr slot);

    template <class = void>
    bool
    tryInsert(Endpoint const& ep);

    [[nodiscard]] bool
    full() const
    {
        return list_.size() >= Tuning::kNUMBER_OF_ENDPOINTS;
    }

    void
    insert(Endpoint const& ep)
    {
        list_.push_back(ep);
    }

    [[nodiscard]] SlotImp::ptr const&
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
    SlotImp::ptr slot_;
    std::vector<Endpoint> list_;
};

template <class>
SlotHandouts::SlotHandouts(SlotImp::ptr slot) : slot_(std::move(slot))
{
    list_.reserve(Tuning::kNUMBER_OF_ENDPOINTS);
}

template <class>
bool
SlotHandouts::tryInsert(Endpoint const& ep)
{
    if (full())
        return false;

    if (ep.hops > Tuning::kMAX_HOPS)
        return false;

    if (slot_->recent.filter(ep.address, ep.hops))
        return false;

    // Don't send them their own address
    if (slot_->remoteEndpoint().address() == ep.address.address())
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
    std::size_t needed_;
    Squelches& squelches_;
    list_type list_;

public:
    template <class = void>
    ConnectHandouts(std::size_t needed, Squelches& squelches);

    template <class = void>
    bool
    tryInsert(beast::IP::Endpoint const& endpoint);

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

    list_type&
    list()
    {
        return list_;
    }

    [[nodiscard]] list_type const&
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
ConnectHandouts::tryInsert(beast::IP::Endpoint const& endpoint)
{
    if (full())
        return false;

    // Make sure the address isn't already in our list
    if (std::any_of(list_.begin(), list_.end(), [&endpoint](beast::IP::Endpoint const& other) {
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

}  // namespace xrpl::PeerFinder
