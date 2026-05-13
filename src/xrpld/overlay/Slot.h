/** @file
 *  Per-validator peer selection state machine for the reduce-relay subsystem.
 *
 *  Implements the flooding-suppression scheme that designates a small, fixed
 *  set of peers as the authoritative relay sources for each validator's
 *  proposals and validations, then instructs all other peers to stop
 *  forwarding via timed `TMSquelch` protocol messages.
 *
 *  The file is entirely within the `xrpl::reduce_relay` namespace and is
 *  header-only.  The three cooperating abstractions are:
 *  - `SquelchHandler` — inversion-of-control callback interface
 *  - `Slot<ClockType>` — per-validator peer selection state machine
 *  - `Slots<ClockType>` — container and lifecycle manager for all slots
 *
 *  @see ReduceRelayCommon.h for all shared numeric constants.
 */

#pragma once

#include <xrpld/core/Config.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/ReduceRelayCommon.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/random.h>
#include <xrpl/beast/container/aged_unordered_map.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/messages.h>

#include <algorithm>
#include <optional>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace xrpl::reduce_relay {

template <typename ClockType>
class Slots;

/** Lifecycle state of a single peer within a `Slot`. */
enum class PeerState : uint8_t {
    Counting,   /**< Accumulating messages; eligible to enter the candidate pool. */
    Selected,   /**< Designated relay source for this validator. */
    Squelched,  /**< Instructed to stop forwarding; awaiting squelch expiry. */
};

/** Overall state of a `Slot` for one validator. */
enum class SlotState : uint8_t {
    Counting,  /**< Actively counting peer messages to select relay sources. */
    Selected,  /**< Relay sources chosen; message counting suspended until reset. */
};

/** Convert a time-point to a duration since the clock epoch.
 *
 *  Used internally to produce millisecond-resolution timestamps for logging
 *  and for the `getPeers()` snapshot returned to callers.
 *
 *  @tparam Unit The duration type to cast to (e.g. `std::chrono::milliseconds`).
 *  @tparam TP   The time-point type (clock-specific).
 *  @param t     The time-point to convert.
 *  @return      The duration since the clock epoch, expressed in `Unit`.
 */
template <typename Unit, typename TP>
Unit
epoch(TP const& t)
{
    return std::chrono::duration_cast<Unit>(t.time_since_epoch());
}

/** Inversion-of-control interface for sending squelch/unsquelch commands.
 *
 *  `OverlayImpl` inherits from this class and provides the real implementation
 *  that transmits `TMSquelch` protocol messages to peers.  The indirection
 *  decouples the peer-selection algorithm from the network layer and lets unit
 *  tests inject mock handlers without modifying `Slot` logic.
 */
class SquelchHandler
{
public:
    virtual ~SquelchHandler() = default;

    /** Instruct a peer to suppress relay of messages from a validator.
     *
     *  The implementation must send a `TMSquelch(squelch=true)` message to
     *  the peer identified by `id`.
     *
     *  @param validator Public key of the validator whose messages should be suppressed.
     *  @param id        ID of the peer to squelch.
     *  @param duration  Duration of the squelch window in seconds.
     */
    virtual void
    squelch(PublicKey const& validator, Peer::id_t id, std::uint32_t duration) const = 0;

    /** Lift a previously issued squelch, allowing the peer to relay again.
     *
     *  The implementation must send a `TMSquelch(squelch=false)` message to
     *  the peer identified by `id`.
     *
     *  @param validator Public key of the validator being unsquelched.
     *  @param id        ID of the peer to unsquelch.
     */
    virtual void
    unsquelch(PublicKey const& validator, Peer::id_t id) const = 0;
};

/** Per-validator peer selection state machine for reduce-relay.
 *
 *  Tracks all peers that have forwarded messages from a single validator and
 *  decides which subset should be designated as authoritative relay sources.
 *  Non-selected peers are squelched via `SquelchHandler::squelch()` for a
 *  randomized duration to suppress redundant flooding.
 *
 *  Two independent state machines run in parallel:
 *  - **`SlotState`**: `Counting` (selecting relay sources) or `Selected`
 *    (sources chosen, counting paused).
 *  - **`PeerState`** per peer: `Counting`, `Selected`, or `Squelched`.
 *
 *  Construction is private; only `Slots<ClockType>` (a `friend`) creates
 *  instances, ensuring each slot is always owned by its container.
 *
 *  @tparam ClockType  Monotonic clock used for timeout and idle detection.
 *      Must satisfy the TrivialClock requirements.  Use `UptimeClock` in
 *      production; use a manually-advanced clock in tests.
 *
 *  @note Not thread-safe. All callers must serialize access externally
 *      (e.g., via the overlay strand as done in `OverlayImpl`).
 */
