/** @file
 *  Ledger close-time resolution binning and monotonicity enforcement.
 *
 *  Provides compile-time constants and three header-only template functions
 *  that translate raw wall-clock observations into canonical, network-agreed
 *  close timestamps written into every immutable ledger record. The binning
 *  approach lets validators with imperfectly synchronized clocks converge on
 *  a single close time without requiring a global time source.
 *
 *  @see getNextLedgerTimeResolution, roundCloseTime, effCloseTime
 */

#pragma once

#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>

#include <chrono>

namespace xrpl {

/** Ordered ladder of candidate close-time bin sizes, in seconds.
 *
 *  The six values — 10, 20, 30, 60, 90, 120 seconds — form a strictly
 *  increasing sequence. `getNextLedgerTimeResolution` traverses this array
 *  to coarsen (move toward index 5) on disagreement and to refine (move
 *  toward index 0) on agreement. The array order directly encodes the
 *  coarser/finer direction; no separate mapping is needed.
 *
 *  Values must be unique and sorted in ascending order.
 *
 *  @see getNextLedgerTimeResolution
 */
std::chrono::seconds constexpr kLEDGER_POSSIBLE_TIME_RESOLUTIONS[] = {
    std::chrono::seconds{10},
    std::chrono::seconds{20},
    std::chrono::seconds{30},
    std::chrono::seconds{60},
    std::chrono::seconds{90},
    std::chrono::seconds{120}};

/** Default close-time resolution used for all ordinary (non-genesis) ledgers.
 *
 *  Equal to `kLEDGER_POSSIBLE_TIME_RESOLUTIONS[2]` (30 seconds). Every
 *  consensus round starts from this resolution and adjusts based on prior
 *  agreement history via `getNextLedgerTimeResolution`.
 */
auto constexpr kLEDGER_DEFAULT_TIME_RESOLUTION = kLEDGER_POSSIBLE_TIME_RESOLUTIONS[2];

/** Close-time resolution used exclusively for the genesis ledger.
 *
 *  Equal to `kLEDGER_POSSIBLE_TIME_RESOLUTIONS[0]` (10 seconds), the finest
 *  available bin. There is no prior-ledger disagreement history at genesis,
 *  so the finest resolution is chosen as the starting point.
 */
auto constexpr kLEDGER_GENESIS_TIME_RESOLUTION = kLEDGER_POSSIBLE_TIME_RESOLUTIONS[0];

/** Number of ledgers between successive close-time resolution refinements.
 *
 *  When the prior ledger reached close-time consensus, the resolution moves
 *  one step finer only every 8th ledger. This conservative cadence avoids
 *  prematurely tightening the bin size after a brief period of agreement,
 *  which could immediately reintroduce disagreements on slightly skewed clocks.
 *
 *  @see getNextLedgerTimeResolution, kDECREASE_LEDGER_TIME_RESOLUTION_EVERY
 */
auto constexpr kINCREASE_LEDGER_TIME_RESOLUTION_EVERY = 8;

/** Number of ledgers between successive close-time resolution coarsenings.
 *
 *  When the prior ledger failed to reach close-time consensus, the resolution
 *  moves one step coarser on every ledger (value = 1). This aggressive
 *  back-off quickly finds a bin size that absorbs the validators' clock skew,
 *  deliberately asymmetric with the slower refinement cadence.
 *
 *  @see getNextLedgerTimeResolution, kINCREASE_LEDGER_TIME_RESOLUTION_EVERY
 */
auto constexpr kDECREASE_LEDGER_TIME_RESOLUTION_EVERY = 1;

/** Compute the close-time resolution to use for the next ledger.
 *
 *  Implements the adaptive binning policy: if the prior ledger failed to
 *  reach close-time consensus the bin size is coarsened (every ledger,
 *  per `kDECREASE_LEDGER_TIME_RESOLUTION_EVERY`); if it succeeded the bin
 *  size is refined (every 8th ledger, per
 *  `kINCREASE_LEDGER_TIME_RESOLUTION_EVERY`). Both adjustments saturate at
 *  the boundaries of `kLEDGER_POSSIBLE_TIME_RESOLUTIONS` rather than
 *  wrapping. The two rules are mutually exclusive — only one fires per call.
 *
 *  Called by the consensus engine at the start of every round to set
 *  `closeResolution_`, which is then used for the full round's close-time
 *  voting and embedded in the accepted ledger.
 *
 *  @param previousResolution The close-time resolution used for the prior
 *      ledger; must be one of the values in
 *      `kLEDGER_POSSIBLE_TIME_RESOLUTIONS`.
 *  @param previousAgree Whether the network agreed on the prior ledger's
 *      close time (true = finer bins are safe to try).
 *  @param ledgerSeq Sequence number of the ledger being built; must be
 *      non-zero. Used for the modulo-based rate-limiting of each direction.
 *  @return The resolution to apply for the new ledger, chosen from
 *      `kLEDGER_POSSIBLE_TIME_RESOLUTIONS`.
 *
 *  @pre `previousResolution` is an element of `kLEDGER_POSSIBLE_TIME_RESOLUTIONS`.
 *  @pre `ledgerSeq != Seq{0}`.
 *
 *  @tparam Rep   Tick-count type of the `std::chrono::duration`.
 *  @tparam Period `std::ratio` tick period of the `std::chrono::duration`.
 *  @tparam Seq   Unsigned integer-like type for the ledger sequence number;
 *      supports `operator%` and comparison with `Seq{0}`. Both built-in
 *      integers and XRPL `tagged_integer` wrappers are accepted.
 */
template <class Rep, class Period, class Seq>
std::chrono::duration<Rep, Period>
getNextLedgerTimeResolution(
    std::chrono::duration<Rep, Period> previousResolution,
    bool previousAgree,
    Seq ledgerSeq)
{
    XRPL_ASSERT(ledgerSeq != Seq{0}, "xrpl::getNextLedgerTimeResolution : valid ledger sequence");

    using namespace std::chrono;
    auto iter = std::find(
        std::begin(kLEDGER_POSSIBLE_TIME_RESOLUTIONS),
        std::end(kLEDGER_POSSIBLE_TIME_RESOLUTIONS),
        previousResolution);
    XRPL_ASSERT(
        iter != std::end(kLEDGER_POSSIBLE_TIME_RESOLUTIONS),
        "xrpl::getNextLedgerTimeResolution : found time resolution");

    // This should never happen, but just as a precaution
    if (iter == std::end(kLEDGER_POSSIBLE_TIME_RESOLUTIONS))
        return previousResolution;

    if (!previousAgree && (ledgerSeq % Seq{kDECREASE_LEDGER_TIME_RESOLUTION_EVERY} == Seq{0}))
    {
        if (++iter != std::end(kLEDGER_POSSIBLE_TIME_RESOLUTIONS))
            return *iter;
    }

    if (previousAgree && (ledgerSeq % Seq{kINCREASE_LEDGER_TIME_RESOLUTION_EVERY} == Seq{0}))
    {
        if (iter-- != std::begin(kLEDGER_POSSIBLE_TIME_RESOLUTIONS))
            return *iter;
    }

    return previousResolution;
}

/** Round a ledger close time to the nearest bin boundary.
 *
 *  Bins are aligned to multiples of `closeResolution` measured from the
 *  clock epoch (`time_since_epoch()`), so any two validators computing this
 *  on the same raw time will produce the same result regardless of local
 *  state — a correctness prerequisite for network agreement. Ties (a time
 *  exactly at the midpoint between two boundaries) round up to the later bin.
 *
 *  A default-constructed `time_point{}` (the epoch sentinel signalling no
 *  agreed close time) is returned unchanged without any rounding.
 *
 *  @param closeTime      The raw close-time observation to round.
 *  @param closeResolution The bin size; must be positive and non-zero.
 *  @return `closeTime` rounded to the nearest epoch-anchored multiple of
 *      `closeResolution`, or `closeTime` unmodified if it equals
 *      `time_point{}`.
 *
 *  @note Called by `effCloseTime` and also directly by the consensus engine
 *      via `asCloseTime()` to canonicalize individual peer proposals.
 */
template <class Clock, class Duration, class Rep, class Period>
std::chrono::time_point<Clock, Duration>
roundCloseTime(
    std::chrono::time_point<Clock, Duration> closeTime,
    std::chrono::duration<Rep, Period> closeResolution)
{
    using time_point = decltype(closeTime);
    if (closeTime == time_point{})
        return closeTime;

    closeTime += (closeResolution / 2);
    return closeTime - (closeTime.time_since_epoch() % closeResolution);
}

/** Compute the effective close time for a ledger, enforcing monotonicity.
 *
 *  Rounds `closeTime` via `roundCloseTime`, then clamps the result to be
 *  strictly greater than `priorCloseTime`. The clamp (`priorCloseTime + 1s`)
 *  handles the edge case where a very fast close would otherwise produce a
 *  rounded time equal to or earlier than the prior ledger's close time,
 *  violating the invariant that ledger timestamps increase strictly along the
 *  chain. When the rounded value is already later than `priorCloseTime`, it
 *  passes through unchanged.
 *
 *  A default-constructed `closeTime` (the epoch sentinel for "no agreed close
 *  time") is returned unchanged without rounding or clamping.
 *
 *  @param closeTime      The raw close-time observation for this ledger.
 *  @param resolution     The bin size for this round's close-time voting.
 *  @param priorCloseTime The effective close time of the preceding ledger;
 *      used as the strict lower bound.
 *  @return `max(roundCloseTime(closeTime, resolution), priorCloseTime + 1s)`,
 *      or `closeTime` unmodified if it equals `time_point{}`.
 *
 *  @note Example edge cases (30 s bins, priorCloseTime = 0 s):
 *      - `effCloseTime(10s, 30s, 0s)` → `1s`  (rounded = 0s, clamped to 1s)
 *      - `effCloseTime(16s, 30s, 0s)` → `30s` (rounded = 30s, passes through)
 */
template <class Clock, class Duration, class Rep, class Period>
std::chrono::time_point<Clock, Duration>
effCloseTime(
    std::chrono::time_point<Clock, Duration> closeTime,
    std::chrono::duration<Rep, Period> resolution,
    std::chrono::time_point<Clock, Duration> priorCloseTime)
{
    using namespace std::chrono_literals;
    using time_point = decltype(closeTime);

    if (closeTime == time_point{})
        return closeTime;

    return std::max<time_point>(roundCloseTime(closeTime, resolution), (priorCloseTime + 1s));
}

}  // namespace xrpl
