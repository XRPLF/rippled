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

//------------------------------------------------------------------------------
// These are protocol parameters used to control the behavior of the system and
// they should not be changed arbitrarily.

//! The percentage threshold above which we can declare consensus.
auto constexpr minimumConsensusPercentage = 80;

using namespace std::chrono_literals;
/**  Possible close time resolutions.

    Values should not be duplicated.
    @see getNextLedgerTimeResolution
*/
std::chrono::seconds constexpr ledgerPossibleTimeResolutions[] =
    {10s, 20s, 30s, 60s, 90s, 120s};

//! Initial resolution of ledger close time.
auto constexpr ledgerDefaultTimeResolution = ledgerPossibleTimeResolutions[2];

//! How often we increase the close time resolution (in numbers of ledgers)
auto constexpr increaseLedgerTimeResolutionEvery = 8;

//! How often we decrease the close time resolution (in numbers of ledgers)
auto constexpr decreaseLedgerTimeResolutionEvery = 1;

//! The number of seconds a ledger may remain idle before closing
auto constexpr LEDGER_IDLE_INTERVAL = 15s;

//! The number of seconds we wait minimum to ensure participation
auto constexpr LEDGER_MIN_CONSENSUS = 1950ms;

//! Minimum number of seconds to wait to ensure others have computed the LCL
auto constexpr LEDGER_MIN_CLOSE = 2s;

//! How often we check state or change positions
auto constexpr LEDGER_GRANULARITY = 1s;

//! How long we consider a proposal fresh
auto constexpr PROPOSE_FRESHNESS = 20s;

//! How often we force generating a new proposal to keep ours fresh
auto constexpr PROPOSE_INTERVAL = 12s;

//------------------------------------------------------------------------------
// Avalanche tuning
//! Percentage of nodes on our UNL that must vote yes
auto constexpr AV_INIT_CONSENSUS_PCT = 50;

//! Percentage of previous close time before we advance
auto constexpr AV_MID_CONSENSUS_TIME = 50;

//! Percentage of nodes that most vote yes after advancing
auto constexpr AV_MID_CONSENSUS_PCT = 65;

//! Percentage of previous close time before we advance
auto constexpr AV_LATE_CONSENSUS_TIME = 85;

//! Percentage of nodes that most vote yes after advancing
auto constexpr AV_LATE_CONSENSUS_PCT = 70;

//! Percentage of previous close time before we are stuck
auto constexpr AV_STUCK_CONSENSUS_TIME = 200;

//! Percentage of nodes that must vote yes after we are stuck
auto constexpr AV_STUCK_CONSENSUS_PCT = 95;

//! Percentage of nodes required to reach agreement on ledger close time
auto constexpr AV_CT_CONSENSUS_PCT = 75;

/** The minimum amount of time to consider the previous round
    to have taken.

    The minimum amount of time to consider the previous round
    to have taken. This ensures that there is an opportunity
    for a round at each avalanche threshold even if the
    previous consensus was very fast. This should be at least
    twice the interval between proposals (0.7s) divided by
    the interval between mid and late consensus ([85-50]/100).
*/
auto constexpr AV_MIN_CONSENSUS_TIME = 5s;

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
*/
template <class duration>
duration
getNextLedgerTimeResolution(
    duration previousResolution,
    bool previousAgree,
    std::uint32_t ledgerSeq)
{
    assert(ledgerSeq);

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
    if (!previousAgree && ledgerSeq % decreaseLedgerTimeResolutionEvery == 0)
    {
        if (++iter != std::end(ledgerPossibleTimeResolutions))
            return *iter;
    }

    // If we previously agreed, we try to increase the resolution to determine
    // if we can continue to agree.
    if (previousAgree && ledgerSeq % increaseLedgerTimeResolutionEvery == 0)
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
template <class time_point>
time_point
roundCloseTime(
    time_point closeTime,
    typename time_point::duration closeResolution)
{
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
template <class time_point>
time_point
effCloseTime(
    time_point closeTime,
    typename time_point::duration const resolution,
    time_point priorCloseTime)
{
    if (closeTime == time_point{})
        return closeTime;

    return std::max<time_point>(
        roundCloseTime(closeTime, resolution), (priorCloseTime + 1s));
}

/** Determines whether the current ledger should close at this time.

    This function should be called when a ledger is open and there is no close
    in progress, or when a transaction is received and no close is in progress.

    @param anyTransactions indicates whether any transactions have been received
    @param prevProposers proposers in the last closing
    @param proposersClosed proposers who have currently closed this ledger
    @param proposersValidated proposers who have validated the last closed
                              ledger
    @param prevRoundTime time for the previous ledger to reach consensus
    @param timeSincePrevClose  time since the previous ledger's (possibly rounded)
                        close time
    @param openTime     duration this ledger has been open
    @param idleInterval the network's desired idle interval
    @param j            journal for logging
*/
bool
shouldCloseLedger(
    bool anyTransactions,
    std::size_t prevProposers,
    std::size_t proposersClosed,
    std::size_t proposersValidated,
    std::chrono::milliseconds prevRoundTime,
    std::chrono::milliseconds timeSincePrevClose,
    std::chrono::milliseconds openTime,
    std::chrono::seconds idleInterval,
    beast::Journal j);

/** Determine if a consensus has been reached

    This function determines if a consensus has been reached

    @param agreeing count of agreements with our position
    @param total count of participants other than us
    @param count_self whether we count ourselves
    @return True if a consensus has been reached
*/
bool
checkConsensusReached(std::size_t agreeing, std::size_t total, bool count_self);

/** Whether we have or don't have a consensus */
enum class ConsensusState {
    No,       //!< We do not have consensus
    MovedOn,  //!< The network has consensus without us
    Yes       //!< We have consensus along with the network
};

/** Determine whether the network reached consensus and whether we joined.

    @param prevProposers proposers in the last closing (not including us)
    @param currentProposers proposers in this closing so far (not including us)
    @param currentAgree proposers who agree with us
    @param currentFinished proposers who have validated a ledger after this one
    @param previousAgreeTime how long, in milliseconds, it took to agree on the
                             last ledger
    @param currentAgreeTime how long, in milliseconds, we've been trying to
                            agree
    @param proposing        whether we should count ourselves
    @param j                journal for logging
*/
ConsensusState
checkConsensus(
    std::size_t prevProposers,
    std::size_t currentProposers,
    std::size_t currentAgree,
    std::size_t currentFinished,
    std::chrono::milliseconds previousAgreeTime,
    std::chrono::milliseconds currentAgreeTime,
    bool proposing,
    beast::Journal j);

}  // ripple

#endif
