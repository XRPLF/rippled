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

#include <cstdint>

namespace ripple {

/** Calculates the close time resolution for the specified ledger.

    The Ripple protocol uses binning to represent time intervals using only one
    timestamp. This allows servers to derive a common time for the next ledger,
    without the need for perfectly synchronized clocks.
    The time resolution (i.e. the size of the intervals) is adjusted dynamically
    based on what happened in the last ledger, to try to avoid disagreements.
*/
int getNextLedgerTimeResolution (
    int previousResolution,
    bool previousAgree,
    std::uint32_t ledgerSeq);

/** Calculates the close time for a ledger, given a close time resolution.

    @param closeTime The time to be rouned.
    @param closeResolution The resolution
*/
std::uint32_t roundCloseTime (
    std::uint32_t closeTime,
    std::uint32_t closeResolution);

//------------------------------------------------------------------------------

// These are protocol parameters used to control the behavior of the system and
// they should not be changed arbitrarily.

// The percentage threshold above which we can declare consensus.
int const minimumConsensusPercentage = 80;

// All possible close time resolutions. Values should not be duplicated.
int const ledgerPossibleTimeResolutions[] = { 10, 20, 30, 60, 90, 120 };

// Initial resolution of ledger close time.
int const ledgerDefaultTimeResolution = ledgerPossibleTimeResolutions[2];

// How often we increase the close time resolution
int const increaseLedgerTimeResolutionEvery = 8;

// How often we decrease the close time resolution
int const decreaseLedgerTimeResolutionEvery = 1;

// The number of seconds a ledger may remain idle before closing
const int LEDGER_IDLE_INTERVAL = 15;

// The number of seconds a validation remains current after its ledger's close
// time. This is a safety to protect against very old validations and the time
// it takes to adjust the close time accuracy window
const int VALIDATION_VALID_WALL = 300;

// The number of seconds a validation remains current after the time we first
// saw it. This provides faster recovery in very rare cases where the number
// of validations produced by the network is lower than normal
const int VALIDATION_VALID_LOCAL = 180;

// The number of seconds before a close time that we consider a validation
// acceptable. This protects against extreme clock errors
const int VALIDATION_VALID_EARLY = 180;

// The number of milliseconds we wait minimum to ensure participation
const int LEDGER_MIN_CONSENSUS = 2000;

// Minimum number of milliseconds to wait to ensure others have computed the LCL
const int LEDGER_MIN_CLOSE = 2000;

// How often we check state or change positions (in milliseconds)
const int LEDGER_GRANULARITY = 1000;

// How long we consider a proposal fresh
const int PROPOSE_FRESHNESS = 20;

// How often we force generating a new proposal to keep ours fresh
const int PROPOSE_INTERVAL = 12;

// Avalanche tuning
// percentage of nodes on our UNL that must vote yes
const int AV_INIT_CONSENSUS_PCT = 50;

// percentage of previous close time before we advance
const int AV_MID_CONSENSUS_TIME = 50;

// percentage of nodes that most vote yes after advancing
const int AV_MID_CONSENSUS_PCT = 65;

// percentage of previous close time before we advance
const int AV_LATE_CONSENSUS_TIME = 85;

// percentage of nodes that most vote yes after advancing
const int AV_LATE_CONSENSUS_PCT = 70;

const int AV_STUCK_CONSENSUS_TIME = 200;
const int AV_STUCK_CONSENSUS_PCT = 95;

const int AV_CT_CONSENSUS_PCT = 75;

} // ripple

#endif
