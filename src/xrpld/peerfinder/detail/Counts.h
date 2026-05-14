#pragma once

#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/Slot.h>
#include <xrpld/peerfinder/detail/Tuning.h>

#include <xrpl/basics/random.h>

namespace xrpl::PeerFinder {

/** Direction of a slot count adjustment passed to `Counts::adjust`. */
enum class CountAdjustment : int {
    Decrement = -1, /**< Remove a slot from tracked counts. */
    Increment = 1   /**< Add a slot to tracked counts. */
};

/** Tracks the occupancy of every connection-slot category managed by PeerFinder.
 *
 *  `Counts` is the bookkeeping core that answers the resource-management
 *  questions `Logic` needs: Are inbound slots available? Should more outbound
 *  attempts be launched? Can this handshaked slot be promoted to active?
 *
 *  All mutations funnel through the private `adjust()` method, which is called
 *  by `Logic` under its own `std::recursive_mutex`. `Counts` itself carries no
 *  synchronization — it is a value-semantics helper embedded in a larger
 *  guarded object.
 *
 *  Fixed peers (from node config) and reserved peers (cluster/reservation
 *  table) are tracked in separate counters and do NOT consume ordinary
 *  inbound/outbound slot capacity; `canActivate()` admits them unconditionally.
 *
 *  @see Logic
 */
class Counts
{
public:
    /** Register a slot's state and properties in the counts.
     *
     *  Thin wrapper around `adjust(s, Increment)`. Call this whenever a slot
     *  is created or transitions to a new state (after updating the slot).
     *
     *  @param s The slot whose current state and properties should be counted.
     */
    void
    add(Slot const& s)
    {
        adjust(s, CountAdjustment::Increment);
    }

    /** Deregister a slot's state and properties from the counts.
     *
     *  Thin wrapper around `adjust(s, Decrement)`. Call this before a slot
     *  transitions to a new state or is destroyed.
     *
     *  @param s The slot whose current state and properties should be removed.
     */
    void
    remove(Slot const& s)
    {
        adjust(s, CountAdjustment::Decrement);
    }

    /** Determine whether a handshaked slot may be promoted to active.
     *
     *  Fixed and reserved slots bypass capacity limits and are always admitted.
     *  For ordinary slots, inbound connections require `in_active_ < in_max_`
     *  and outbound connections require `out_active_ < out_max_`.
     *
     *  @param s The slot to test; must be in `Connected` or `Accept` state.
     *  @return `true` if the slot may become active, `false` if all slots of
     *      its direction are occupied.
     *  @note Fixed and reserved connections are never blocked by slot limits,
     *      ensuring administratively configured peers can always connect.
     */
    [[nodiscard]] bool
    canActivate(Slot const& s) const
    {
        XRPL_ASSERT(
            s.state() == Slot::State::Connected || s.state() == Slot::State::Accept,
            "xrpl::PeerFinder::Counts::can_activate : valid input state");

        if (s.fixed() || s.reserved())
            return true;

        if (s.inbound())
            return in_active_ < in_max_;

        return out_active_ < out_max_;
    }

    /** Compute how many additional outbound attempts should be launched.
     *
     *  Compares the current in-flight attempt count against
     *  `Tuning::kMAX_CONNECT_ATTEMPTS` (20) to cap simultaneous outbound
     *  connection storms regardless of how many free slots remain.
     *
     *  @return Number of additional attempts that may be started; 0 when the
     *      in-flight count has already reached the cap.
     */
    [[nodiscard]] std::size_t
    attemptsNeeded() const
    {
        if (attempts_ >= Tuning::kMAX_CONNECT_ATTEMPTS)
            return 0;
        return Tuning::kMAX_CONNECT_ATTEMPTS - attempts_;
    }

    /** Return the current number of in-flight outbound connection attempts.
     *
     *  Counts slots in `Connect` or `Connected` state (both are outbound
     *  attempts that have not yet been admitted as active).
     *
     *  @return Number of in-flight outbound attempts.
     */
    [[nodiscard]] std::size_t
    attempts() const
    {
        return attempts_;
    }

    /** Return the configured maximum number of outbound slots.
     *
     *  Set by `onConfig()` from `Config::outPeers`. Fixed-peer connections
     *  do not consume this quota.
     *
     *  @return Maximum desired outbound peer count.
     */
    [[nodiscard]] int
    outMax() const
    {
        return out_max_;
    }

    /** Return the number of active ordinary outbound peers.
     *
     *  Fixed and reserved peers are excluded; they have their own counters
     *  and do not consume outbound slot capacity.
     *
     *  @return Count of active non-fixed, non-reserved outbound peers.
     */
    [[nodiscard]] int
    outActive() const
    {
        return out_active_;
    }

    /** Return the total number of fixed-peer connections (any state).
     *
     *  @return Count of fixed slots across all states.
     */
    [[nodiscard]] std::size_t
    fixed() const
    {
        return fixed_;
    }