template <typename ClockType>
class Slot final
{
private:
    friend class Slots<ClockType>;
    using id_t = Peer::id_t;
    using time_point = typename ClockType::time_point;

    /** Callback type invoked when a message is received from an already-squelched peer.
     *
     *  Passed through to `update()` so callers can track how often squelched
     *  peers ignore their squelch instructions (counted as
     *  `TrafficCount::squelch_ignored` upstream).
     */
    using ignored_squelch_callback = std::function<void()>;

    /** Construct a slot for a single validator.
     *
     *  Only callable by `Slots<ClockType>` (friend).  `lastSelected_` is
     *  initialised to `ClockType::now()` so the inactivity guard in `update()`
     *  does not immediately reset a freshly created slot.
     *
     *  @param handler          Squelch/unsquelch callback implementation.
     *  @param journal          Journal for trace/warn logging.
     *  @param maxSelectedPeers Maximum relay sources to designate per round.
     */
    Slot(SquelchHandler const& handler, beast::Journal journal, uint16_t maxSelectedPeers)
        : lastSelected_(ClockType::now())
        , handler_(handler)
        , journal_(journal)
        , maxSelectedPeers_(maxSelectedPeers)
    {
    }

    /** Process a validator message received from a peer and advance slot state.
     *
     *  The full selection algorithm runs here:
     *  1. New peer → insert with `PeerState::Counting` and call `initCounting()`.
     *  2. Peer with expired squelch → reset to `Counting` and call `initCounting()`.
     *  3. Inactivity guard: if `lastSelected_` is more than
     *     `2 × kMAX_UNSQUELCH_EXPIRE_DEFAULT` in the past, reset via `initCounting()`.
     *  4. Increment count; once it exceeds `kMIN_MESSAGE_THRESHOLD` the peer
     *     enters the `considered_` candidate pool.
     *  5. When `maxSelectedPeers_` peers each reach `kMAX_MESSAGE_THRESHOLD + 1`,
     *     randomly draw `maxSelectedPeers_` non-idle candidates as relay sources
     *     and squelch the rest.  If fewer than `maxSelectedPeers_` non-idle
     *     candidates exist, `initCounting()` defers to the next round.
     *
     *  @param validator Public key of the source validator.
     *  @param id        ID of the peer that forwarded this message.
     *  @param type      Protocol message type; only `mtVALIDATION` and
     *      `mtPROPOSE_LEDGER` are meaningful (others reserved for future use).
     *  @param callback  Invoked if a message arrives from an already-squelched peer.
     *
     *  @note No-op if the slot is in `SlotState::Selected` and the peer is
     *      not squelched (counting is suspended until the slot resets).
     */
    void
    update(
        PublicKey const& validator,
        id_t id,
        protocol::MessageType type,
        ignored_squelch_callback callback);

    /** React to a peer leaving the relay pool.
     *
     *  If the removed peer was `Selected`, every currently `Squelched` peer
     *  is unsquelched via `SquelchHandler::unsquelch()`, all peer states are
     *  reset to `Counting`, and the slot transitions back to
     *  `SlotState::Counting` to restart selection.  If the peer was only in
     *  the `considered_` pool, `reachedThreshold_` is decremented to keep
     *  counts consistent.
     *
     *  @param validator Public key of the source validator.
     *  @param id        ID of the peer being removed.
     *  @param erase     If `true`, remove the `PeerInfo` entry entirely
     *      (peer disconnected).  If `false`, retain the entry with reset
     *      counts (peer merely went idle — may reconnect later).
     *
     *  @note The `unsquelch()` callbacks are fired *after* the optional
     *      erase, so the peer map is already consistent when callbacks run.
     */
    void
    deletePeer(PublicKey const& validator, id_t id, bool erase);

    /** Time of the most recent completed peer-selection round.
     *
     *  `Slots::deleteIdlePeers()` compares this against the current time to
     *  decide whether the entire slot has gone stale and should be removed.
     *
     *  @return Reference to `lastSelected_`; valid for the lifetime of this slot.
     */
    [[nodiscard]] time_point const&
    getLastSelected() const
    {
        return lastSelected_;
    }

    /** Count peers currently in the given `PeerState`.
     *
     *  @param state The state to filter by.
     *  @return Number of peers in `state`.
     */
    [[nodiscard]] std::uint16_t
    inState(PeerState state) const;

