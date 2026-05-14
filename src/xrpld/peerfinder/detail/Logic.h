/** @file
 *  Central connection-strategy engine for the XRPL PeerFinder subsystem.
 *
 *  Declares `Logic<Checker>`, the policy core that answers every
 *  resource-management question the overlay layer cannot resolve on its own:
 *  which addresses to connect to, which inbound connections to accept, what
 *  endpoint gossip to broadcast, and how to record success or failure. The
 *  `Checker` template parameter is the async TCP reachability prober; the
 *  production implementation is `PeerFinder::Checker` while unit tests inject
 *  a synchronous mock.
 */

#pragma once

#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/detail/Bootcache.h>
#include <xrpld/peerfinder/detail/Counts.h>
#include <xrpld/peerfinder/detail/Fixed.h>
#include <xrpld/peerfinder/detail/Handouts.h>
#include <xrpld/peerfinder/detail/Livecache.h>
#include <xrpld/peerfinder/detail/SlotImp.h>
#include <xrpld/peerfinder/detail/Source.h>
#include <xrpld/peerfinder/detail/Store.h>
#include <xrpld/peerfinder/detail/iosformat.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/random.h>
#include <xrpl/beast/net/IPAddressConversion.h>
#include <xrpl/beast/utility/WrappedSink.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <set>

namespace xrpl::PeerFinder {

/** Central decision-making engine for the XRPL PeerFinder subsystem.
 *
 *  `Logic` answers every policy question the network layer cannot resolve on
 *  its own: which addresses to attempt, which incoming connections to accept,
 *  what endpoint gossip to broadcast, and how to record success or failure.
 *
 *  Six data structures capture the complete topology state:
 *  - `slots` — master table keyed by remote endpoint; every live connection
 *    regardless of state has an entry here.
 *  - `connectedAddresses` — port-stripped multiset used to enforce the per-IP
 *    connection limit (`Config::ipLimit`).
 *  - `keys` — deduplication set of public keys; prevents two connections to the
 *    same cryptographic identity.
 *  - `fixed_` — private map of always-on peer endpoints to their `Fixed`
 *    Fibonacci-backoff records.
 *  - `livecache` — short-lived (30 s TTL) gossip cache populated from
 *    received `mtENDPOINTS` messages.
 *  - `bootcache` — persistent address store backed by the injected `Store`
 *    (SQLite in production).
 *
 *  @par Thread safety
 *  All public methods acquire `lock` (a `std::recursive_mutex`) before
 *  accessing shared state. The recursive variant is required because
 *  `onClosed()` calls `remove()`, which is also independently lockable.
 *
 *  @tparam Checker  Async reachability prober. In production this is
 *      `PeerFinder::Checker`; unit tests may inject a synchronous mock.
 *
 *  @see Counts, Bootcache, Livecache, Fixed, SlotImp
 */
template <class Checker>
class Logic
{
public:
    /** Map type from remote endpoint to slot. Every live connection, inbound
     *  or outbound and regardless of handshake state, has an entry here.
     *  This is the single source of truth for "are we connected to this
     *  address?".
     */
    using Slots = std::map<beast::IP::Endpoint, std::shared_ptr<SlotImp>>;

    /** Journal used for all diagnostic and trace logging within Logic. */
    beast::Journal journal;

    /** Monotonic clock shared with the Livecache, Bootcache, and Fixed records. */
    clock_type& clock;

    /** Persistent address store (SQLite in production) used by `bootcache`. */
    Store& store;

    /** Async reachability prober; called to verify a peer's listening port. */
    Checker& checker;

    /** Guards all mutable state. Recursive because `onClosed` calls `remove`. */
    std::recursive_mutex lock;

    /** Set to `true` by `stop()`; prevents new fetches and terminates running ones. */
    bool stopping = false;

    /** The address source currently being fetched; set so `stop()` can cancel it. */
    std::shared_ptr<Source> fetchSource;

private:
    Config config_;

    Counts counts_;

    /** Always-on peers mapped to their Fibonacci-backoff reconnect state. */
    std::map<beast::IP::Endpoint, Fixed> fixed_;

public:
    /** Short-lived (30 s TTL) gossip cache populated from received `mtENDPOINTS`
     *  messages. Organised internally by hop count; `buildEndpointsForPeers`
     *  reads from this when assembling endpoint broadcasts.
     */
    Livecache<> livecache;

    /** Persistent address store consulted when the livecache is empty and no
     *  outbound attempts are in flight.
     */
    Bootcache bootcache;

    /** Master slot table keyed by remote endpoint. See the `Slots` typedef. */
    Slots slots;

    /** Port-stripped multiset of all connected (or attempting) IP addresses,
     *  used to enforce `Config::ipLimit`. May contain duplicates when multiple
     *  connections share the same host but use different ports.
     */
    std::multiset<beast::IP::Address> connectedAddresses;

    /** Deduplication set of public keys for all active peers.
     *  Prevents two simultaneous connections to the same cryptographic identity.
     */
    std::set<PublicKey> keys;

    /** Dynamic address sources consulted when the bootcache needs refilling. */
    std::vector<std::shared_ptr<Source>> sources;

    /** Next time at which `buildEndpointsForPeers` will broadcast endpoint lists.
     *  Advanced by `Tuning::kSECONDS_PER_MESSAGE` after each broadcast cycle.
     */
    clock_type::time_point whenBroadcast;

    /** Aged set (60 s TTL) of recently attempted remote addresses, shared across
     *  `autoconnect()` calls to prevent rapid reconnection to the same address.
     */
    ConnectHandouts::Squelches squelches;

    //--------------------------------------------------------------------------
public:
    /** Construct a `Logic` instance and apply a default-constructed `Config`.
     *
     *  @param clock   Shared monotonic clock (typically `UptimeClock`).
     *  @param store   Persistent address store used to back the bootcache.
     *  @param checker Async TCP reachability prober for connectivity tests.
     *  @param journal Diagnostic journal for all log output from this object.
     */
    Logic(clock_type& clock, Store& store, Checker& checker, beast::Journal journal)
        : journal(journal)
        , clock(clock)
        , store(store)
        , checker(checker)
        , livecache(clock, journal)
        , bootcache(store, clock, journal)
        , whenBroadcast(clock.now())
        , squelches(clock)
    {
        config({});
    }

    /** Load persistent bootcache state from the backing store.
     *
     *  Must be called once during startup, before any connections are made.
     *  Delegates to `Bootcache::load()` under `lock`.
     */
    void
    load()
    {
        std::scoped_lock const _(lock);
        bootcache.load();
    }

    /** Signal shutdown: cancel any in-progress source fetch and block new ones.
     *
     *  Sets `stopping = true` and calls `cancel()` on `fetchSource` if a fetch
     *  is currently running. After this returns, `fetch()` will no-op on every
     *  subsequent call. Safe to call from any thread.
     */
    void
    stop()
    {
        std::scoped_lock const _(lock);
        stopping = true;
        if (fetchSource != nullptr)
            fetchSource->cancel();
    }