    /** Return the number of fixed-peer connections that are fully active.
     *
     *  @return Count of fixed slots in `Active` state.
     */
    [[nodiscard]] std::size_t
    fixedActive() const
    {
        return fixed_active_;
    }

    //--------------------------------------------------------------------------

    /** Apply configuration limits for inbound and outbound slot capacities.
     *
     *  Sets `out_max_` unconditionally from `config.outPeers`. Sets `in_max_`
     *  from `config.inPeers` only when `config.wantIncoming` is true; if the
     *  node does not want inbound connections, `in_max_` remains 0, which
     *  causes `canActivate()` to reject all inbound slots.
     *
     *  @param config The active PeerFinder configuration.
     */
    void
    onConfig(Config const& config)
    {
        out_max_ = config.outPeers;
        if (config.wantIncoming)
            in_max_ = config.inPeers;
    }

    /** Return the number of inbound connections that have not yet handshaked.
     *
     *  These are slots in `Accept` state — the TCP connection is established
     *  but the protocol handshake is still in progress.
     *
     *  @return Count of pre-handshake inbound slots.
     */
    [[nodiscard]] int
    acceptCount() const
    {
        return acceptCount_;
    }

    /** Return the number of outbound connection attempts currently in progress.
     *
     *  Alias for `attempts()` using the naming convention expected by
     *  `onWrite()` and `stateString()`.
     *
     *  @return Count of slots in `Connect` or `Connected` state.
     */
    [[nodiscard]] int
    connectCount() const
    {
        return attempts_;
    }

    /** Return the number of connections currently undergoing graceful teardown.
     *
     *  @return Count of slots in `Closing` state.
     */
    [[nodiscard]] int
    closingCount() const
    {
        return closingCount_;
    }

    /** Return the configured maximum number of inbound slots.
     *
     *  Zero when `Config::wantIncoming` was false at `onConfig()` time,
     *  causing all inbound activation attempts to be rejected.
     *
     *  @return Maximum allowed inbound active peer count.
     */
    [[nodiscard]] int
    inMax() const
    {
        return in_max_;
    }

    /** Return the number of active ordinary inbound peers.
     *
     *  Fixed and reserved peers are excluded and do not consume inbound
     *  slot capacity.
     *
     *  @return Count of active non-fixed, non-reserved inbound peers.
     */
    [[nodiscard]] int
    inboundActive() const
    {
        return in_active_;
    }

    /** Return the combined active ordinary peer count (inbound + outbound).
     *
     *  Fixed and reserved peers are excluded from both terms.
     *
     *  @return `in_active_ + out_active_`.
     */
    [[nodiscard]] int
    totalActive() const
    {
        return in_active_ + out_active_;
    }

    /** Return the number of available inbound slots.
     *
     *  Fixed and reserved peers do not consume inbound capacity, so they do
     *  not reduce this value.
     *
     *  @return `in_max_ - in_active_`, or 0 if the inbound limit is reached.
     */
    [[nodiscard]] int
    inboundSlotsFree() const
    {
        if (in_active_ < in_max_)
            return in_max_ - in_active_;
        return 0;
    }

    /** Return the number of available outbound slots.
     *
     *  Fixed and reserved peers do not consume outbound capacity, so they do
     *  not reduce this value.
     *
     *  @return `out_max_ - out_active_`, or 0 if the outbound limit is reached.
     */
    [[nodiscard]] int
    outboundSlotsFree() const
    {
        if (out_active_ < out_max_)
            return out_max_ - out_active_;
        return 0;
    }

    //--------------------------------------------------------------------------

    /** Determine whether this node considers itself connected to the network.
     *
     *  Returns `true` only when `out_max_ <= 0`, which means the node is
     *  configured with zero desired outbound connections (pure-listener mode)
     *  and therefore considers itself connected without needing any outbound
     *  peers. In the common case where `out_max_ > 0` this always returns
     *  `false`; `Logic` uses `out_active_` vs `out_max_` directly to drive
     *  connection attempts.
     *
     *  @note Fixed peers are not counted toward `out_active_` and do not
     *      influence this result.
     *
     *  @return `true` if the node operates as a pure listener (outPeers == 0).
     */
    [[nodiscard]] bool
    isConnectedToNetwork() const
    {
        return out_max_ <= 0;
    }

    /** Serialize current slot counts into a property-stream map for monitoring.
     *
     *  Emits: `accept`, `connect`, `close`, `in` (active/max), `out`
     *  (active/max), `fixed` (active fixed peers), `reserved`, and `total`
     *  (all active peers including fixed and reserved).
     *
     *  @param map The property-stream map to write into.
     */
    void
    onWrite(beast::PropertyStream::Map& map) const
    {
        map["accept"] = acceptCount();
        map["connect"] = connectCount();
        map["close"] = closingCount();
        map["in"] << in_active_ << "/" << in_max_;
        map["out"] << out_active_ << "/" << out_max_;
        map["fixed"] = fixed_active_;
        map["reserved"] = reserved_;
        map["total"] = active_;
    }