    /** Count peers NOT in the given `PeerState`.
     *
     *  @param state The state to exclude.
     *  @return Number of peers whose state differs from `state`.
     */
    [[nodiscard]] std::uint16_t
    notInState(PeerState state) const;

    /** Current slot-level state.
     *
     *  @return `SlotState::Counting` while selecting relay sources,
     *      `SlotState::Selected` once sources are chosen.
     */
    [[nodiscard]] SlotState
    getState() const
    {
        return state_;
    }

    /** IDs of peers currently in `PeerState::Selected`.
     *
     *  @return Sorted set of selected peer IDs; empty when the slot is in
     *      `SlotState::Counting`.
     */
    [[nodiscard]] std::set<id_t>
    getSelected() const;

    /** Snapshot of all tracked peers for inspection and testing.
     *
     *  @return Map from peer ID to a tuple of
     *      `(PeerState, message_count, squelch_expiry_ms, last_message_ms)`
     *      where timestamps are milliseconds since the clock epoch.
     */
    [[nodiscard]] std::unordered_map<id_t, std::tuple<PeerState, uint16_t, uint32_t, uint32_t>>
    getPeers() const;

    /** Scan all tracked peers and retire any that have gone silent.
     *
     *  A peer is considered idle if `ClockType::now() - lastMessage > kIDLED`.
     *  Idle peers are passed to `deletePeer(..., false)` — their entry is
     *  retained with reset counts so a resumed peer is treated as a new
     *  participant rather than a clean insert.  If an idle peer was
     *  `Selected`, all `Squelched` peers are unsquelched and the slot
     *  reverts to `SlotState::Counting`.
     *
     *  @param validator Public key of the source validator (forwarded to
     *      `deletePeer()` for logging and unsquelch callbacks).
     */
    void
    deleteIdlePeer(PublicKey const& validator);

    /** Compute a randomized squelch window for the current selection round.
     *
     *  The duration is drawn uniformly from
     *  `[kMIN_UNSQUELCH_EXPIRE, min(max(kMAX_UNSQUELCH_EXPIRE_DEFAULT,
     *  kSQUELCH_PER_PEER × npeers), kMAX_UNSQUELCH_EXPIRE_PEERS)]`.
     *  Scaling by `npeers` prevents simultaneous unsquelch storms on
     *  well-connected nodes.
     *
     *  @param npeers Number of peers to be squelched in this round.
     *  @return A randomized squelch duration in seconds.
     */
    std::chrono::seconds
    getSquelchDuration(std::size_t npeers);

private:
    /** Zero the message counts for all peers in `Counting` or `Selected` state.
     *
     *  Called as part of `initCounting()` and after a completed selection
     *  round so that counts accurately reflect activity since the last reset.
     */
    void
    resetCounts();

    /** Transition the slot to `SlotState::Counting` and clear selection state.
     *
     *  Clears `considered_`, resets `reachedThreshold_` to zero, resets all
     *  peer message counts, and sets `state_` to `SlotState::Counting`.
     *  Called whenever a new peer appears, a squelch expires, inactivity is
     *  detected, or a selected peer is lost.
     */
    void
    initCounting();

    /** Per-peer tracking data maintained inside `peers_`. */
    struct PeerInfo
    {
        PeerState state;         /**< Current lifecycle state of this peer. */
        std::size_t count;       /**< Messages received since the last reset. */
        time_point expire;       /**< When the current squelch expires (only meaningful if `Squelched`). */
        time_point lastMessage;  /**< Timestamp of the most recent message from this peer. */
    };

    /** All peers that have ever forwarded a message for this validator. */
    std::unordered_map<id_t, PeerInfo> peers_;

    /** Peers that have surpassed `kMIN_MESSAGE_THRESHOLD` and are
     *  eligible to be randomly selected as relay sources. */
    std::unordered_set<id_t> considered_;

    /** Number of peers in `considered_` that have reached `kMAX_MESSAGE_THRESHOLD`.
     *  Selection fires when this equals `maxSelectedPeers_`. */
    std::uint16_t reachedThreshold_{0};

    /** Timestamp of the most recently completed selection round.
     *  Used by `Slots::deleteIdlePeers()` to age out stale slots. */
    typename ClockType::time_point lastSelected_;

    SlotState state_{SlotState::Counting};  /**< Current slot-level state. */
    SquelchHandler const& handler_;         /**< Squelch/unsquelch callback implementation. */
    beast::Journal const journal_;          /**< Logger. */

