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

#include <chrono>
#include <cstdint>
#include <ripple/basics/chrono.h>
#include <ripple/beast/utility/Journal.h>

namespace ripple {

/** Calculates the close time resolution for the specified ledger.

    The Ripple protocol uses binning to represent time intervals using only one
    timestamp. This allows servers to derive a common time for the next ledger,
    without the need for perfectly synchronized clocks.
    The time resolution (i.e. the size of the intervals) is adjusted dynamically
    based on what happened in the last ledger, to try to avoid disagreements.
*/
NetClock::duration
getNextLedgerTimeResolution (
    NetClock::duration previousResolution,
    bool previousAgree,
    std::uint32_t ledgerSeq);

/** Calculates the close time for a ledger, given a close time resolution.

    @param closeTime The time to be rouned.
    @param closeResolution The resolution
*/
NetClock::time_point
roundCloseTime (
    NetClock::time_point closeTime,
    NetClock::duration closeResolution);

/** Determines whether the current ledger should close at this time.

    This function should be called when a ledger is open and there is no close
    in progress, or when a transaction is received and no close is in progress.

    @param anyTransactions indicates whether any transactions have been received
    @param previousProposers proposers in the last closing
    @param proposersClosed proposers who have currently closed this ledger
    @param proposersValidated proposers who have validated the last closed
                              ledger
    @param previousTime time for the previous ledger to reach consensus
    @param currentTime  time since the previous ledger's
                           (possibly rounded) close time
    @param openTime     time waiting to close this ledger
    @param idleInterval the network's desired idle interval
    @param j            journal for logging
*/
bool
shouldCloseLedger (
    bool anyTransactions,
    int previousProposers,
    int proposersClosed,
    int proposersValidated,
    std::chrono::milliseconds previousTime,
    std::chrono::milliseconds currentTime, // Time since last ledger's close time
    std::chrono::milliseconds openTime,    // Time waiting to close this ledger
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
checkConsensusReached (int agreeing, int total, bool count_self);

/** Whether we have or don't have a consensus */
enum class ConsensusState
{
    No,           // We do not have consensus
    MovedOn,      // The network has consensus without us
    Yes           // We have consensus along with the network
};

/** Determine whether the network reached consensus and whether we joined.

    @param previousProposers proposers in the last closing (not including us)
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
checkConsensus (
    int previousProposers,
    int currentProposers,
    int currentAgree,
    int currentFinished,
    std::chrono::milliseconds previousAgreeTime,
    std::chrono::milliseconds currentAgreeTime,
    bool proposing,
    beast::Journal j);

//------------------------------------------------------------------------------

// These are protocol parameters used to control the behavior of the system and
// they should not be changed arbitrarily.

// The percentage threshold above which we can declare consensus.
auto constexpr minimumConsensusPercentage = 80;

using namespace std::chrono_literals;
// All possible close time resolutions. Values should not be duplicated.
std::chrono::seconds constexpr ledgerPossibleTimeResolutions[] =
    { 10s, 20s, 30s, 60s, 90s, 120s };

#ifndef _MSC_VER
// Initial resolution of ledger close time.
auto constexpr ledgerDefaultTimeResolution = ledgerPossibleTimeResolutions[2];
#else
// HH Remove this workaround of a VS bug when possible
auto constexpr ledgerDefaultTimeResolution = 30s;
#endif

// How often we increase the close time resolution
auto constexpr increaseLedgerTimeResolutionEvery = 8;

// How often we decrease the close time resolution
auto constexpr decreaseLedgerTimeResolutionEvery = 1;

// The number of seconds a ledger may remain idle before closing
auto constexpr LEDGER_IDLE_INTERVAL = 15s;

// The number of seconds a validation remains current after its ledger's close
// time. This is a safety to protect against very old validations and the time
// it takes to adjust the close time accuracy window
auto constexpr VALIDATION_VALID_WALL = 5min;

// The number of seconds a validation remains current after the time we first
// saw it. This provides faster recovery in very rare cases where the number
// of validations produced by the network is lower than normal
auto constexpr VALIDATION_VALID_LOCAL = 3min;

// The number of seconds before a close time that we consider a validation
// acceptable. This protects against extreme clock errors
auto constexpr VALIDATION_VALID_EARLY = 3min;

// The number of seconds we wait minimum to ensure participation
auto constexpr LEDGER_MIN_CONSENSUS = 2s;

// Minimum number of seconds to wait to ensure others have computed the LCL
auto constexpr LEDGER_MIN_CLOSE = 2s;

// How often we check state or change positions (in milliseconds)
auto constexpr LedgerStateCheckInterval = 1s;

// How long we consider a proposal fresh
auto constexpr PROPOSE_FRESHNESS = 20s;

// How often we force generating a new proposal to keep ours fresh
auto constexpr PROPOSE_INTERVAL = 12s;

// Avalanche tuning
// percentage of nodes on our UNL that must vote yes
auto constexpr AV_INIT_CONSENSUS_PCT = 50;

// percentage of previous close time before we advance
auto constexpr AV_MID_CONSENSUS_TIME = 50;

// percentage of nodes that most vote yes after advancing
auto constexpr AV_MID_CONSENSUS_PCT = 65;

// percentage of previous close time before we advance
auto constexpr AV_LATE_CONSENSUS_TIME = 85;

// percentage of nodes that most vote yes after advancing
auto constexpr AV_LATE_CONSENSUS_PCT = 70;

auto constexpr AV_STUCK_CONSENSUS_TIME = 200;
auto constexpr AV_STUCK_CONSENSUS_PCT = 95;

auto constexpr AV_CT_CONSENSUS_PCT = 75;

// The minimum amount of time to consider the previous round
// to have taken. This ensures that there is an opportunity
// for a round at each avalanche threshold even if the
// previous consensus was very fast. This should be at least
// twice the interval between proposals (0.7s) divided by
// the interval between mid and late consensus ([85-50]/100).
auto constexpr AV_MIN_CONSENSUS_TIME = 5s;

} // ripple

#endif
