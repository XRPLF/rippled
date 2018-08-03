//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_APP_LEDGER_LEDGERTIMING_H_INCLUDED
#define RIPPLE_APP_LEDGER_LEDGERTIMING_H_INCLUDED

#include <ripple/basics/chrono.h>
#include <ripple/beast/utility/Journal.h>
#include <chrono>
#include <cstdint>

namespace ripple {

/**  Possible ledger close time resolutions.

    Values should not be duplicated.
    @see getNextLedgerTimeResolution
*/
std::chrono::seconds constexpr ledgerPossibleTimeResolutions[] =
    {
        std::chrono::seconds { 10},
        std::chrono::seconds { 20},
        std::chrono::seconds { 30},
        std::chrono::seconds { 60},
        std::chrono::seconds { 90},
        std::chrono::seconds {120}
    };

//! Initial resolution of ledger close time.
auto constexpr ledgerDefaultTimeResolution = ledgerPossibleTimeResolutions[2];

//! How often we increase the close time resolution (in numbers of ledgers)
auto constexpr increaseLedgerTimeResolutionEvery = 8;

//! How often we decrease the close time resolution (in numbers of ledgers)
auto constexpr decreaseLedgerTimeResolutionEvery = 1;

/** Calculates the close time resolution for the specified ledger.

    The Ripple protocol uses binning to represent time intervals using only one
    timestamp. This allows servers to derive a common time for the next ledger,
    without the need for perfectly synchronized clocks.
    The time resolution (i.e. the size of the intervals) is adjusted dynamically
    based on what happened in the last ledger, to try to avoid disagreements.

    @param previousResolution the resolution used for the prior ledger
    @param previousAgree whether consensus agreed on the close time of the prior
    ledger
    @param ledgerSeq the sequence number of the new ledger

    @pre previousResolution must be a valid bin
         from @ref ledgerPossibleTimeResolutions

    @tparam Rep Type representing number of ticks in std::chrono::duration
    @tparam Period An std::ratio representing tick period in
                   std::chrono::duration
    @tparam Seq Unsigned integer-like type corresponding to the ledger sequence
                number. It should be comparable to 0 and support modular
                division. Built-in and tagged_integers are supported.
*/
template <class Rep, class Period, class Seq>
std::chrono::duration<Rep, Period>
getNextLedgerTimeResolution(
    std::chrono::duration<Rep, Period> previousResolution,
    bool previousAgree,
    Seq ledgerSeq)
{
    assert(ledgerSeq != Seq{0});

    using namespace std::chrono;
    // Find the current resolution:
    auto iter = std::find(
        std::begin(ledgerPossibleTimeResolutions),
        std::end(ledgerPossibleTimeResolutions),
        previousResolution);
    assert(iter != std::end(ledgerPossibleTimeResolutions));

    // This should never happen, but just as a precaution
    if (iter == std::end(ledgerPossibleTimeResolutions))
        return previousResolution;

    // If we did not previously agree, we try to decrease the resolution to
    // improve the chance that we will agree now.
    if (!previousAgree &&
        (ledgerSeq % Seq{decreaseLedgerTimeResolutionEvery} == Seq{0}))
    {
        if (++iter != std::end(ledgerPossibleTimeResolutions))
            return *iter;
    }

    // If we previously agreed, we try to increase the resolution to determine
    // if we can continue to agree.
    if (previousAgree &&
        (ledgerSeq % Seq{increaseLedgerTimeResolutionEvery} == Seq{0}))
    {
        if (iter-- != std::begin(ledgerPossibleTimeResolutions))
            return *iter;
    }

    return previousResolution;
}

/** Calculates the close time for a ledger, given a close time resolution.

    @param closeTime The time to be rouned.
    @param closeResolution The resolution
    @return @b closeTime rounded to the nearest multiple of @b closeResolution.
    Rounds up if @b closeTime is midway between multiples of @b closeResolution.
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

/** Calculate the effective ledger close time

    After adjusting the ledger close time based on the current resolution, also
    ensure it is sufficiently separated from the prior close time.

    @param closeTime The raw ledger close time
    @param resolution The current close time resolution
    @param priorCloseTime The close time of the prior ledger
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

    return std::max<time_point>(
        roundCloseTime(closeTime, resolution), (priorCloseTime + 1s));
}

}
#endif
