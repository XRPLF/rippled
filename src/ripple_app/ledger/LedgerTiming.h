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

#ifndef __LEDGERTIMING__
#define __LEDGERTIMING__

namespace ripple {

// The number of seconds a ledger may remain idle before closing
#   define LEDGER_IDLE_INTERVAL     15

// The number of seconds a validation remains current after its ledger's close time
// This is a safety to protect against very old validations and the time it takes to adjust
// the close time accuracy window
#   define LEDGER_VAL_INTERVAL      300

// The number of seconds before a close time that we consider a validation acceptable
// This protects against extreme clock errors
#   define LEDGER_EARLY_INTERVAL    180

// The number of milliseconds we wait minimum to ensure participation
#   define LEDGER_MIN_CONSENSUS     2000

// The number of milliseconds we wait minimum to ensure others have computed the LCL
#   define LEDGER_MIN_CLOSE         2000

// Initial resolution of ledger close time
#   define LEDGER_TIME_ACCURACY     30

// How often to increase resolution
#   define LEDGER_RES_INCREASE      8

// How often to decrease resolution
#   define LEDGER_RES_DECREASE      1

// How often we check state or change positions (in milliseconds)
#   define LEDGER_GRANULARITY       1000

// The percentage of active trusted validators that must be able to
// keep up with the network or we consider the network overloaded
#   define LEDGER_NET_RATIO         70

// How long we consider a proposal fresh
#   define PROPOSE_FRESHNESS        20

// How often we force generating a new proposal to keep ours fresh
#   define PROPOSE_INTERVAL         12

// Avalanche tuning
#   define AV_INIT_CONSENSUS_PCT    50  // percentage of nodes on our UNL that must vote yes

#   define AV_MID_CONSENSUS_TIME    50  // percentage of previous close time before we advance
#   define AV_MID_CONSENSUS_PCT     65  // percentage of nodes that most vote yes after advancing

#   define AV_LATE_CONSENSUS_TIME   85  // percentage of previous close time before we advance
#   define AV_LATE_CONSENSUS_PCT    70  // percentage of nodes that most vote yes after advancing

#   define AV_STUCK_CONSENSUS_TIME  200
#   define AV_STUCK_CONSENSUS_PCT   95

#   define AV_CT_CONSENSUS_PCT      75

class ContinuousLedgerTiming
{
public:

    static int LedgerTimeResolution[];

    // Returns the number of seconds the ledger was or should be open
    // Call when a consensus is reached and when any transaction is relayed to be added
    static bool shouldClose (
        bool anyTransactions,
        int previousProposers,      int proposersClosed,    int proposerersValidated,
        int previousMSeconds,       int currentMSeconds,    int openMSeconds,
        int idleInterval);

    static bool haveConsensus (
        int previousProposers,      int currentProposers,
        int currentAgree,           int currentClosed,
        int previousAgreeTime,      int currentAgreeTime,
        bool forReal,               bool& failed);

    static int getNextLedgerTimeResolution (int previousResolution, bool previousAgree, int ledgerSeq);
};

} // ripple

#endif
