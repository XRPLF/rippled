/** @file
 *  Declares `SlotImp`, the mutable concrete implementation of the `Slot`
 *  interface used internally by PeerFinder's `Logic` class to track per-peer
 *  connection state, lifecycle transitions, and endpoint gossip deduplication.
 */

#pragma once

#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/Slot.h>

#include <xrpl/beast/container/aged_unordered_map.h>

#include <atomic>
#include <optional>

namespace xrpl::PeerFinder {

/** Concrete, mutable peer connection slot owned by PeerFinder's `Logic`.
 *
 *  `SlotImp` extends the read-only `Slot` interface with the writable state
 *  and state-machine enforcement that `Logic` needs to manage the overlay
 *  topology. `Logic` keeps slots in a `std::map` keyed by remote endpoint and
 *  passes them around as `SlotImp::ptr`; the broader codebase receives only the
 *  weaker `shared_ptr<Slot>` handle.
 *
 *  @note `m_inbound` and `m_fixed` are `const` after construction — the
 *      connection direction and fixed-peer status never change for the lifetime
 *      of the slot. All other state evolves through the methods below.
 *  @see Slot, Logic
 */
class SlotImp : public Slot
{
public:
    /** Shared-ownership handle used within the `detail` namespace.
     *
     *  `Logic` stores and passes slots as `SlotImp::ptr` rather than the
     *  weaker `Slot::ptr` so it can reach mutable members without casting.
     */
    using ptr = std::shared_ptr<SlotImp>;

    /** Construct an inbound slot for an already-accepted TCP connection.
     *
     *  Both endpoints are known from the accept call. `checked` and
     *  `canAccept` start `false` — reachability of the remote address has not
     *  been verified yet; a connectivity probe may be launched later. Initial
     *  state is `State::Accept`.
     *
     *  @param localEndpoint   Local socket endpoint of the accepted connection.
     *  @param remoteEndpoint  Remote peer's socket endpoint.
     *  @param fixed           `true` if the remote address is in the
     *      operator's fixed-peers list, exempting this slot from the normal
     *      slot-budget enforced by `Counts::can_activate`.
     *  @param clock           Shared clock for timestamping the `recent` cache.
     */
    SlotImp(
        beast::IP::Endpoint const& localEndpoint,
        beast::IP::Endpoint remoteEndpoint,
        bool fixed,
        clock_type& clock);

    /** Construct an outbound slot for a connection being initiated.
     *
     *  The local endpoint is unknown until the OS assigns a port after
     *  `async_connect` completes; `local_endpoint_` is left `nullopt` and
     *  filled in by `Logic::onConnected`. `checked` and `canAccept` are
     *  initialised `true` — a successful outbound TCP connect is itself proof
     *  of reachability, so no separate connectivity probe is needed. Initial
     *  state is `State::Connect`.
     *
     *  @param remoteEndpoint  Remote peer's address being dialled.
     *  @param fixed           `true` if the remote address is in the
     *      operator's fixed-peers list, exempting this slot from the normal
     *      slot-budget enforced by `Counts::can_activate`.
     *  @param clock           Shared clock for timestamping the `recent` cache.
     */
    SlotImp(beast::IP::Endpoint remoteEndpoint, bool fixed, clock_type& clock);

    bool
    inbound() const override
    {
        return inbound_;
    }

    bool
    fixed() const override
    {
        return fixed_;
    }

    bool
    reserved() const override
    {
        return reserved_;
    }

    State
    state() const override
    {
        return state_;
    }

    beast::IP::Endpoint const&
    remoteEndpoint() const override
    {
        return remote_endpoint_;
    }

    std::optional<beast::IP::Endpoint> const&
    localEndpoint() const override
    {
        return local_endpoint_;
    }

    std::optional<PublicKey> const&
    publicKey() const override
    {
        return public_key_;
    }

    /** Returns a human-readable log prefix identifying this slot.
     *
     *  Wraps the remote endpoint's fingerprint (IP:port + optional public key
     *  abbreviation) in brackets for use at the start of log lines. Delegates
     *  to `getFingerprint()`.
     *
     *  @return A string of the form `"[<fingerprint>] "`.
     */
    std::string
    prefix() const
    {
        return "[" + getFingerprint(remoteEndpoint(), publicKey()) + "] ";
    }