    /** Maximum number of peers designated as relay sources per selection round.
     *  Configurable via `Config::VP_REDUCE_RELAY_SQUELCH_MAX_SELECTED_PEERS`
     *  (default 5). */
    uint16_t const maxSelectedPeers_;
};

template <typename ClockType>
void
Slot<ClockType>::deleteIdlePeer(PublicKey const& validator)
{
    using namespace std::chrono;
    auto now = ClockType::now();
    for (auto it = peers_.begin(); it != peers_.end();)
    {
        auto& peer = it->second;
        auto id = it->first;
        ++it;
        if (now - peer.lastMessage > kIDLED)
        {
            JLOG(journal_.trace())
                << "deleteIdlePeer: " << Slice(validator) << " " << id << " idled "
                << duration_cast<seconds>(now - peer.lastMessage).count() << " selected "
                << (peer.state == PeerState::Selected);
            deletePeer(validator, id, false);
        }
    }
}

template <typename ClockType>
void
Slot<ClockType>::update(
    PublicKey const& validator,
    id_t id,
    protocol::MessageType type,
    ignored_squelch_callback callback)
{
    using namespace std::chrono;
    auto now = ClockType::now();
    auto it = peers_.find(id);
    // First message from this peer
    if (it == peers_.end())
    {
        JLOG(journal_.trace()) << "update: adding peer " << Slice(validator) << " " << id;
        peers_.emplace(std::make_pair(id, PeerInfo{PeerState::Counting, 0, now, now}));
        initCounting();
        return;
    }
    // Message from a peer with expired squelch
    if (it->second.state == PeerState::Squelched && now > it->second.expire)
    {
        JLOG(journal_.trace()) << "update: squelch expired " << Slice(validator) << " " << id;
        it->second.state = PeerState::Counting;
        it->second.lastMessage = now;
        initCounting();
        return;
    }

    auto& peer = it->second;

    JLOG(journal_.trace()) << "update: existing peer " << Slice(validator) << " " << id
                           << " slot state " << static_cast<int>(state_) << " peer state "
                           << static_cast<int>(peer.state) << " count " << peer.count << " last "
                           << duration_cast<milliseconds>(now - peer.lastMessage).count()
                           << " pool " << considered_.size() << " threshold " << reachedThreshold_
                           << " " << (type == protocol::mtVALIDATION ? "validation" : "proposal");

    peer.lastMessage = now;

    // report if we received a message from a squelched peer
    if (peer.state == PeerState::Squelched)
        callback();

    if (state_ != SlotState::Counting || peer.state == PeerState::Squelched)
        return;

    if (++peer.count > kMIN_MESSAGE_THRESHOLD)
        considered_.insert(id);
    if (peer.count == (kMAX_MESSAGE_THRESHOLD + 1))
        ++reachedThreshold_;

    if (now - lastSelected_ > 2 * kMAX_UNSQUELCH_EXPIRE_DEFAULT)
    {
        JLOG(journal_.trace()) << "update: resetting due to inactivity " << Slice(validator) << " "
                               << id << " " << duration_cast<seconds>(now - lastSelected_).count();
        initCounting();
        return;
    }

    if (reachedThreshold_ == maxSelectedPeers_)
    {
        // Randomly select maxSelectedPeers_ peers from considered.
        // Exclude peers that have been idling > IDLED -
        // it's possible that deleteIdlePeer() has not been called yet.
        // If number of remaining peers != maxSelectedPeers_
        // then reset the Counting state and let deleteIdlePeer() handle
        // idled peers.
        std::unordered_set<id_t> selected;
        auto const consideredPoolSize = considered_.size();
        while (selected.size() != maxSelectedPeers_ && !considered_.empty())
        {
            auto i = considered_.size() == 1 ? 0 : randInt(considered_.size() - 1);
            auto it = std::next(considered_.begin(), i);
            auto id = *it;
            considered_.erase(it);
            auto const& itPeers = peers_.find(id);
            if (itPeers == peers_.end())
            {
                JLOG(journal_.error())
                    << "update: peer not found " << Slice(validator) << " " << id;
                continue;
            }
            if (now - itPeers->second.lastMessage < kIDLED)
                selected.insert(id);
        }

        if (selected.size() != maxSelectedPeers_)
        {
            JLOG(journal_.trace()) << "update: selection failed " << Slice(validator) << " " << id;
            initCounting();
            return;
        }

        lastSelected_ = now;

        auto s = selected.begin();
        JLOG(journal_.trace()) << "update: " << Slice(validator) << " " << id << " pool size "
                               << consideredPoolSize << " selected " << *s << " "
                               << *std::next(s, 1) << " " << *std::next(s, 2);

        XRPL_ASSERT(
            peers_.size() >= maxSelectedPeers_, "xrpl::reduce_relay::Slot::update : minimum peers");

        // squelch peers which are not selected and
        // not already squelched
        std::stringstream str;
        for (auto& [k, v] : peers_)
        {
            v.count = 0;

            if (selected.find(k) != selected.end())
            {
                v.state = PeerState::Selected;
            }
            else if (v.state != PeerState::Squelched)
            {
                if (journal_.trace())
                    str << k << " ";
                v.state = PeerState::Squelched;
                std::chrono::seconds const duration =
                    getSquelchDuration(peers_.size() - maxSelectedPeers_);
                v.expire = now + duration;
                handler_.squelch(validator, k, duration.count());
            }
        }
        JLOG(journal_.trace()) << "update: squelching " << Slice(validator) << " " << id << " "
                               << str.str();
        considered_.clear();
        reachedThreshold_ = 0;
        state_ = SlotState::Selected;
    }
}