    //--------------------------------------------------------------------------
    //
    // Manager
    //
    //--------------------------------------------------------------------------

    /** Apply a new configuration and propagate slot-count limits to `Counts`.
     *
     *  Replaces the stored `Config` and calls `Counts::onConfig` to recompute
     *  inbound/outbound maximums. Safe to call after startup to adjust limits.
     *
     *  @param c New configuration to apply.
     */
    void
    config(Config const& c)
    {
        std::scoped_lock const _(lock);
        config_ = c;
        counts_.onConfig(config_);
    }

    /** Return a snapshot of the current configuration.
     *
     *  @return A copy of the active `Config` at the time of the call.
     */
    Config
    config()
    {
        std::scoped_lock const _(lock);
        return config_;
    }

    /** Add a single fixed-peer endpoint to the always-on list.
     *
     *  Convenience overload that wraps `ep` in a one-element vector and
     *  delegates to the multi-address overload.
     *
     *  @param name Human-readable label used in log messages.
     *  @param ep   The single remote endpoint to add as a fixed peer.
     */
    void
    addFixedPeer(std::string const& name, beast::IP::Endpoint const& ep)
    {
        addFixedPeer(name, std::vector<beast::IP::Endpoint>{ep});
    }

    /** Add one or more fixed-peer endpoints to the always-on list.
     *
     *  For each address that is not already in `fixed_`, a new `Fixed` record
     *  is created with a zero backoff (immediately eligible). Addresses with
     *  port 0 are rejected with an exception. If `addresses` is empty, a
     *  warning is logged and the call is a no-op.
     *
     *  @param name      Human-readable label (e.g. hostname) for log messages.
     *  @param addresses Resolved endpoints to register as fixed peers.
     *  @throws std::runtime_error if any address has port 0.
     */
    void
    addFixedPeer(std::string const& name, std::vector<beast::IP::Endpoint> const& addresses)
    {
        std::scoped_lock const _(lock);

        if (addresses.empty())
        {
            JLOG(journal.info()) << "Could not resolve fixed slot '" << name << "'";
            return;
        }

        for (auto const& remoteAddress : addresses)
        {
            if (remoteAddress.port() == 0)
            {
                Throw<std::runtime_error>(
                    "Port not specified for address:" + remoteAddress.toString());
            }

            auto result(fixed_.emplace(
                std::piecewise_construct,
                std::forward_as_tuple(remoteAddress),
                std::make_tuple(std::ref(clock))));

            if (result.second)
            {
                JLOG(journal.debug())
                    << beast::Leftw(18) << "Logic add fixed '" << name << "' at " << remoteAddress;
                return;
            }
        }
    }

    //--------------------------------------------------------------------------

    /** Handle completion of an async connectivity test initiated by `onEndpoints`.
     *
     *  If `ec == boost::asio::error::operation_aborted` the check was cancelled
     *  during shutdown and the callback returns immediately without acquiring
     *  `lock`.  Otherwise the slot identified by `remoteAddress` is looked up;
     *  if the connection has already closed, the result is silently discarded.
     *
     *  On success (`!ec`): marks `slot.canAccept = true` and records the
     *  peer's listening port so subsequent `onEndpoints` calls can add it to
     *  the livecache.  On failure: marks `slot.canAccept = false` and calls
     *  `bootcache.onFailure(checkedAddress)`.
     *
     *  @param remoteAddress  Remote endpoint of the slot that triggered the
     *      check; used as the `slots` map key.
     *  @param checkedAddress The address/port that was probed by `Checker`.
     *  @param ec             Result of the async TCP connect attempt.
     */
    void
    checkComplete(
        beast::IP::Endpoint const& remoteAddress,
        beast::IP::Endpoint const& checkedAddress,
        boost::system::error_code ec)
    {
        if (ec == boost::asio::error::operation_aborted)
            return;

        std::scoped_lock const _(lock);
        auto const iter(slots.find(remoteAddress));
        if (iter == slots.end())
        {
            // The slot disconnected before we finished the check
            JLOG(journal.debug()) << beast::Leftw(18) << "Logic tested " << checkedAddress
                                  << " but the connection was closed";
            return;
        }

        SlotImp& slot(*iter->second);
        slot.checked = true;
        slot.connectivityCheckInProgress = false;

        beast::WrappedSink sink{journal.sink(), slot.prefix()};
        beast::Journal const journal{sink};

        if (ec)
        {
            // VFALCO TODO Should we retry depending on the error?
            slot.canAccept = false;
            JLOG(journal.error()) << "Logic testing " << iter->first << " with error, "
                                  << ec.message();
            bootcache.onFailure(checkedAddress);
            return;
        }

        slot.canAccept = true;
        slot.setListeningPort(checkedAddress.port());
        JLOG(journal.debug()) << "Logic testing " << checkedAddress << " succeeded";
    }

    //--------------------------------------------------------------------------

    /** Register a new inbound connection and allocate its slot.
     *
     *  Applies two gatekeeping checks before creating the slot:
     *  1. Per-IP connection limit (`Config::ipLimit`) — enforced only for
     *     public remote addresses; RFC-private addresses are exempt.
     *  2. Duplicate detection — if `slots` already contains `remoteEndpoint`,
     *     the connection is a duplicate and is rejected.
     *
     *  On success the slot is created in `Slot::State::Accept`, inserted into
     *  `slots` and `connectedAddresses`, and `Counts` is updated.
     *
     *  @param localEndpoint  The local socket endpoint (TLS listener address).
     *  @param remoteEndpoint The remote peer's TCP endpoint.
     *  @return A pair of `{slot, Result::Success}` on success, or
     *      `{nullptr, Result::IpLimitExceeded}` / `{nullptr, Result::DuplicatePeer}`
     *      on rejection.
     */
    std::pair<SlotImp::ptr, Result>
    newInboundSlot(
        beast::IP::Endpoint const& localEndpoint,
        beast::IP::Endpoint const& remoteEndpoint)
    {
        JLOG(journal.debug()) << beast::Leftw(18) << "Logic accept" << remoteEndpoint
                              << " on local " << localEndpoint;

        std::scoped_lock const _(lock);

        // Check for connection limit per address
        if (isPublic(remoteEndpoint))
        {
            auto const count = connectedAddresses.count(remoteEndpoint.address());
            if (count + 1 > config_.ipLimit)
            {
                JLOG(journal.debug()) << beast::Leftw(18) << "Logic dropping inbound "
                                      << remoteEndpoint << " because of ip limits.";
                return {SlotImp::ptr(), Result::IpLimitExceeded};
            }
        }

        // Check for duplicate connection
        if (slots.contains(remoteEndpoint))
        {
            JLOG(journal.debug()) << beast::Leftw(18) << "Logic dropping " << remoteEndpoint
                                  << " as duplicate incoming";
            return {SlotImp::ptr(), Result::DuplicatePeer};
        }

        // Create the slot
        SlotImp::ptr const slot(
            std::make_shared<SlotImp>(
                localEndpoint, remoteEndpoint, fixed(remoteEndpoint.address()), clock));
        // Add slot to table
        auto const result(slots.emplace(slot->remoteEndpoint(), slot));
        // Remote address must not already exist
        XRPL_ASSERT(
            result.second,
            "xrpl::PeerFinder::Logic::new_inbound_slot : remote endpoint "
            "inserted");
        // Add to the connected address list
        connectedAddresses.emplace(remoteEndpoint.address());

        // Update counts
        counts_.add(*slot);

        return {result.first->second, Result::Success};
    }

