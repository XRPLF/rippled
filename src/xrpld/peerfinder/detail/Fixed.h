#pragma once

#include <xrpld/peerfinder/detail/Tuning.h>

namespace xrpl::PeerFinder {

/** Reconnection backoff state for a statically-configured (fixed) peer slot.
 *
 *  Tracks when the next connection attempt is permitted for a single fixed
 *  peer endpoint. On failure, `when_` is advanced along a Fibonacci backoff
 *  sequence (`Tuning::kCONNECTION_BACKOFF`: 1,1,2,3,5,8,13,21,34,55 minutes)
 *  so unreachable peers are retried progressively less frequently, capping at
 *  55 minutes. On success, the peer is immediately re-eligible.
 *
 *  Instances live as values in `Logic::fixed_` (`std::map<Endpoint, Fixed>`)
 *  and are accessed only while `Logic`'s internal mutex is held — no
 *  per-object synchronization is needed.
 *
 *  @see Logic::getFixed
 */
class Fixed
{
public:
    /** Construct a `Fixed` record that is immediately eligible for connection.
     *
     *  Sets the earliest permitted attempt time to `clock.now()`, so the
     *  peer qualifies for its first connection attempt without any delay.
     *
     *  @param clock The PeerFinder clock used to obtain the current time.
     */
    explicit Fixed(clock_type& clock) : when_(clock.now())
    {
    }

    Fixed(Fixed const&) = default;

    /** Return the earliest time at which a new connection attempt is allowed.
     *
     *  `Logic::getFixed` compares this value against the current time to
     *  decide whether the peer is on cooldown. A time point at or before
     *  `now` means the peer is eligible.
     *
     *  @return The `clock_type::time_point` after which a connection attempt
     *      may be made.
     */
    [[nodiscard]] clock_type::time_point const&
    when() const
    {
        return when_;
    }

    /** Advance the backoff state after a failed connection attempt.
     *
     *  Increments the consecutive-failure counter (capped at the last index
     *  of `Tuning::kCONNECTION_BACKOFF` to prevent out-of-bounds access),
     *  then sets `when_` to `now + kCONNECTION_BACKOFF[failures_]` minutes.
     *  The Fibonacci-shaped sequence (1,1,2,3,5,8,13,21,34,55) grows quickly
     *  enough to avoid hammering an unreachable peer, yet caps at 55 minutes
     *  so a fixed peer is never abandoned entirely.
     *
     *  @param now The current clock time, used to compute the next eligible
     *      attempt time.
     */
    void
    failure(clock_type::time_point const& now)
    {
        failures_ = std::min(failures_ + 1, Tuning::kCONNECTION_BACKOFF.size() - 1);
        when_ = now + std::chrono::minutes(Tuning::kCONNECTION_BACKOFF[failures_]);
    }

    /** Reset the backoff state after a successful connection.
     *
     *  Clears the consecutive-failure counter and sets `when_` to `now`,
     *  making the peer immediately eligible for a new attempt should the
     *  connection drop and need to be re-established.
     *
     *  @param now The current clock time, assigned directly as the new
     *      earliest eligible attempt time.
     */
    void
    success(clock_type::time_point const& now)
    {
        failures_ = 0;
        when_ = now;
    }

private:
    /** Earliest time at which the next connection attempt is permitted. */
    clock_type::time_point when_;

    /** Number of consecutive failures since the last successful connection.
     *
     *  Used as an index into `Tuning::kCONNECTION_BACKOFF`; clamped to
     *  `kCONNECTION_BACKOFF.size() - 1` so it is always a valid index.
     */
    std::size_t failures_{0};
};

}  // namespace xrpl::PeerFinder