template <typename ClockType>
std::chrono::seconds
Slot<ClockType>::getSquelchDuration(std::size_t npeers)
{
    using namespace std::chrono;
    auto m = std::max(kMAX_UNSQUELCH_EXPIRE_DEFAULT, seconds{kSQUELCH_PER_PEER * npeers});
    if (m > kMAX_UNSQUELCH_EXPIRE_PEERS)
    {
        m = kMAX_UNSQUELCH_EXPIRE_PEERS;
        JLOG(journal_.warn()) << "getSquelchDuration: unexpected squelch duration " << npeers;
    }
    return seconds{xrpl::randInt(kMIN_UNSQUELCH_EXPIRE / 1s, m / 1s)};
}

template <typename ClockType>
void
Slot<ClockType>::deletePeer(PublicKey const& validator, id_t id, bool erase)
{
    auto it = peers_.find(id);
    if (it != peers_.end())
    {
        std::vector<Peer::id_t> toUnsquelch;

        JLOG(journal_.trace()) << "deletePeer: " << Slice(validator) << " " << id << " selected "
                               << (it->second.state == PeerState::Selected) << " considered "
                               << (considered_.contains(id)) << " erase " << erase;
        auto now = ClockType::now();
        if (it->second.state == PeerState::Selected)
        {
            for (auto& [k, v] : peers_)
            {
                if (v.state == PeerState::Squelched)
                    toUnsquelch.push_back(k);
                v.state = PeerState::Counting;
                v.count = 0;
                v.expire = now;
            }

            considered_.clear();
            reachedThreshold_ = 0;
            state_ = SlotState::Counting;
        }
        else if (considered_.contains(id))
        {
            if (it->second.count > kMAX_MESSAGE_THRESHOLD)
                --reachedThreshold_;
            considered_.erase(id);
        }

        it->second.lastMessage = now;
        it->second.count = 0;

        if (erase)
            peers_.erase(it);

        // Must be after peers_.erase(it)
        for (auto const& k : toUnsquelch)
            handler_.unsquelch(validator, k);
    }
}

template <typename ClockType>
void
Slot<ClockType>::resetCounts()
{
    for (auto& [_, peer] : peers_)
    {
        (void)_;
        peer.count = 0;
    }
}

template <typename ClockType>
void
Slot<ClockType>::initCounting()
{
    state_ = SlotState::Counting;
    considered_.clear();
    reachedThreshold_ = 0;
    resetCounts();
}

template <typename ClockType>
std::uint16_t
Slot<ClockType>::inState(PeerState state) const
{
    return std::count_if(
        peers_.begin(), peers_.end(), [&](auto const& it) { return (it.second.state == state); });
}

template <typename ClockType>
std::uint16_t
Slot<ClockType>::notInState(PeerState state) const
{
    return std::count_if(
        peers_.begin(), peers_.end(), [&](auto const& it) { return (it.second.state != state); });
}

template <typename ClockType>
std::set<typename Peer::id_t>
Slot<ClockType>::getSelected() const
{
    std::set<id_t> r;
    for (auto const& [id, info] : peers_)
    {
        if (info.state == PeerState::Selected)
            r.insert(id);
    }
    return r;
}