    /** Register a new outbound connection attempt and allocate its slot.
     *
     *  Creates a slot in `Slot::State::Connect` for the given remote endpoint.
     *  Rejects duplicate attempts (same `remoteEndpoint` already in `slots`).
     *  Self-connect detection is deferred to `onConnected()` because the local
     *  endpoint is not yet known at this stage.
     *
     *  On success the slot is inserted into `slots` and `connectedAddresses`,
     *  and `Counts` is updated to reflect the in-flight attempt.
     *
     *  @param remoteEndpoint The remote peer's TCP endpoint to connect to.
     *  @return `{slot, Result::Success}` or `{nullptr, Result::DuplicatePeer}`.
     */
    std::pair<SlotImp::ptr, Result>
    newOutboundSlot(beast::IP::Endpoint const& remoteEndpoint)
    {
        JLOG(journal.debug()) << beast::Leftw(18) << "Logic connect " << remoteEndpoint;

        std::scoped_lock const _(lock);

        // Check for duplicate connection
        if (slots.contains(remoteEndpoint))
        {
            JLOG(journal.debug()) << beast::Leftw(18) << "Logic dropping " << remoteEndpoint
                                  << " as duplicate connect";
            return {SlotImp::ptr(), Result::DuplicatePeer};
        }

        // Create the slot
        SlotImp::ptr const slot(
            std::make_shared<SlotImp>(remoteEndpoint, fixed(remoteEndpoint), clock));

        // Add slot to table
        auto const result = slots.emplace(slot->remoteEndpoint(), slot);
        // Remote address must not already exist
        XRPL_ASSERT(
            result.second,
            "xrpl::PeerFinder::Logic::new_outbound_slot : remote endpoint "
            "inserted");

        // Add to the connected address list
        connectedAddresses.emplace(remoteEndpoint.address());

        // Update counts
        counts_.add(*slot);

        return {result.first->second, Result::Success};
    }

    /** Notify Logic that a TCP connection has been established for an outbound slot.
     *
     *  Records the now-known `localEndpoint` on the slot and performs
     *  self-connect detection: if `localEndpoint` already appears as a remote
     *  endpoint in `slots`, this connection loops back to ourselves and must be
     *  torn down. Advances the slot state from `Connect` to `Connected` and
     *  updates `Counts`.
     *
     *  @param slot          The outbound slot that just completed TCP connect.
     *  @param localEndpoint The local socket address assigned by the OS.
     *  @return `true` if the connection is valid and should proceed to TLS
     *      handshake; `false` if it was detected as a self-connect.
     */
    bool
    onConnected(SlotImp::ptr const& slot, beast::IP::Endpoint const& localEndpoint)
    {
        beast::WrappedSink sink{journal.sink(), slot->prefix()};
        beast::Journal const journal{sink};

        JLOG(journal.trace()) << "Logic connected on local " << localEndpoint;

        std::scoped_lock const _(lock);

        // The object must exist in our table
        XRPL_ASSERT(
            slots.contains(slot->remoteEndpoint()),
            "xrpl::PeerFinder::Logic::onConnected : valid slot input");
        // Assign the local endpoint now that it's known
        slot->localEndpoint(localEndpoint);

        // Check for self-connect by address
        {
            auto const iter(slots.find(localEndpoint));
            if (iter != slots.end())
            {
                XRPL_ASSERT(
                    iter->second->localEndpoint() == slot->remoteEndpoint(),
                    "xrpl::PeerFinder::Logic::onConnected : local and remote "
                    "endpoints do match");
                JLOG(journal.warn()) << "Logic dropping as self connect";
                return false;
            }
        }

        // Update counts
        counts_.remove(*slot);
        slot->state(Slot::State::Connected);
        counts_.add(*slot);
        return true;
    }

    /** Promote a handshaked slot to active status after XRPL protocol handshake.
     *
     *  This is the final gate before a peer is considered a full, active
     *  connection.  Three checks must pass:
     *  1. `key` must not already be in `keys` (prevents duplicate identity).
     *  2. `counts_.canActivate(*slot)` must return `true` (capacity check),
     *     unless the slot is fixed or reserved — those bypass slot limits.
     *  3. The slot must currently be in `Accept` or `Connected` state.
     *
     *  On success: registers `key` in `keys`, transitions the slot to
     *  `Slot::State::Active`, updates `Counts`, and records a bootcache success
     *  for outbound slots. For outbound fixed slots, advances the `Fixed`
     *  success state (resets backoff).
     *
     *  @param slot     The slot that completed the XRPL handshake.
     *  @param key      The peer's node public key as exchanged in the handshake.
     *  @param reserved `true` if the peer is in the reservation table or cluster.
     *  @return `Result::Success` if the slot was activated;
     *      `Result::DuplicatePeer` if `key` is already connected;
     *      `Result::Full` if all ordinary slots of the appropriate direction
     *      are occupied; `Result::InboundDisabled` if inbound is configured off.
     */
    Result
    activate(SlotImp::ptr const& slot, PublicKey const& key, bool reserved)
    {
        beast::WrappedSink sink{journal.sink(), slot->prefix()};
        beast::Journal const journal{sink};

        JLOG(journal.debug()) << "Logic handshake " << slot->remoteEndpoint() << " with "
                              << (reserved ? "reserved " : "") << "key " << key;

        std::scoped_lock const _(lock);

        // The object must exist in our table
        XRPL_ASSERT(
            slots.contains(slot->remoteEndpoint()),
            "xrpl::PeerFinder::Logic::activate : valid slot input");
        // Must be accepted or connected
        XRPL_ASSERT(
            slot->state() == Slot::State::Accept || slot->state() == Slot::State::Connected,
            "xrpl::PeerFinder::Logic::activate : valid slot state");

        // Check for duplicate connection by key
        if (keys.contains(key))
            return Result::DuplicatePeer;

        // If the peer belongs to a cluster or is reserved,
        // update the slot to reflect that.
        counts_.remove(*slot);
        slot->reserved(reserved);
        counts_.add(*slot);

        // See if we have an open space for this slot
        if (!counts_.canActivate(*slot))
        {
            if (!slot->inbound())
                bootcache.onSuccess(slot->remoteEndpoint());
            if (slot->inbound() && counts_.inMax() == 0)
                return Result::InboundDisabled;
            return Result::Full;
        }

        // Set the key right before adding to the map, otherwise we might
        // assert later when erasing the key.
        slot->publicKey(key);
        {
            [[maybe_unused]] bool const inserted = keys.insert(key).second;
            // Public key must not already exist
            XRPL_ASSERT(inserted, "xrpl::PeerFinder::Logic::activate : public key inserted");
        }

        // Change state and update counts
        counts_.remove(*slot);
        slot->activate(clock.now());
        counts_.add(*slot);

        if (!slot->inbound())
            bootcache.onSuccess(slot->remoteEndpoint());

        // Mark fixed slot success
        if (slot->fixed() && !slot->inbound())
        {
            auto iter(fixed_.find(slot->remoteEndpoint()));
            if (iter == fixed_.end())
            {
                logicError(
                    "PeerFinder::Logic::activate(): remote_endpoint "
                    "missing from fixed_");
            }

            iter->second.success(clock.now());
            JLOG(journal.trace()) << "Logic fixed success";
        }

        return Result::Success;
    }

