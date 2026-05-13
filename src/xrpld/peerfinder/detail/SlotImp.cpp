/** @file
 *  Implements the concrete PeerFinder slot — per-connection state machine and
 *  the recent-endpoint deduplication cache.
 *
 *  `SlotImp` is the mutable counterpart of the read-only `Slot` interface.
 *  All lifecycle state transitions pass through this file; the header provides
 *  only trivial accessor inlines.
 */

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

/** Construct an inbound slot for an already-accepted TCP connection.
 *
 *  Both endpoints are known immediately from the accept call, so both are
 *  recorded at construction.  Because the remote peer has not yet been
 *  interrogated, `checked` and `canAccept` are initialised to `false` —
 *  the PeerFinder does not yet know whether the remote address is publicly
 *  reachable.  A connectivity probe may be launched later to determine this.
 *
 *  Initial state is `State::Accept`.
 *
 *  @param localEndpoint   The local socket endpoint of the accepted connection.
 *  @param remoteEndpoint  The remote peer's socket endpoint.
 *  @param fixed           Whether the remote address is in the operator's
 *      fixed-peers list; exempts the slot from normal slot-budget enforcement.
 *  @param clock           Shared clock used to timestamp the `recent` cache.
 */
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
    , remote_endpoint_(std::move(remoteEndpoint))
    , local_endpoint_(localEndpoint)
    , listening_port_(kUNKNOWN_PORT)
    , checked(false)
    , canAccept(false)
    , connectivityCheckInProgress(false)
{
}

/** Construct an outbound slot for a connection being initiated.
 *
 *  The local endpoint is unknown until the OS assigns a port after
 *  `async_connect` completes; `local_endpoint_` is left as `nullopt` and
 *  filled in by `Logic::onConnected`.  Crucially, `checked` and `canAccept`
 *  are initialised to `true`: successfully connecting to a remote address is
 *  itself proof of reachability, so no separate connectivity probe is needed.
 *
 *  Initial state is `State::Connect`.
 *
 *  @param remoteEndpoint  The remote peer's address being dialled.
 *  @param fixed           Whether the remote address is in the operator's
 *      fixed-peers list; exempts the slot from normal slot-budget enforcement.
 *  @param clock           Shared clock used to timestamp the `recent` cache.
 */
SlotImp::SlotImp(beast::IP::Endpoint remoteEndpoint, bool fixed, clock_type& clock)
    : recent(clock)
    , inbound_(false)
    , fixed_(fixed)
    , reserved_(false)
    , state_(State::Connect)
    , remote_endpoint_(std::move(remoteEndpoint))
    , listening_port_(kUNKNOWN_PORT)
    , checked(true)
    , canAccept(true)
    , connectivityCheckInProgress(false)
{
}

/** Advance the slot to a new lifecycle state, enforcing the state machine topology.
 *
 *  This is the general-purpose state setter for all transitions **except**
 *  the move to `State::Active`, which must go through `activate()` so that
 *  `whenAcceptEndpoints` is stamped atomically.  The following invariants are
 *  enforced via `XRPL_ASSERT` and will abort in debug builds on violation:
 *
 *  - `State::Active` is rejected here — use `activate()`.
 *  - The target state must differ from the current state (no no-op transitions).
 *  - `State::Accept` and `State::Connect` are entry-point-only states; no
 *    code path may re-enter them after construction.
 *  - `State::Connected` is only reachable from outbound slots currently in
 *    `State::Connect`.  Inbound slots skip this state entirely, going directly
 *    from `State::Accept` to `State::Active` via `activate()`.
 *  - `State::Closing` is forbidden while the slot is still in `State::Connect`
 *    (an in-progress outbound attempt should be aborted, not gracefully closed).
 *
 *  @param state  The desired target state.
 */
void
SlotImp::state(State state)
{
    XRPL_ASSERT(
        state != State::Active, "xrpl::PeerFinder::SlotImp::state : input state is not active");

    XRPL_ASSERT(
        state_ != state,
        "xrpl::PeerFinder::SlotImp::state : input state is different from "
        "current");

    XRPL_ASSERT(
        state != State::Accept && state != State::Connect,
        "xrpl::PeerFinder::SlotImp::state : input state is not an initial");

    XRPL_ASSERT(
        state != State::Connected || (!inbound_ && state_ == State::Connect),
        "xrpl::PeerFinder::SlotImp::state : input state is not connected an "
        "invalid state");

    XRPL_ASSERT(
        state != State::Closing || state_ != State::Connect,
        "xrpl::PeerFinder::SlotImp::state : input state is not closing an "
        "invalid state");

    state_ = state;
}