template <typename ClockType>
std::unordered_map<typename Peer::id_t, std::tuple<PeerState, uint16_t, uint32_t, uint32_t>>
Slot<ClockType>::getPeers() const
{
    using namespace std::chrono;
    auto r = std::
        unordered_map<id_t, std::tuple<PeerState, std::uint16_t, std::uint32_t, std::uint32_t>>();

    for (auto const& [id, info] : peers_)
    {
        r.emplace(
            std::make_pair(
                id,
                std::move(
                    std::make_tuple(
                        info.state,
                        info.count,
                        epoch<milliseconds>(info.expire).count(),
                        epoch<milliseconds>(info.lastMessage).count()))));
    }

    return r;
}

/** Container and lifecycle manager for all per-validator `Slot` instances.
 *
 *  `Slots` is the sole public entry point for the reduce-relay subsystem.
 *  `OverlayImpl` holds a `Slots<UptimeClock>` member and calls
 *  `updateSlotAndSquelch()` each time a validator message arrives.
 *
 *  Key responsibilities:
 *  - Create and look up `Slot` objects keyed by validator `PublicKey`.
 *  - Deduplicate messages via the `peersWithMessage_` aged map so each
 *    (message-hash, peer-ID) pair is counted at most once per `kIDLED` window.
 *  - Enforce the `kWAIT_ON_BOOTUP` boot delay before squelching begins.
 *  - Periodically retire idle peers and stale slots via `deleteIdlePeers()`.
 *
 *  @tparam ClockType  Monotonic clock; same constraint as `Slot<ClockType>`.
 *
 *  @note `peersWithMessage_` is `inline static`, shared across all
 *      instantiations of `Slots<ClockType>`, not per-instance.
 *
 *  @note Not thread-safe.  All callers must serialize externally.
 */
template <typename ClockType>
class Slots final
{
    using time_point = typename ClockType::time_point;
    using id_t = typename Peer::id_t;
    using messages = beast::aged_unordered_map<
        uint256,
        std::unordered_set<Peer::id_t>,
        ClockType,
        HardenedHash<strong_hash>>;

public:
    /** Construct the slot container.
     *
     *  Reads `Config::VP_REDUCE_RELAY_BASE_SQUELCH_ENABLE` and
     *  `Config::VP_REDUCE_RELAY_SQUELCH_MAX_SELECTED_PEERS` to configure
     *  feature enablement and the per-slot peer limit.
     *
     *  @param registry Service registry used to obtain the journal.
     *  @param handler  Squelch/unsquelch callback implementation
     *      (typically `OverlayImpl`).
     *  @param config   Global node configuration.
     */
    Slots(ServiceRegistry& registry, SquelchHandler const& handler, Config const& config)
        : handler_(handler)
        , logs_(registry.getLogs())
        , journal_(registry.getJournal("Slots"))
        , baseSquelchEnabled_(config.VP_REDUCE_RELAY_BASE_SQUELCH_ENABLE)
        , maxSelectedPeers_(config.VP_REDUCE_RELAY_SQUELCH_MAX_SELECTED_PEERS)
    {
    }
    ~Slots() = default;

    /** Return `true` if base squelching is both configured and past the boot delay.
     *
     *  Convenience wrapper combining `baseSquelchEnabled_` and `reduceRelayReady()`.
     *  `OverlayImpl` checks this before invoking squelch logic on inbound messages.
     */
    bool
    baseSquelchReady()
    {
        return baseSquelchEnabled_ && reduceRelayReady();
    }

    /** Return `true` once `kWAIT_ON_BOOTUP` has elapsed since process start.
     *
     *  Activating squelching too early, while the peer set is still forming,
     *  risks designating only the first few connected peers as relay sources
     *  for all validators.  The boot delay ensures a representative sample
     *  exists before any peer is squelched.
     *
     *  @note Result is cached in `reduceRelayReady_`; once `true` it never
     *      reverts to `false` for the lifetime of this object.
     */
    bool
    reduceRelayReady()
    {
        if (!reduceRelayReady_)
        {
            reduceRelayReady_ = reduce_relay::epoch<std::chrono::minutes>(ClockType::now()) >
                reduce_relay::kWAIT_ON_BOOTUP;
        }

        return reduceRelayReady_;
    }

