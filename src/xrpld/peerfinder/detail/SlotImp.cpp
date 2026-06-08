#include <xrpld/peerfinder/detail/SlotImp.h>

#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/Slot.h>
#include <xrpld/peerfinder/detail/Tuning.h>

#include <xrpl/beast/container/detail/aged_unordered_container.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <cstdint>
#include <utility>

namespace xrpl::PeerFinder {

SlotImp::SlotImp(
    beast::IP::Endpoint const& localEndpoint,
    beast::IP::Endpoint remoteEndpoint,
    bool fixed,
    clock_type& clock)
    : recent(clock)
    , inbound_(true)
    , fixed_(fixed)
    , reserved_(false)
    , state_(State::Accept)
    , remoteEndpoint_(std::move(remoteEndpoint))
    , localEndpoint_(localEndpoint)
    , listeningPort_(kUnknownPort)
    , checked(false)
    , canAccept(false)
    , connectivityCheckInProgress(false)
{
}

SlotImp::SlotImp(beast::IP::Endpoint remoteEndpoint, bool fixed, clock_type& clock)
    : recent(clock)
    , inbound_(false)
    , fixed_(fixed)
    , reserved_(false)
    , state_(State::Connect)
    , remoteEndpoint_(std::move(remoteEndpoint))
    , listeningPort_(kUnknownPort)
    , checked(true)
    , canAccept(true)
    , connectivityCheckInProgress(false)
{
}

void
SlotImp::state(State state)
{
    // Must go through activate() to set active state
    XRPL_ASSERT(
        state != State::Active, "xrpl::PeerFinder::SlotImp::state : input state is not active");

    // The state must be different
    XRPL_ASSERT(
        state_ != state,
        "xrpl::PeerFinder::SlotImp::state : input state is different from "
        "current");

    // You can't transition into the initial states
    XRPL_ASSERT(
        state != State::Accept && state != State::Connect,
        "xrpl::PeerFinder::SlotImp::state : input state is not an initial");

    // Can only become connected from outbound connect state
    XRPL_ASSERT(
        state != State::Connected || (!inbound_ && state_ == State::Connect),
        "xrpl::PeerFinder::SlotImp::state : input state is not connected an "
        "invalid state");

    // Can't gracefully close on an outbound connection attempt
    XRPL_ASSERT(
        state != State::Closing || state_ != State::Connect,
        "xrpl::PeerFinder::SlotImp::state : input state is not closing an "
        "invalid state");

    state_ = state;
}

void
SlotImp::activate(clock_type::time_point const& now)
{
    // Can only become active from the accept or connected state
    XRPL_ASSERT(
        state_ == State::Accept || state_ == State::Connected,
        "xrpl::PeerFinder::SlotImp::activate : valid state");

    state_ = State::Active;
    whenAcceptEndpoints = now;
}

//------------------------------------------------------------------------------

Slot::~Slot() = default;

//------------------------------------------------------------------------------

SlotImp::RecentT::RecentT(clock_type& clock) : cache_(clock)
{
}

void
SlotImp::RecentT::insert(beast::IP::Endpoint const& ep, std::uint32_t hops)
{
    auto const result(cache_.emplace(ep, hops));
    if (!result.second)
    {
        // NOTE Other logic depends on this <= inequality.
        if (hops <= result.first->second)
        {
            result.first->second = hops;
            cache_.touch(result.first);
        }
    }
}

bool
SlotImp::RecentT::filter(beast::IP::Endpoint const& ep, std::uint32_t hops)
{
    auto const iter(cache_.find(ep));
    if (iter == cache_.end())
        return false;
    // We avoid sending an endpoint if we heard it
    // from them recently at the same or lower hop count.
    // NOTE Other logic depends on this <= inequality.
    return iter->second <= hops;
}

void
SlotImp::RecentT::expire()
{
    beast::expire(cache_, Tuning::kLiveCacheSecondsToLive);
}

}  // namespace xrpl::PeerFinder