    /** Return a list of livecache addresses to send as a redirect response.
     *
     *  Legacy function: redirects are now returned in the HTTP 503 handshake
     *  body rather than via `TMEndpoints`. Shuffles the livecache hop list
     *  and feeds it through the `handout()` algorithm into a
     *  `RedirectHandouts` receiver for the given slot.
     *
     *  @param slot The slot being redirected; used to filter already-known
     *      addresses via its `recent` cache.
     *  @return A vector of `Endpoint` objects suitable for a redirect message.
     */
    std::vector<Endpoint>
    redirect(SlotImp::ptr const& slot)
    {
        std::scoped_lock const _(lock);
        RedirectHandouts h(slot);
        livecache.hops.shuffle();
        handout(&h, (&h) + 1, livecache.hops.begin(), livecache.hops.end());
        return std::move(h.list());
    }

    /** Compute the next batch of outbound connection addresses.
     *
     *  Implements the four-tier outbound connection strategy. Returns early at
     *  the first tier that produces candidates, preventing redundant attempts:
     *
     *  1. **Fixed peers** — if fewer fixed connections are active than
     *     configured, `getFixed()` scans `fixed_` for eligible (backoff
     *     elapsed, not already connected or squelched) entries.  If none are
     *     ready but outbound attempts are in flight, returns empty and waits.
     *  2. **Livecache** — shuffled and iterated in reverse-hop order (highest
     *     hops first for topological diversity) via `ConnectHandouts`.
     *  3. **Bootcache refill** — DNS-based placeholder (not yet implemented).
     *  4. **Bootcache fallback** — iterates bootcache entries until the
     *     `ConnectHandouts` receiver is full.
     *
     *  Between tiers, if in-flight attempts already exist but a tier produced
     *  no new candidates, the call returns an empty list to avoid a
     *  thundering-herd reconnect storm. The `squelches` aged set (60 s TTL)
     *  prevents rapid reconnection to the same address across calls.
     *
     *  @return Endpoints to attempt; may be empty if no candidates are
     *      available or if existing attempts are still pending.
     *
     *  @note Must be called periodically (e.g. once per `kSECONDS_PER_CONNECT`)
     *      by the owning `ManagerImp` timer callback.
     */
    // VFALCO TODO This should add the returned addresses to the
    //             squelch list in one go once the list is built,
    //             rather than having each module add to the squelch list.
    std::vector<beast::IP::Endpoint>
    autoconnect()
    {
        std::vector<beast::IP::Endpoint> none;

        std::scoped_lock const _(lock);

        // Count how many more outbound attempts to make
        //
        auto needed(counts_.attemptsNeeded());
        if (needed == 0)
            return none;

        ConnectHandouts h(needed, squelches);

        // Make sure we don't connect to already-connected entries.
        for (auto const& s : slots)
        {
            auto const result(squelches.insert(s.second->remoteEndpoint().address()));
            if (!result.second)
                squelches.touch(result.first);
        }

        // 1. Use Fixed if:
        //    Fixed active count is below fixed count AND
        //      ( There are eligible fixed addresses to try OR
        //        Any outbound attempts are in progress)
        //
        if (counts_.fixedActive() < fixed_.size())
        {
            getFixed(needed, h.list(), squelches);

            if (!h.list().empty())
            {
                JLOG(journal.debug())
                    << beast::Leftw(18) << "Logic connect " << h.list().size() << " fixed";
                return h.list();
            }

            if (counts_.attempts() > 0)
            {
                JLOG(journal.debug())
                    << beast::Leftw(18) << "Logic waiting on " << counts_.attempts() << " attempts";
                return none;
            }
        }

        // Only proceed if auto connect is enabled and we
        // have less than the desired number of outbound slots
        //
        if (!config_.autoConnect || counts_.outActive() >= counts_.outMax())
            return none;

        // 2. Use Livecache if:
        //    There are any entries in the cache OR
        //    Any outbound attempts are in progress
        //
        {
            livecache.hops.shuffle();
            handout(&h, (&h) + 1, livecache.hops.rbegin(), livecache.hops.rend());
            if (!h.list().empty())
            {
                JLOG(journal.debug())
                    << beast::Leftw(18) << "Logic connect " << h.list().size() << " live "
                    << ((h.list().size() > 1) ? "endpoints" : "endpoint");
                return h.list();
            }
            if (counts_.attempts() > 0)
            {
                JLOG(journal.debug())
                    << beast::Leftw(18) << "Logic waiting on " << counts_.attempts() << " attempts";
                return none;
            }
        }

        /*  3. Bootcache refill
            If the Bootcache is empty, try to get addresses from the current
            set of Sources and add them into the Bootstrap cache.

            Pseudocode:
                If (    domainNames.count() > 0 AND (
                           unusedBootstrapIPs.count() == 0
                        OR activeNameResolutions.count() > 0) )
                    ForOneOrMore (DomainName that hasn't been resolved recently)
                        Contact DomainName and add entries to the
           unusedBootstrapIPs return;
        */

        // 4. Use Bootcache if:
        //    There are any entries we haven't tried lately
        //
        for (auto iter(bootcache.begin()); !h.full() && iter != bootcache.end(); ++iter)
            h.tryInsert(*iter);

        if (!h.list().empty())
        {
            JLOG(journal.debug()) << beast::Leftw(18) << "Logic connect " << h.list().size()
                                  << " boot " << ((h.list().size() > 1) ? "addresses" : "address");
            return h.list();
        }

        // If we get here we are stuck
        return none;
    }