    /** Update the slot for `validator` using a no-op ignored-squelch callback.
     *
     *  Convenience overload for callers that do not need to track squelch
     *  violations.  Deduplicates the message via `addPeerMessage()` and
     *  creates the `Slot` on first call for this validator.
     *
     *  @param key       Hash of the received message (used for deduplication).
     *      A zero hash (`key.isZero()`) bypasses deduplication.
     *  @param validator Public key of the validator that produced the message.
     *  @param id        ID of the peer that forwarded the message.
     *  @param type      Protocol message type.
     */
    void
    updateSlotAndSquelch(
        uint256 const& key,
        PublicKey const& validator,
        id_t id,
        protocol::MessageType type)
    {
        updateSlotAndSquelch(key, validator, id, type, []() {});
    }

    /** Update the slot for `validator` with an explicit ignored-squelch callback.
     *
     *  The message is first checked against `peersWithMessage_`; if the
     *  (hash, peer) pair was already seen within the `kIDLED` window the call
     *  returns immediately without updating any slot.  Otherwise the slot for
     *  `validator` is created if absent, then `Slot::update()` is called.
     *
     *  @param key       Hash of the received message.
     *  @param validator Public key of the source validator.
     *  @param id        ID of the forwarding peer.
     *  @param type      Protocol message type.
     *  @param callback  Invoked when a message arrives from a currently-squelched peer.
     */
    void
    updateSlotAndSquelch(
        uint256 const& key,
        PublicKey const& validator,
        id_t id,
        protocol::MessageType type,
        typename Slot<ClockType>::ignored_squelch_callback callback);

    /** Retire idle peers and stale slots.
     *
     *  For every slot, calls `Slot::deleteIdlePeer()` to identify and clean up
     *  peers that have not forwarded a message within `kIDLED`.  After
     *  processing each slot, removes the entire slot entry if its
     *  `lastSelected_` timestamp is older than `kMAX_UNSQUELCH_EXPIRE_DEFAULT`
     *  — a validator that has stopped producing messages should not leave
     *  permanent state.  Called periodically from `OverlayImpl`'s timer.
     */
    void
    deleteIdlePeers();

    /** Count peers in `state` for the given validator's slot.
     *
     *  @param validator Validator public key.
     *  @param state     Peer state to count.
     *  @return Count, or `std::nullopt` if no slot exists for `validator`.
     */
    [[nodiscard]] std::optional<std::uint16_t>
    inState(PublicKey const& validator, PeerState state) const
    {
        auto const& it = slots_.find(validator);
        if (it != slots_.end())
            return it->second.inState(state);
        return {};
    }

    /** Count peers NOT in `state` for the given validator's slot.
     *
     *  @param validator Validator public key.
     *  @param state     Peer state to exclude.
     *  @return Count, or `std::nullopt` if no slot exists for `validator`.
     */
    [[nodiscard]] std::optional<std::uint16_t>
    notInState(PublicKey const& validator, PeerState state) const
    {
        auto const& it = slots_.find(validator);
        if (it != slots_.end())
            return it->second.notInState(state);
        return {};
    }

    /** Return `true` if the given validator's slot is in `state`.
     *
     *  Returns `false` if no slot exists for `validator`.
     *
     *  @param validator Validator public key.
     *  @param state     Slot state to test.
     */
    [[nodiscard]] bool
    inState(PublicKey const& validator, SlotState state) const
    {
        auto const& it = slots_.find(validator);
        if (it != slots_.end())
            return it->second.state_ == state;
        return false;
    }

    /** IDs of peers currently selected as relay sources for `validator`.
     *
     *  @param validator Validator public key.
     *  @return Sorted set of selected peer IDs; empty if no slot exists or
     *      the slot is in `SlotState::Counting`.
     */
    std::set<id_t>
    getSelected(PublicKey const& validator)
    {
        auto const& it = slots_.find(validator);
        if (it != slots_.end())
            return it->second.getSelected();
        return {};
    }

    /** Snapshot of peer tracking data for `validator`'s slot.
     *
     *  Primarily used for testing and diagnostics.
     *
     *  @param validator Validator public key.
     *  @return Map from peer ID to
     *      `(PeerState, message_count, squelch_expiry_ms, last_message_ms)`,
     *      or an empty map if no slot exists.
     */
    std::
        unordered_map<typename Peer::id_t, std::tuple<PeerState, uint16_t, uint32_t, std::uint32_t>>
        getPeers(PublicKey const& validator)
    {
        auto const& it = slots_.find(validator);
        if (it != slots_.end())
            return it->second.getPeers();
        return {};
    }

    /** Current state of the slot for `validator`.
     *
     *  @param validator Validator public key.
     *  @return Slot state, or `std::nullopt` if no slot exists.
     */
    std::optional<SlotState>
    getState(PublicKey const& validator)
    {
        auto const& it = slots_.find(validator);
        if (it != slots_.end())
            return it->second.getState();
        return {};
    }

