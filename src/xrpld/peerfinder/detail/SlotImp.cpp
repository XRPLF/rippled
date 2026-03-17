#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/detail/SlotImp.h>
#include <xrpld/peerfinder/detail/Tuning.h>

namespace xrpl {
namespace PeerFinder {

SlotImp::SlotImp(
    beast::IP::Endpoint const& localEndpoint,
    beast::IP::Endpoint const& remoteEndpoint,
    bool fixed,
    clock_type& clock)
    : recent(clock)
    , inbound_(true)
    , fixed_(fixed)
    , reserved_(false)
    , state_(accept)
    , remote_endpoint_(remoteEndpoint)
    , local_endpoint_(localEndpoint)
    , listening_port_(unknownPort)
    , checked(false)
    , canAccept(false)
    , connectivityCheckInProgress(false)
{
}

SlotImp::SlotImp(beast::IP::Endpoint const& remoteEndpoint, bool fixed, clock_type& clock)
    : recent(clock)
    , inbound_(false)
    , fixed_(fixed)
    , reserved_(false)
    , state_(connect)
    , remote_endpoint_(remoteEndpoint)
    , listening_port_(unknownPort)
    , checked(true)
    , canAccept(true)
    , connectivityCheckInProgress(false)
{
}

void
SlotImp::state(State state)
{
    // Must go through activate() to set active state
    XRPL_ASSERT(state != active, "xrpl::PeerFinder::SlotImp::state : input state is not active");

    // The state must be different
    XRPL_ASSERT(
        state_ != state,
        "xrpl::PeerFinder::SlotImp::state : input state is different from "
        "current");

    // You can't transition into the initial states
    XRPL_ASSERT(
        state != accept && state != connect,
        "xrpl::PeerFinder::SlotImp::state : input state is not an initial");

    // Can only become connected from outbound connect state
    XRPL_ASSERT(
        state != connected || (!inbound_ && state_ == connect),
        "xrpl::PeerFinder::SlotImp::state : input state is not connected an "
        "invalid state");

    // Can't gracefully close on an outbound connection attempt
    XRPL_ASSERT(
        state != closing || state_ != connect,
        "xrpl::PeerFinder::SlotImp::state : input state is not closing an "
        "invalid state");

    state_ = state;
}

void
SlotImp::activate(clock_type::time_point const& now)
{
    // Can only become active from the accept or connected state
    XRPL_ASSERT(
        state_ == accept || state_ == connected,
        "xrpl::PeerFinder::SlotImp::activate : valid state");

    state_ = active;
    whenAcceptEndpoints = now;
}

//------------------------------------------------------------------------------

Slot::~Slot() = default;

//------------------------------------------------------------------------------

SlotImp::recent_t::recent_t(clock_type& clock) : cache(clock)
{
}

void
SlotImp::recent_t::insert(beast::IP::Endpoint const& ep, std::uint32_t hops)
{
    auto const result(cache.emplace(ep, hops));
    if (!result.second)
    {
        // NOTE Other logic depends on this <= inequality.
        if (hops <= result.first->second)
        {
            result.first->second = hops;
            cache.touch(result.first);
        }
    }
}

bool
SlotImp::recent_t::filter(beast::IP::Endpoint const& ep, std::uint32_t hops)
{
    auto const iter(cache.find(ep));
    if (iter == cache.end())
        return false;
    // We avoid sending an endpoint if we heard it
    // from them recently at the same or lower hop count.
    // NOTE Other logic depends on this <= inequality.
    return iter->second <= hops;
}

void
SlotImp::recent_t::expire()
{
    beast::expire(cache, Tuning::liveCacheSecondsToLive);
}

}  // namespace PeerFinder
}  // namespace xrpl
