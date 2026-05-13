/** @file
 *  Per-peer validator relay suppression for the XRPL reduce-relay protocol.
 *
 *  This is the downstream enforcement half of the reduce-relay system.
 *  `Squelch<ClockType>` records instructions received via `TMSquelch` and
 *  gates outbound validator messages accordingly.  The upstream selection
 *  logic — which decides *which* peers to squelch — lives in `Slot.h`.
 *
 *  @see Slot.h, ReduceRelayCommon.h
 *  @see https://xrpl.org/blog/2021/message-routing-optimizations-pt-1-proposal-validation-relaying.html
 */

#pragma once

#include <xrpld/overlay/ReduceRelayCommon.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/PublicKey.h>

#include <chrono>

namespace xrpl::reduce_relay {

/** Enforces per-validator relay suppression for a single peer connection.
 *
 *  Each `PeerImp` owns one `Squelch` instance.  When the upstream
 *  coordinator (`Slots`) selects a preferred relay set for a validator, it
 *  sends a `TMSquelch` protobuf message to every non-selected peer.
 *  `PeerImp::onMessage(TMSquelch)` calls `addSquelch` or `removeSquelch` to
 *  record the directive here.  Before forwarding any outbound validator
 *  message, `PeerImp::send` calls `expireSquelch`; a `false` return
 *  short-circuits the send and increments the `squelch_suppressed` traffic
 *  counter.
 *
 *  Expiry is lazy: squelch entries are only removed when `expireSquelch` is
 *  called after the deadline passes.  No background cleanup task is needed.
 *
 *  @tparam ClockType Clock used for computing squelch expiry deadlines.
 *      Production code passes `UptimeClock`; unit tests inject a controlled
 *      `ManualClock` to advance time deterministically.
 *
 *  @note This class is not thread-safe on its own.  Callers must ensure
 *      all method calls run on the owning `PeerImp`'s strand.
 */
template <typename ClockType>
class Squelch
{
    using time_point = typename ClockType::time_point;

public:
    /** Construct with a diagnostic journal.
     *
     *  @param journal Journal used to log invalid squelch duration errors.
     */
    explicit Squelch(beast::Journal journal) : journal_(journal)
    {
    }
    virtual ~Squelch() = default;

    /** Record a squelch directive for a validator.
     *
     *  Stores an expiry deadline of `ClockType::now() + squelchDuration` for
     *  the given validator key.  If `squelchDuration` falls outside
     *  `[kMIN_UNSQUELCH_EXPIRE, kMAX_UNSQUELCH_EXPIRE_PEERS]`, the duration
     *  is rejected: an error is logged, any existing entry for the key is
     *  proactively cleared via `removeSquelch`, and `false` is returned.
     *  The caller (`PeerImp::onMessage(TMSquelch)`) then charges the sending
     *  peer a `feeInvalidData` resource fee.
     *
     *  @param validator The validator's public key.
     *  @param squelchDuration Requested suppression window.  Must be in
     *      `[kMIN_UNSQUELCH_EXPIRE (300 s), kMAX_UNSQUELCH_EXPIRE_PEERS (3600 s)]`.
     *  @return `true` if the squelch was recorded; `false` if the duration
     *      was out of range (any prior entry for this key is also cleared).
     */
    bool
    addSquelch(PublicKey const& validator, std::chrono::seconds const& squelchDuration);

    /** Clear a squelch entry for a validator.
     *
     *  Called when `PeerImp::onMessage(TMSquelch)` receives a message with
     *  `squelch == false` — the unsquelch signal sent by the upstream
     *  coordinator when a previously selected peer disconnects or idles,
     *  freeing the suppressed peers to resume relaying.
     *
     *  @param validator The validator's public key.
     */
    void
    removeSquelch(PublicKey const& validator);

    /** Check whether a validator message may be forwarded.
     *
     *  Returns `true` (allow forward) when no active squelch exists for the
     *  key, or when an existing entry's deadline has passed — in which case
     *  the stale entry is lazily removed from the map.  Returns `false`
     *  (suppress) when the squelch is still active.
     *
     *  Called by `PeerImp::send()` on every outbound message that carries a
     *  validator public key.  There is no background timer; cleanup happens
     *  naturally the first time a post-expiry message is about to be sent.
     *
     *  @param validator The validator's public key.
     *  @return `true` if the message should be forwarded; `false` if it
     *      should be dropped because the squelch window is still active.
     */
    bool
    expireSquelch(PublicKey const& validator);

private:
    /** Maps each squelched validator key to its suppression deadline.
     *
     *  Entries are added by `addSquelch` and removed either explicitly by
     *  `removeSquelch` or lazily by `expireSquelch` once the deadline passes.
     */
    hash_map<PublicKey, time_point> squelched_;
    beast::Journal const journal_;
};

template <typename ClockType>
bool
Squelch<ClockType>::addSquelch(
    PublicKey const& validator,
    std::chrono::seconds const& squelchDuration)
{
    if (squelchDuration >= kMIN_UNSQUELCH_EXPIRE && squelchDuration <= kMAX_UNSQUELCH_EXPIRE_PEERS)
    {
        squelched_[validator] = ClockType::now() + squelchDuration;
        return true;
    }

    JLOG(journal_.error()) << "squelch: invalid squelch duration " << squelchDuration.count();

    removeSquelch(validator);

    return false;
}

template <typename ClockType>
void
Squelch<ClockType>::removeSquelch(PublicKey const& validator)
{
    squelched_.erase(validator);
}

template <typename ClockType>
bool
Squelch<ClockType>::expireSquelch(PublicKey const& validator)
{
    auto now = ClockType::now();

    auto const& it = squelched_.find(validator);
    if (it == squelched_.end())
        return true;
    if (it->second > now)
        return false;

    squelched_.erase(it);

    return true;
}

}  // namespace xrpl::reduce_relay