    std::optional<std::uint16_t>
    listeningPort() const override
    {
        std::uint32_t const value = listening_port_;
        if (value == kUNKNOWN_PORT)
            return std::nullopt;
        return value;
    }

    /** Record the port on which the remote peer accepts inbound connections.
     *
     *  Written by `Logic` after the connectivity probe confirms reachability.
     *  The underlying `atomic<int32_t>` makes this safe to call from a thread
     *  different from the reader of `listeningPort()`.
     *
     *  @param port  The verified listening port of the remote peer.
     */
    void
    setListeningPort(std::uint16_t port)
    {
        listening_port_ = port;
    }

    /** Set the local socket endpoint, filled in after outbound connect completes.
     *
     *  @param endpoint  The OS-assigned local endpoint for this connection.
     */
    void
    localEndpoint(beast::IP::Endpoint const& endpoint)
    {
        local_endpoint_ = endpoint;
    }

    /** Update the remote endpoint, e.g. after address resolution refines the port.
     *
     *  @param endpoint  The updated remote endpoint.
     */
    void
    remoteEndpoint(beast::IP::Endpoint const& endpoint)
    {
        remote_endpoint_ = endpoint;
    }

    /** Record the peer's public key once the overlay handshake completes.
     *
     *  `Logic::activate` uses the key as a deduplication guard — a second slot
     *  presenting the same key is rejected as a duplicate peer.
     *
     *  @param key  The peer's node public key.
     */
    void
    publicKey(PublicKey const& key)
    {
        public_key_ = key;
    }

    /** Mark this slot as reserved (cluster member or explicit reservation).
     *
     *  Reserved slots bypass the public slot cap in `Counts::can_activate`.
     *  Must only be set during or after `activate()`, once the peer's identity
     *  is known from the handshake.
     *
     *  @param reserved  `true` to grant reserved status.
     */
    void
    reserved(bool reserved)
    {
        reserved_ = reserved;
    }

    //--------------------------------------------------------------------------

    /** Transition the slot to a new lifecycle state, enforcing state-machine rules.
     *
     *  This setter handles all transitions **except** `State::Active`, which
     *  must go through `activate()` so that `whenAcceptEndpoints` is always
     *  stamped. The following invariants are asserted (debug builds abort on
     *  violation):
     *  - `State::Active` is rejected — use `activate()`.
     *  - The target state must differ from the current state.
     *  - `State::Accept` and `State::Connect` are entry-only; cannot be
     *    re-entered after construction.
     *  - `State::Connected` is only reachable from an outbound slot currently
     *    in `State::Connect`.
     *  - `State::Closing` is forbidden while still in `State::Connect`.
     *
     *  @param state  The desired target state.
     */
    void
    state(State state);

    /** Promote the slot to `State::Active` and arm the endpoint-flood throttle.
     *
     *  This is the **sole** legal path to `State::Active`. It stamps
     *  `whenAcceptEndpoints = now`, initialising the per-slot rate-limit window
     *  that gates acceptance of `mtENDPOINTS` messages. Bypassing this method
     *  (e.g. via the generic `state()` setter) would leave the flood-control
     *  timestamp uninitialised.
     *
     *  Valid prior states are `State::Accept` (inbound) and
     *  `State::Connected` (outbound). Any other prior state aborts in debug.
     *
     *  @param now  Current clock time, recorded as `whenAcceptEndpoints` to
     *      start the per-slot endpoint rate-limiting window.
     */
    void
    activate(clock_type::time_point const& now);

    /** Per-slot deduplication cache for endpoint gossip.
     *
     *  Tracks every `beast::IP::Endpoint` we have either received from this
     *  peer or sent to this peer, keyed by the lowest hop count seen. Used by
     *  `Logic`'s `Handouts` algorithm to suppress redundant re-announcement of
     *  addresses the peer already knows at an equal or shorter hop distance.
     *  Entries expire after `Tuning::kLIVE_CACHE_SECONDS_TO_LIVE` (30 s),
     *  matching the Livecache TTL.
     *
     *  @note The `<=` boundary in both `insert()` and `filter()` is
     *      load-bearing. Changing one without the other breaks the gossip
     *      deduplication contract relied on by `Handouts`.
     */
    class RecentT
    {
    public:
        /** Construct the cache backed by the given clock.
         *
         *  @param clock  Shared clock for TTL-based entry expiry.
         */
        explicit RecentT(clock_type& clock);