/** Transition the slot to `State::Active` and arm the endpoint-flood throttle.
 *
 *  This is the **sole** path to `State::Active`.  Splitting activation into
 *  its own method — rather than allowing `state(State::Active)` — guarantees
 *  that `whenAcceptEndpoints` is always set when a slot goes live.  Skipping
 *  this stamp would leave the flood-control timestamp uninitialised, allowing
 *  a peer to immediately flood `mtENDPOINTS` messages.
 *
 *  Valid prior states are `State::Accept` (inbound, post-handshake) and
 *  `State::Connected` (outbound, post-handshake).  Any other prior state
 *  triggers an `XRPL_ASSERT` abort in debug builds.
 *
 *  @param now  Current clock time, recorded as `whenAcceptEndpoints` to
 *      establish the start of the per-slot endpoint rate-limiting window.
 */
void
SlotImp::activate(clock_type::time_point const& now)
{
    XRPL_ASSERT(
        state_ == State::Accept || state_ == State::Connected,
        "xrpl::PeerFinder::SlotImp::activate : valid state");

    state_ = State::Active;
    whenAcceptEndpoints = now;
}

//------------------------------------------------------------------------------

/** Out-of-line definition required for the pure-virtual destructor.
 *
 *  C++ requires a definition for pure-virtual destructors because the
 *  base-class destructor is called during the destruction of any derived
 *  object.  The `= default` body satisfies this without adding code.
 */
Slot::~Slot() = default;

//------------------------------------------------------------------------------

/** Construct the recent-endpoint cache backed by the given clock.
 *
 *  @param clock  Shared clock for the underlying `beast::aged_unordered_map`,
 *      used to track insertion time for TTL-based expiry.
 */
SlotImp::RecentT::RecentT(clock_type& clock) : cache_(clock)
{
}

/** Record an endpoint and its hop distance in the per-slot deduplication cache.
 *
 *  Called for every endpoint we receive **from** this peer and every endpoint
 *  we **send to** this peer, so that `filter()` can suppress redundant gossip
 *  in both directions.
 *
 *  If the endpoint is already in the cache and the new hop count is less than
 *  or equal to the stored value, the stored hop count is updated and the
 *  entry's age is reset via `touch()`.  If the new hop count is strictly
 *  greater than the stored value, the entry is left unchanged — a closer
 *  known path takes precedence.
 *
 *  @note The `<=` update boundary is load-bearing: `filter()` mirrors it, and
 *      the `Handouts` algorithm depends on this pair of semantics to correctly
 *      decide which endpoints are redundant for a given peer.
 *
 *  @param ep    The endpoint address to record.
 *  @param hops  The hop distance at which `ep` was observed or announced.
 */
void
SlotImp::RecentT::insert(beast::IP::Endpoint const& ep, std::uint32_t hops)
{
    auto const result(cache_.emplace(ep, hops));
    if (!result.second)
    {
        if (hops <= result.first->second)
        {
            result.first->second = hops;
            cache_.touch(result.first);
        }
    }
}

/** Returns `true` if sending this endpoint to the peer would be redundant.
 *
 *  Suppresses an outbound announcement when the cache already holds the same
 *  endpoint at a hop count **less than or equal to** the hop count we are
 *  about to send.  This reflects the principle that a peer with closer
 *  knowledge of an address does not benefit from hearing about it again at
 *  the same or greater distance.
 *
 *  Mirroring `insert()`'s `<=` update boundary is intentional and load-bearing
 *  for the `Handouts` round-robin algorithm — changing one without the other
 *  would break the gossip deduplication contract.
 *
 *  @param ep    The endpoint address to test.
 *  @param hops  The hop distance at which we would announce `ep`.
 *  @return `true` if the send should be suppressed; `false` if the endpoint
 *      is unknown or the cached hop count is strictly greater than `hops`
 *      (meaning we have a closer path worth sharing).
 */
bool
SlotImp::RecentT::filter(beast::IP::Endpoint const& ep, std::uint32_t hops)
{
    auto const iter(cache_.find(ep));
    if (iter == cache_.end())
        return false;
    return iter->second <= hops;
}

/** Remove stale entries from the deduplication cache.
 *
 *  Prunes all entries older than `Tuning::kLIVE_CACHE_SECONDS_TO_LIVE` (30 s).
 *  Called by `Logic::oncePerSecond()` so that a peer which drops and reconnects
 *  will receive fresh endpoint announcements rather than having them
 *  indefinitely suppressed by stale cache state.
 *
 *  The TTL deliberately matches the Livecache TTL so that the two caches
 *  remain consistent in their view of "recently known" endpoints.
 */
void
SlotImp::RecentT::expire()
{
    beast::expire(cache_, Tuning::kLIVE_CACHE_SECONDS_TO_LIVE);
}

}  // namespace xrpl::PeerFinder