    /** Assemble the endpoint lists to broadcast to each active peer.
     *
     *  Called periodically (rate-limited to one pass per
     *  `Tuning::kSECONDS_PER_MESSAGE`).  On each cycle:
     *  1. Collects all `Slot::State::Active` slots, shuffles them to vary
     *     broadcast order across cycles.
     *  2. If `config_.wantIncoming` and inbound slots are available,
     *     injects a self-advertisement entry with `hops == 0` and the
     *     all-zeros IPv6 address (recipients substitute the socket's remote
     *     address, sidestepping the "what is my public IP" problem).
     *  3. Distributes livecache entries fairly across all target slots via
     *     the `handout()` round-robin algorithm.
     *  4. Advances `whenBroadcast` by `Tuning::kSECONDS_PER_MESSAGE`.
     *
     *  @return A vector of `{slot, endpoints}` pairs. The caller (typically
     *      `ManagerImp`) is responsible for serialising each list into a
     *      `mtENDPOINTS` message and sending it on the corresponding slot.
     */
    std::vector<std::pair<std::shared_ptr<Slot>, std::vector<Endpoint>>>
    buildEndpointsForPeers()
    {
        std::vector<std::pair<std::shared_ptr<Slot>, std::vector<Endpoint>>> result;

        std::scoped_lock const _(lock);

        clock_type::time_point const now = clock.now();
        if (whenBroadcast <= now)
        {
            std::vector<SlotHandouts> targets;

            {
                // build list of active slots
                std::vector<SlotImp::ptr> activeSlots;
                activeSlots.reserve(slots.size());
                std::for_each(
                    slots.cbegin(), slots.cend(), [&activeSlots](Slots::value_type const& value) {
                        if (value.second->state() == Slot::State::Active)
                            activeSlots.emplace_back(value.second);
                    });
                std::shuffle(activeSlots.begin(), activeSlots.end(), defaultPrng());

                // build target vector
                targets.reserve(activeSlots.size());
                std::for_each(
                    activeSlots.cbegin(), activeSlots.cend(), [&targets](SlotImp::ptr const& slot) {
                        targets.emplace_back(slot);
                    });
            }

            /* VFALCO NOTE
                This is a temporary measure. Once we know our own IP
                address, the correct solution is to put it into the Livecache
                at hops 0, and go through the regular handout path. This way
                we avoid handing our address out too frequently, which this code
                suffers from.
            */
            // Add an entry for ourselves if:
            // 1. We want incoming
            // 2. We have slots
            // 3. We haven't failed the firewalled test
            //
            if (config_.wantIncoming && counts_.inMax() > 0)
            {
                Endpoint ep;
                ep.hops = 0;
                // we use the unspecified (0) address here because the value is
                // irrelevant to recipients. When peers receive an endpoint
                // with 0 hops, they use the socket remote_addr instead of the
                // value in the message. Furthermore, since the address value
                // is ignored, the type/version (ipv4 vs ipv6) doesn't matter
                // either. ipv6 has a slightly more compact string
                // representation of 0, so use that for self entries.
                ep.address =
                    beast::IP::Endpoint(beast::IP::AddressV6()).atPort(config_.listeningPort);
                for (auto& t : targets)
                    t.insert(ep);
            }

            // build sequence of endpoints by hops
            livecache.hops.shuffle();
            handout(targets.begin(), targets.end(), livecache.hops.begin(), livecache.hops.end());

            // broadcast
            for (auto const& t : targets)
            {
                SlotImp::ptr const& slot = t.slot();
                auto const& list = t.list();
                beast::WrappedSink sink{journal.sink(), slot->prefix()};
                beast::Journal const journal{sink};
                JLOG(journal.trace()) << "Logic sending " << list.size()
                                      << ((list.size() == 1) ? " endpoint" : " endpoints");
                result.emplace_back(slot, list);
            }

            whenBroadcast = now + Tuning::kSECONDS_PER_MESSAGE;
        }

        return result;
    }

    /** Perform periodic maintenance tasks, called once per second.
     *
     *  Under `lock`, in order:
     *  - Expires stale livecache entries.
     *  - Expires the `recent` address cache in each slot.
     *  - Expires stale entries from the `squelches` aged set.
     *  - Calls `Bootcache::periodicActivity` for cooldown-throttled SQLite
     *    writes and cache pruning.
     */
    void
    oncePerSecond()
    {
        std::scoped_lock const _(lock);

        // Expire the Livecache
        livecache.expire();

        // Expire the recent cache in each slot
        for (auto const& entry : slots)
            entry.second->expire();

        // Expire the recent attempts table
        beast::expire(squelches, Tuning::kRECENT_ATTEMPT_DURATION);

        bootcache.periodicActivity();
    }

    //--------------------------------------------------------------------------

    /** Validate and normalise an endpoint list received from a peer.
     *
     *  Mutates `list` in place, removing entries that fail any check and
     *  incrementing the hop count of surviving entries by one (so that when
     *  the entries are later retransmitted, the hop reflects our own distance
     *  to the origin):
     *
     *  - Entries with `hops > Tuning::kMAX_HOPS` (6) are dropped.
     *  - The first `hops == 0` entry is treated as the sender's self-
     *    advertisement: its IP is replaced with the sender's actual socket
     *    address (the sender does not know its own public IP).  Any subsequent
     *    `hops == 0` entries are dropped as duplicates.
     *  - Non-public or unspecified addresses are dropped.
     *  - Addresses duplicated within the list are dropped (O(n²) scan,
     *    acceptable for small lists bounded by `kNUMBER_OF_ENDPOINTS_MAX`).
     *
     *  @param slot The slot from which the endpoint list was received; used to
     *      substitute the sender's socket address for zero-hop entries.
     *  @param list The endpoint list to validate and normalise; modified in place.
     */
    void
    preprocess(SlotImp::ptr const& slot, Endpoints& list)
    {
        bool neighbor(false);
        for (auto iter = list.begin(); iter != list.end();)
        {
            Endpoint& ep(*iter);

            // Enforce hop limit
            if (ep.hops > Tuning::kMAX_HOPS)
            {
                JLOG(journal.debug()) << beast::Leftw(18) << "Endpoints drop " << ep.address
                                      << " for excess hops " << ep.hops;
                iter = list.erase(iter);
                continue;
            }

            // See if we are directly connected
            if (ep.hops == 0)
            {
                if (!neighbor)
                {
                    // Fill in our neighbors remote address
                    neighbor = true;
                    ep.address = slot->remoteEndpoint().atPort(ep.address.port());
                }
                else
                {
                    JLOG(journal.debug())
                        << beast::Leftw(18) << "Endpoints drop " << ep.address << " for extra self";
                    iter = list.erase(iter);
                    continue;
                }
            }

            // Discard invalid addresses
            if (!isValidAddress(ep.address))
            {
                JLOG(journal.debug())
                    << beast::Leftw(18) << "Endpoints drop " << ep.address << " as invalid";
                iter = list.erase(iter);
                continue;
            }

            // Filter duplicates
            if (std::any_of(list.begin(), iter, [ep](Endpoints::value_type const& other) {
                    return ep.address == other.address;
                }))
            {
                JLOG(journal.debug())
                    << beast::Leftw(18) << "Endpoints drop " << ep.address << " as duplicate";
                iter = list.erase(iter);
                continue;
            }

            // Increment hop count on the incoming message, so
            // we store it at the hop count we will send it at.
            //
            ++ep.hops;

            ++iter;
        }
    }