        /** Record an endpoint and its hop distance in the deduplication cache.
         *
         *  Called both for endpoints received **from** this peer and for
         *  endpoints we **send to** this peer. If the endpoint is already
         *  cached and the new hop count is less than or equal to the stored
         *  value, the entry is updated and its age is reset. If the new hop
         *  count is strictly greater, the existing (closer) entry is preserved.
         *
         *  @param ep    The endpoint to record.
         *  @param hops  The hop distance at which `ep` was observed or sent.
         */
        void
        insert(beast::IP::Endpoint const& ep, std::uint32_t hops);

        /** Returns `true` if sending this endpoint to the peer would be redundant.
         *
         *  Suppresses the send when the cache holds the endpoint at a hop count
         *  **less than or equal to** `hops`. A peer with closer knowledge of an
         *  address gains nothing from hearing about it again at the same or
         *  greater distance.
         *
         *  @param ep    The endpoint to test.
         *  @param hops  The hop distance at which we would announce `ep`.
         *  @return `true` to suppress the send; `false` if unknown or the
         *      cached distance is strictly greater than `hops`.
         */
        bool
        filter(beast::IP::Endpoint const& ep, std::uint32_t hops);

    private:
        void
        expire();

        friend class SlotImp;
        beast::aged_unordered_map<beast::IP::Endpoint, std::uint32_t> cache_;
    } recent;

    /** Expire stale entries from the `recent` deduplication cache.
     *
     *  Delegates to `RecentT::expire()`, which prunes entries older than
     *  `Tuning::kLIVE_CACHE_SECONDS_TO_LIVE`. Called by `Logic::oncePerSecond`
     *  so that reconnecting peers receive fresh endpoint announcements.
     */
    void
    expire()
    {
        recent.expire();
    }

private:
    bool const inbound_;
    bool const fixed_;
    bool reserved_;
    State state_;
    beast::IP::Endpoint remote_endpoint_;
    std::optional<beast::IP::Endpoint> local_endpoint_;
    std::optional<PublicKey> public_key_;

    static std::int32_t constexpr kUNKNOWN_PORT = -1;
    std::atomic<std::int32_t> listening_port_;

public:
    // DEPRECATED: raw public data members pending a planned refactor of the
    // connectivity-check workflow. Still read and written directly by `Logic`.

    /** Whether the remote address has been reachability-checked.
     *
     *  `true` for outbound slots from construction (TCP connect proves
     *  reachability). For inbound slots, set to `true` by `Logic` once the
     *  async `Checker` probe completes (successfully or not).
     *
     *  @deprecated Will move to a private accessor when the checker workflow
     *      is refactored.
     */
    bool checked;

    /** Whether the remote peer can accept inbound connections at its advertised address.
     *
     *  Only meaningful when `checked == true`. `true` means the connectivity
     *  probe succeeded and the address is publicly reachable; `false` means the
     *  probe failed. Outbound slots initialise this to `true` unconditionally.
     *
     *  @deprecated Will move to a private accessor when the checker workflow
     *      is refactored.
     */
    bool canAccept;

    /** Whether an async connectivity probe for this peer is currently in flight.
     *
     *  Set to `true` by `Logic` when it launches a `Checker::async_connect`
     *  probe; cleared to `false` in the probe completion callback regardless
     *  of outcome. Guards against launching duplicate probes for the same slot.
     *
     *  @deprecated Will move to a private accessor when the checker workflow
     *      is refactored.
     */
    bool connectivityCheckInProgress;

    /** Earliest time at which the next `mtENDPOINTS` message from this peer will be accepted.
     *
     *  Stamped to `now` by `activate()` and then advanced by
     *  `Tuning::kSECONDS_PER_MESSAGE` after each accepted batch. `Logic`
     *  compares `whenAcceptEndpoints > now` to enforce the per-peer endpoint
     *  rate limit; early delivery should trigger a load charge.
     *
     *  @deprecated Will move to a private accessor when the checker workflow
     *      is refactored.
     */
    clock_type::time_point whenAcceptEndpoints;
};

}  // namespace xrpl::PeerFinder