    /** Return a compact human-readable summary of current slot state.
     *
     *  Produces a string of the form
     *  `"3/8 out, 10/21 in, 2 connecting, 0 closing"` used in log
     *  messages throughout `Logic`.
     *
     *  @return Diagnostic string; no side effects.
     */
    [[nodiscard]] std::string
    stateString() const
    {
        std::stringstream ss;
        ss << out_active_ << "/" << out_max_ << " out, " << in_active_ << "/" << in_max_ << " in, "
           << connectCount() << " connecting, " << closingCount() << " closing";
        return ss.str();
    }

    //--------------------------------------------------------------------------
private:
    /** Increment or decrement a counter by exactly one step.
     *
     *  All `std::size_t` counters MUST be updated through this helper rather
     *  than via `+= static_cast<int>(dir)`. Adding `-1` to a `std::size_t`
     *  implicitly converts to `SIZE_MAX` (unsigned-integer overflow), which
     *  masks underflow bugs and is flagged by UBSan. Plain `int` counters
     *  (`acceptCount_`, `attempts_`, `closingCount_`) are safe with `+= n`
     *  and bypass this helper.
     *
     *  @tparam T Counter type (integral).
     *  @param counter Reference to the counter to adjust.
     *  @param dir Whether to increment or decrement.
     */
    template <typename T>
    static void
    adjustCounter(T& counter, CountAdjustment dir)
    {
        switch (dir)
        {
            case CountAdjustment::Increment:
                ++counter;
                break;
            case CountAdjustment::Decrement:
                --counter;
                break;
        }
    }

    /** Update all counters that track the given slot's state and properties.
     *
     *  Single entry-point for all count mutations: `add()` and `remove()` are
     *  thin wrappers that call this with `Increment` or `Decrement`. Keeping
     *  all counter logic here ensures add/remove can never diverge.
     *
     *  State mapping:
     *  - `Accept`    → `acceptCount_` (asserts inbound).
     *  - `Connect` / `Connected` → `attempts_` (asserts outbound).
     *  - `Active`    → `active_`; additionally `fixed_active_` for fixed slots,
     *                  or `in_active_`/`out_active_` for ordinary slots.
     *  - `Closing`   → `closingCount_`.
     *
     *  Fixed and reserved slots are always tracked in `fixed_`/`reserved_`
     *  regardless of state.
     *
     *  @param s   The slot whose current state drives the counter selection.
     *  @param dir `Increment` when adding a slot; `Decrement` when removing.
     */
    void
    adjust(Slot const& s, CountAdjustment const dir)
    {
        int const n = static_cast<int>(dir);
        if (s.fixed())
            adjustCounter(fixed_, dir);

        if (s.reserved())
            adjustCounter(reserved_, dir);

        switch (s.state())
        {
            case Slot::State::Accept:
                XRPL_ASSERT(s.inbound(), "xrpl::PeerFinder::Counts::adjust : input is inbound");
                acceptCount_ += n;
                break;

            case Slot::State::Connect:
            case Slot::State::Connected:
                XRPL_ASSERT(
                    !s.inbound(),
                    "xrpl::PeerFinder::Counts::adjust : input is not "
                    "inbound");
                attempts_ += n;
                break;

            case Slot::State::Active:
                if (s.fixed())
                    adjustCounter(fixed_active_, dir);
                if (!s.fixed() && !s.reserved())
                {
                    if (s.inbound())
                    {
                        adjustCounter(in_active_, dir);
                    }
                    else
                    {
                        adjustCounter(out_active_, dir);
                    }
                }
                adjustCounter(active_, dir);
                break;

            case Slot::State::Closing:
                closingCount_ += n;
                break;

            // LCOV_EXCL_START
            default:
                UNREACHABLE("xrpl::PeerFinder::Counts::adjust : invalid input state");
                break;
                // LCOV_EXCL_STOP
        };
    }

private:
    /** Outbound connection attempts. */
    int attempts_{0};

    /** Active connections, including fixed and reserved. */
    std::size_t active_{0};

    /** Total number of inbound slots. */
    std::size_t in_max_{0};

    /** Number of inbound slots assigned to active peers. */
    std::size_t in_active_{0};

    /** Maximum desired outbound slots. */
    std::size_t out_max_{0};

    /** Active outbound slots. */
    std::size_t out_active_{0};

    /** Fixed connections. */
    std::size_t fixed_{0};

    /** Active fixed connections. */
    std::size_t fixed_active_{0};

    /** Reserved connections. */
    std::size_t reserved_{0};

    /** Inbound connections in `Accept` state (TCP established, pre-handshake). */
    int acceptCount_{0};

    /** Connections in `Closing` state (graceful teardown in progress). */
    int closingCount_{0};
};

}  // namespace xrpl::PeerFinder