    /** Process an `mtENDPOINTS` gossip message received from an active peer.
     *
     *  Oversized lists are randomly sampled down to
     *  `Tuning::kNUMBER_OF_ENDPOINTS_MAX` before any other processing.
     *  A per-slot rate limit (`Tuning::kSECONDS_PER_MESSAGE`, 151 s) prevents
     *  flooding — messages arriving faster than this are silently dropped.
     *
     *  After the rate check, `preprocess()` cleans the list.  For each
     *  surviving entry:
     *  - The address is recorded in the slot's `recent` cache.
     *  - For first-hop entries (`hops == 1` after increment), if the slot has
     *    not yet been connectivity-tested, `checker.asyncConnect()` is called
     *    and `connectivityCheckInProgress` is set; the first such entry is
     *    discarded pending the result.  If the test already failed
     *    (`!slot->canAccept`), the entry is dropped entirely.
     *  - Entries that survive are added to `livecache` and `bootcache`.
     *
     *  @param slot The active slot from which the message was received.
     *  @param list The deserialized endpoint list (taken by value for mutation).
     *
     *  @note The slot must be in `Slot::State::Active`; the assert fires otherwise.
     */
    void
    onEndpoints(SlotImp::ptr const& slot, Endpoints list)
    {
        beast::WrappedSink sink{journal.sink(), slot->prefix()};
        beast::Journal const journal{sink};

        // If we're sent too many endpoints, sample them at random:
        if (list.size() > Tuning::kNUMBER_OF_ENDPOINTS_MAX)
        {
            std::shuffle(list.begin(), list.end(), defaultPrng());
            list.resize(Tuning::kNUMBER_OF_ENDPOINTS_MAX);
        }

        JLOG(journal.trace()) << "Endpoints contained " << list.size()
                              << ((list.size() > 1) ? " entries" : " entry");

        std::scoped_lock const _(lock);

        // The object must exist in our table
        XRPL_ASSERT(
            slots.contains(slot->remoteEndpoint()),
            "xrpl::PeerFinder::Logic::onEndpoints : valid slot input");

        // Must be handshaked!
        XRPL_ASSERT(
            slot->state() == Slot::State::Active,
            "xrpl::PeerFinder::Logic::onEndpoints : valid slot state");

        clock_type::time_point const now(clock.now());

        // Limit how often we accept new endpoints
        if (slot->whenAcceptEndpoints > now)
            return;

        preprocess(slot, list);

        for (auto const& ep : list)
        {
            XRPL_ASSERT(ep.hops, "xrpl::PeerFinder::Logic::onEndpoints : nonzero hops");

            slot->recent.insert(ep.address, ep.hops);

            // Note hops has been incremented, so 1
            // means a directly connected neighbor.
            //
            if (ep.hops == 1)
            {
                if (slot->connectivityCheckInProgress)
                {
                    JLOG(journal.debug())
                        << "Logic testing " << ep.address << " already in progress";
                    continue;
                }

                if (!slot->checked)
                {
                    // Mark that a check for this slot is now in progress.
                    slot->connectivityCheckInProgress = true;

                    // Test the slot's listening port before
                    // adding it to the livecache for the first time.
                    //
                    checker.asyncConnect(
                        ep.address,
                        std::bind(
                            &Logic::checkComplete,
                            this,
                            slot->remoteEndpoint(),
                            ep.address,
                            std::placeholders::_1));

                    // Note that we simply discard the first Endpoint
                    // that the neighbor sends when we perform the
                    // listening test. They will just send us another
                    // one in a few seconds.

                    continue;
                }

                // If they failed the test then skip the address
                if (!slot->canAccept)
                    continue;
            }

            // We only add to the livecache if the neighbor passed the
            // listening test, else we silently drop neighbor endpoint
            // since their listening port is misconfigured.
            //
            livecache.insert(ep);
            bootcache.insert(ep.address);
        }

        slot->whenAcceptEndpoints = now + Tuning::kSECONDS_PER_MESSAGE;
    }

    //--------------------------------------------------------------------------

    /** Remove a slot from all internal data structures and update `Counts`.
     *
     *  Erases the slot from `slots`, removes its public key from `keys` (if
     *  the key was set), and removes one entry from `connectedAddresses`.  All
     *  three must exist at the time of the call; their absence triggers a
     *  `logicError` (fatal assertion).
     *
     *  Called by `onClosed()`. Also callable independently; the recursive
     *  `lock` allows both paths to hold it simultaneously.
     *
     *  @param slot The slot to remove; must be present in `slots`.
     */
    void
    remove(SlotImp::ptr const& slot)
    {
        {
            auto const iter = slots.find(slot->remoteEndpoint());
            // The slot must exist in the table
            if (iter == slots.end())
            {
                logicError(
                    "PeerFinder::Logic::remove(): remote_endpoint "
                    "missing from slots_");
            }

            // Remove from slot by IP table
            slots.erase(iter);
        }
        // Remove the key if present
        if (slot->publicKey() != std::nullopt)
        {
            auto const iter = keys.find(*slot->publicKey());
            // Key must exist
            if (iter == keys.end())
            {
                logicError(
                    "PeerFinder::Logic::remove(): public_key missing "
                    "from keys_");
            }

            keys.erase(iter);
        }
        // Remove from connected address table
        {
            auto const iter(connectedAddresses.find(slot->remoteEndpoint().address()));
            // Address must exist
            if (iter == connectedAddresses.end())
            {
                logicError(
                    "PeerFinder::Logic::remove(): remote_endpoint "
                    "address missing from connectedAddresses_");
            }

            connectedAddresses.erase(iter);
        }

        // Update counts
        counts_.remove(*slot);
    }