    /** Notify all slots that a peer has disconnected.
     *
     *  Iterates every validator slot and calls `Slot::deletePeer()`.  If the
     *  peer was a selected relay source in any slot, the affected squelched
     *  peers are unsquelched and that slot reverts to `SlotState::Counting`.
     *
     *  @param id    ID of the disconnected peer.
     *  @param erase If `true`, remove the peer's `PeerInfo` entry entirely.
     */
    void
    deletePeer(id_t id, bool erase);

private:
    /** Record a (message, peer) pair and return whether it is novel.
     *
     *  Entries age out after `kIDLED` seconds via `beast::expire()`.  A zero
     *  hash (`key.isZero()`) is always treated as novel (no deduplication).
     *
     *  @param key  Hash of the incoming message.
     *  @param id   ID of the peer that forwarded it.
     *  @return `true` if the pair is new and the slot should be updated;
     *      `false` if this (message, peer) combination was already seen
     *      within the current idle window.
     */
    bool
    addPeerMessage(uint256 const& key, id_t id);

    /** Set to `true` once `kWAIT_ON_BOOTUP` has passed; never reverts. */
    std::atomic_bool reduceRelayReady_{false};

    /** Per-validator slot instances. */
    hash_map<PublicKey, Slot<ClockType>> slots_;

    SquelchHandler const& handler_;  /**< Squelch/unsquelch callback. */
    Logs& logs_;
    beast::Journal const journal_;

    bool const baseSquelchEnabled_;  /**< Whether base squelching is enabled in config. */
    uint16_t const maxSelectedPeers_;  /**< Per-slot relay-source cap from config. */

    /** Deduplication map: message hash → set of peer IDs that have forwarded it.
     *
     *  Entries expire after `kIDLED` seconds.  Declared `inline static` so
     *  that all `Slots<ClockType>` instances share a single map — necessary
     *  because the production code constructs only one instance but tests may
     *  construct several.
     */
    inline static messages peersWithMessage{beast::getAbstractClock<ClockType>()};
};

template <typename ClockType>
bool
Slots<ClockType>::addPeerMessage(uint256 const& key, id_t id)
{
    beast::expire(peersWithMessage, reduce_relay::kIDLED);

    if (key.isNonZero())
    {
        auto it = peersWithMessage.find(key);
        if (it == peersWithMessage.end())
        {
            JLOG(journal_.trace()) << "addPeerMessage: new " << to_string(key) << " " << id;
            peersWithMessage.emplace(key, std::unordered_set<id_t>{id});
            return true;
        }

        if (it->second.find(id) != it->second.end())
        {
            JLOG(journal_.trace())
                << "addPeerMessage: duplicate message " << to_string(key) << " " << id;
            return false;
        }

        JLOG(journal_.trace()) << "addPeerMessage: added " << to_string(key) << " " << id;

        it->second.insert(id);
    }

    return true;
}

template <typename ClockType>
void
Slots<ClockType>::updateSlotAndSquelch(
    uint256 const& key,
    PublicKey const& validator,
    id_t id,
    protocol::MessageType type,
    typename Slot<ClockType>::ignored_squelch_callback callback)
{
    if (!addPeerMessage(key, id))
        return;

    auto it = slots_.find(validator);
    if (it == slots_.end())
    {
        JLOG(journal_.trace()) << "updateSlotAndSquelch: new slot " << Slice(validator);
        auto it = slots_
                      .emplace(
                          std::make_pair(
                              validator,
                              Slot<ClockType>(handler_, logs_.journal("Slot"), maxSelectedPeers_)))
                      .first;
        it->second.update(validator, id, type, callback);
    }
    else
    {
        it->second.update(validator, id, type, callback);
    }
}

template <typename ClockType>
void
Slots<ClockType>::deletePeer(id_t id, bool erase)
{
    for (auto& [validator, slot] : slots_)
        slot.deletePeer(validator, id, erase);
}

template <typename ClockType>
void
Slots<ClockType>::deleteIdlePeers()
{
    auto now = ClockType::now();

    for (auto it = slots_.begin(); it != slots_.end();)
    {
        it->second.deleteIdlePeer(it->first);
        if (now - it->second.getLastSelected() > kMAX_UNSQUELCH_EXPIRE_DEFAULT)
        {
            JLOG(journal_.trace()) << "deleteIdlePeers: deleting idle slot " << Slice(it->first);
            it = slots_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

}  // namespace xrpl::reduce_relay