    /** Handle peer disconnection: clean up state and record outcomes.
     *
     *  Calls `remove(slot)` to purge the slot from all tables, then performs
     *  state-specific bookkeeping:
     *  - **Fixed outbound slot not yet active**: calls `Fixed::failure()` to
     *    advance the Fibonacci backoff so the next reconnect attempt is delayed.
     *  - **`Connect` / `Connected` state**: calls `bootcache.onFailure()` to
     *    penalise the address's valence streak.
     *  - **`Active` / `Closing`**: informational log only.
     *
     *  @param slot The slot whose connection just closed.
     */
    void
    onClosed(SlotImp::ptr const& slot)
    {
        std::scoped_lock const _(lock);

        remove(slot);

        beast::WrappedSink sink{journal.sink(), slot->prefix()};
        beast::Journal const journal{sink};

        // Mark fixed slot failure
        if (slot->fixed() && !slot->inbound() && slot->state() != Slot::State::Active)
        {
            auto iter(fixed_.find(slot->remoteEndpoint()));
            if (iter == fixed_.end())
            {
                logicError(
                    "PeerFinder::Logic::on_closed(): remote_endpoint "
                    "missing from fixed_");
            }

            iter->second.failure(clock.now());
            JLOG(journal.debug()) << "Logic fixed failed";
        }

        // Do state specific bookkeeping
        switch (slot->state())
        {
            case Slot::State::Accept:
                JLOG(journal.trace()) << "Logic accept failed";
                break;

            case Slot::State::Connect:
            case Slot::State::Connected:
                bootcache.onFailure(slot->remoteEndpoint());
                // VFALCO TODO If the address exists in the ephemeral/live
                //             endpoint livecache then we should mark the
                //             failure
                // as if it didn't pass the listening test. We should also
                // avoid propagating the address.
                break;

            case Slot::State::Active:
                JLOG(journal.trace()) << "Logic close";
                break;

            case Slot::State::Closing:
                JLOG(journal.trace()) << "Logic finished";
                break;

            // LCOV_EXCL_START
            default:
                UNREACHABLE(
                    "xrpl::PeerFinder::Logic::on_closed : invalid slot "
                    "state");
                break;
                // LCOV_EXCL_STOP
        }
    }

    /** Record a connection failure for bootcache valence accounting.
     *
     *  Called when an outbound connection attempt fails at the TCP or TLS layer
     *  (distinct from `onClosed` which handles clean disconnects).  Delegates
     *  to `Bootcache::onFailure` to decrement the address's valence streak.
     *
     *  @param slot The slot whose outbound attempt failed.
     */
    void
    onFailure(SlotImp::ptr const& slot)
    {
        std::scoped_lock const _(lock);

        bootcache.onFailure(slot->remoteEndpoint());
    }

    /** Insert redirect IP addresses from an HTTP 503 response into the bootcache.
     *
     *  Accepts up to `Tuning::kMAX_REDIRECTS` addresses from the iterator
     *  range `[first, last)`, converts each from the Asio TCP endpoint type to
     *  a `beast::IP::Endpoint`, and calls `bootcache.insert()`.  Addresses
     *  beyond the limit are silently ignored.
     *
     *  @tparam FwdIter Forward iterator over `boost::asio::ip::tcp::endpoint`.
     *  @param first        Beginning of the redirect address range.
     *  @param last         End of the redirect address range.
     *  @param remoteAddress The peer that sent the redirect (for logging only).
     */
    template <class FwdIter>
    void
    onRedirects(FwdIter first, FwdIter last, boost::asio::ip::tcp::endpoint const& remoteAddress);

    //--------------------------------------------------------------------------

    /** Return `true` if `endpoint` exactly matches a configured fixed peer.
     *
     *  Port-sensitive: `192.0.2.1:51235` and `192.0.2.1:51236` are distinct.
     *  Must be called with `lock` held.
     *
     *  @param endpoint The remote endpoint to test.
     */
    bool
    fixed(beast::IP::Endpoint const& endpoint) const
    {
        for (auto const& entry : fixed_)
        {
            if (entry.first == endpoint)
                return true;
        }
        return false;
    }

    /** Return `true` if `address` (without port) matches any fixed peer.
     *
     *  Port-insensitive: any fixed peer on the given IP qualifies, regardless
     *  of port. Used by `newInboundSlot` to tag inbound connections from a
     *  configured fixed-peer host. Must be called with `lock` held.
     *
     *  @param address The remote IP address to test (port ignored).
     */
    bool
    fixed(beast::IP::Address const& address) const
    {
        for (auto const& entry : fixed_)
        {
            if (entry.first.address() == address)
                return true;
        }
        return false;
    }

    //--------------------------------------------------------------------------
    //
    // Connection Strategy
    //
    //--------------------------------------------------------------------------

    /** Populate `c` with fixed-peer endpoints that are eligible for a new connection attempt.
     *
     *  Iterates `fixed_` and appends up to `needed` endpoints that satisfy all
     *  three conditions:
     *  1. The `Fixed::when()` backoff time has elapsed.
     *  2. The address is not already in `squelches`.
     *  3. The address is not already present in `slots` (not connected or
     *     attempting).
     *
     *  Each selected address is also inserted into `squelches` so that the
     *  caller's handout loop does not try to add it again from another tier.
     *
     *  @tparam Container A sequence container supporting `push_back`
     *      (typically `std::vector<beast::IP::Endpoint>`).
     *  @param needed   Maximum number of endpoints to add to `c`.
     *  @param c        Output container receiving eligible endpoints.
     *  @param squelches Aged set of recently attempted addresses (in/out).
     */
    template <class Container>
    void
    getFixed(std::size_t needed, Container& c, typename ConnectHandouts::Squelches& squelches)
    {
        auto const now(clock.now());
        for (auto iter = fixed_.begin(); needed && iter != fixed_.end(); ++iter)
        {
            auto const& address(iter->first.address());
            if (iter->second.when() <= now && squelches.find(address) == squelches.end() &&
                std::none_of(slots.cbegin(), slots.cend(), [address](Slots::value_type const& v) {
                    return address == v.first.address();
                }))
            {
                squelches.insert(iter->first.address());
                c.push_back(iter->first);
                --needed;
            }
        }
    }

    //--------------------------------------------------------------------------

    /** Fetch addresses from `source` immediately and insert them into the bootcache.
     *
     *  Calls `fetch()` synchronously before returning. Intended for static
     *  sources (e.g. `[ips]` config entries) that must be loaded at startup
     *  before any connections are attempted.
     *
     *  @param source The address source to fetch from.
     */
    void
    addStaticSource(std::shared_ptr<Source> const& source)
    {
        fetch(source);
    }

    /** Register a dynamic address source for deferred resolution.
     *
     *  Appends `source` to `sources` for future DNS-based bootcache refill.
     *  The source is not fetched immediately.
     *
     *  @param source The dynamic address source to register.
     */
    void
    addSource(std::shared_ptr<Source> const& source)
    {
        sources.push_back(source);
    }

    //--------------------------------------------------------------------------
    //
    // Bootcache livecache sources
    //
    //--------------------------------------------------------------------------

    /** Insert a set of static addresses into the bootcache with elevated valence.
     *
     *  Delegates to `Bootcache::insertStatic` for each address, which assigns
     *  `staticValence = 32` so configured peers outrank dynamically discovered
     *  ones during sorted iteration. Addresses already present are not
     *  re-inserted.
     *
     *  @param list Addresses to insert (typically resolved from `[ips]` config).
     *  @return The number of addresses that were newly inserted (not already
     *      present in the bootcache).
     */
    int
    addBootcacheAddresses(IPAddresses const& list)
    {
        int count(0);
        std::scoped_lock const _(lock);
        for (auto const& addr : list)
        {
            if (bootcache.insertStatic(addr))
                ++count;
        }
        return count;
    }

    /** Synchronously fetch addresses from `source` and add them to the bootcache.
     *
     *  Guards against shutdown races using a double-checked `stopping` flag:
     *  checked once before starting the synchronous fetch (sets `fetchSource`
     *  so `stop()` can cancel it) and again after the fetch returns (to avoid
     *  processing results after teardown has begun).
     *
     *  On success, calls `addBootcacheAddresses` and logs the count. On error,
     *  logs at `error` level but does not throw.
     *
     *  @note The fetch is currently synchronous (see inline VFALCO note). This
     *      blocks the calling thread for the duration of I/O.
     *
     *  @param source The address source to fetch from.
     */
    void
    fetch(std::shared_ptr<Source> const& source)
    {
        Source::Results results;

        {
            {
                std::scoped_lock const _(lock);
                if (stopping)
                    return;
                fetchSource = source;
            }

            // VFALCO NOTE The fetch is synchronous,
            //             not sure if that's a good thing.
            //
            source->fetch(results, journal);

            {
                std::scoped_lock const _(lock);
                if (stopping)
                    return;
                fetchSource = nullptr;
            }
        }

        if (!results.error)
        {
            int const count(addBootcacheAddresses(results.addresses));
            JLOG(journal.info()) << beast::Leftw(18) << "Logic added " << count << " new "
                                 << ((count == 1) ? "address" : "addresses") << " from "
                                 << source->name();
        }
        else
        {
            JLOG(journal.error()) << beast::Leftw(18) << "Logic failed "
                                  << "'" << source->name() << "' fetch, "
                                  << results.error.message();
        }
    }

    //--------------------------------------------------------------------------
    //
    // Endpoint message handling
    //
    //--------------------------------------------------------------------------

    /** Return `true` if `address` is valid for inclusion in gossip or the bootcache.
     *
     *  Rejects unspecified (0.0.0.0 / ::), non-public (RFC-private, loopback,
     *  multicast), and zero-port addresses. All three checks must pass.
     *
     *  @param address The endpoint to validate.
     */
    bool
    isValidAddress(beast::IP::Endpoint const& address)
    {
        if (isUnspecified(address))
            return false;
        if (!isPublic(address))
            return false;
        if (address.port() == 0)
            return false;
        return true;
    }

    //--------------------------------------------------------------------------
    //
    // PropertyStream
    //
    //--------------------------------------------------------------------------

    /** Serialise all slots into a `PropertyStream::Set` for diagnostics.
     *
     *  Each slot produces one map entry in `set` with keys `local_address`,
     *  `remote_address`, `state`, and optional flags `inbound`, `fixed`,
     *  `reserved`. Used by `onWrite` to populate the `peers` sub-tree of the
     *  administrative property stream.
     *
     *  @param set   Output set to append slot maps into.
     *  @param slots The slot table to serialise.
     */
    void
    writeSlots(beast::PropertyStream::Set& set, Slots const& slots)
    {
        for (auto const& entry : slots)
        {
            beast::PropertyStream::Map item(set);
            SlotImp const& slot(*entry.second);
            if (slot.localEndpoint() != std::nullopt)
                item["local_address"] = to_string(*slot.localEndpoint());
            item["remote_address"] = to_string(slot.remoteEndpoint());
            if (slot.inbound())
                item["inbound"] = "yes";
            if (slot.fixed())
                item["fixed"] = "yes";
            if (slot.reserved())
                item["reserved"] = "yes";

            item["state"] = stateString(slot.state());
        }
    }

    /** Serialise the complete internal state into a `PropertyStream::Map`.
     *
     *  Produces the following sub-trees (consumed by the `peers` RPC endpoint):
     *  - `bootcache` — total entry count (uint32).
     *  - `fixed` — number of configured fixed peers.
     *  - `peers` — per-slot details via `writeSlots`.
     *  - `counts` — aggregated slot counts from `Counts::onWrite`.
     *  - `config` — active configuration from `Config::onWrite`.
     *  - `livecache` — livecache statistics.
     *  - `bootcache` — bootcache statistics (detailed sub-map).
     *
     *  @param map The top-level property stream map to write into.
     */
    void
    onWrite(beast::PropertyStream::Map& map)
    {
        std::scoped_lock const _(lock);

        // VFALCO NOTE These ugly casts are needed because
        //             of how std::size_t is declared on some linuxes
        //
        map["bootcache"] = std::uint32_t(bootcache.size());
        map["fixed"] = std::uint32_t(fixed_.size());

        {
            beast::PropertyStream::Set child("peers", map);
            writeSlots(child, slots);
        }

        {
            beast::PropertyStream::Map child("counts", map);
            counts_.onWrite(child);
        }

        {
            beast::PropertyStream::Map child("config", map);
            config_.onWrite(child);
        }

        {
            beast::PropertyStream::Map child("livecache", map);
            livecache.onWrite(child);
        }

        {
            beast::PropertyStream::Map child("bootcache", map);
            bootcache.onWrite(child);
        }
    }

    //--------------------------------------------------------------------------
    //
    // Diagnostics
    //
    //--------------------------------------------------------------------------

    /** Return the current slot-count snapshot for diagnostic inspection.
     *
     *  Returns a `const` reference valid only while `lock` is held by the
     *  caller. Used by unit tests to verify count invariants after state
     *  transitions.
     */
    Counts const&
    counts() const
    {
        return counts_;
    }

    /** Convert a `Slot::State` enumerator to a human-readable string.
     *
     *  Returns one of: `"accept"`, `"connect"`, `"connected"`, `"active"`,
     *  `"closing"`, or `"?"` for unrecognised values. Used in `writeSlots`
     *  and log messages.
     *
     *  @param state The slot state to convert.
     *  @return A string literal naming the state.
     */
    static std::string
    stateString(Slot::State state)
    {
        switch (state)
        {
            case Slot::State::Accept:
                return "accept";
            case Slot::State::Connect:
                return "connect";
            case Slot::State::Connected:
                return "connected";
            case Slot::State::Active:
                return "active";
            case Slot::State::Closing:
                return "closing";
            default:
                break;
        };
        return "?";
    }
};

//------------------------------------------------------------------------------

template <class Checker>
template <class FwdIter>
void
Logic<Checker>::onRedirects(
    FwdIter first,
    FwdIter last,
    boost::asio::ip::tcp::endpoint const& remoteAddress)
{
    std::scoped_lock const _(lock);
    std::size_t n = 0;
    for (; first != last && n < Tuning::kMAX_REDIRECTS; ++first, ++n)
        bootcache.insert(beast::IPAddressConversion::fromAsio(*first));
    if (n > 0)
    {
        JLOG(journal.trace()) << beast::Leftw(18) << "Logic add " << n << " redirect IPs from "
                              << remoteAddress;
    }
}

}  // namespace xrpl::PeerFinder
