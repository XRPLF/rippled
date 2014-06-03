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

namespace ripple {

// VFALCO Should rename ContinuousLedgerTiming to LedgerTiming

struct LedgerTiming; // for Log

SETUP_LOG (LedgerTiming)

// NOTE: First and last times must be repeated
int ContinuousLedgerTiming::LedgerTimeResolution[] = { 10, 10, 20, 30, 60, 90, 120, 120 };

// Called when a ledger is open and no close is in progress -- when a transaction is received and no close
// is in process, or when a close completes. Returns the number of seconds the ledger should be be open.
bool ContinuousLedgerTiming::shouldClose (
    bool anyTransactions,
    int previousProposers,      // proposers in the last closing
    int proposersClosed,        // proposers who have currently closed this ledgers
    int proposersValidated,     // proposers who have validated the last closed ledger
    int previousMSeconds,       // milliseconds the previous ledger took to reach consensus
    int currentMSeconds,        // milliseconds since the previous ledger closed
    int openMSeconds,           // milliseconds since the previous LCL was computed
    int idleInterval)           // network's desired idle interval
{
    if ((previousMSeconds < -1000) || (previousMSeconds > 600000) ||
            (currentMSeconds < -1000) || (currentMSeconds > 600000))
    {
        WriteLog (lsWARNING, LedgerTiming) <<
            "CLC::shouldClose range Trans=" << (anyTransactions ? "yes" : "no") <<
            " Prop: " << previousProposers << "/" << proposersClosed <<
            " Secs: " << currentMSeconds << " (last: " << previousMSeconds << ")";
        return true;
    }

    if (!anyTransactions)
    {
        // no transactions so far this interval
        if (proposersClosed > (previousProposers / 4)) // did we miss a transaction?
        {
            WriteLog (lsTRACE, LedgerTiming) << 
                "no transactions, many proposers: now (" << proposersClosed <<
                " closed, " << previousProposers << " before)";
            return true;
        }

#if 0 // This false triggers on the genesis ledger

        if (previousMSeconds > (1000 * (LEDGER_IDLE_INTERVAL + 2))) // the last ledger was very slow to close
        {
            WriteLog (lsTRACE, LedgerTiming) << "was slow to converge (p=" << (previousMSeconds) << ")";

            if (previousMSeconds < 2000)
                return previousMSeconds;

            return previousMSeconds - 1000;
        }

#endif
        return currentMSeconds >= (idleInterval * 1000); // normal idle
    }

    if ((openMSeconds < LEDGER_MIN_CLOSE) && ((proposersClosed + proposersValidated) < (previousProposers / 2 )))
    {
        WriteLog (lsDEBUG, LedgerTiming) <<
            "Must wait minimum time before closing";
        return false;
    }

    if ((currentMSeconds < previousMSeconds) && ((proposersClosed + proposersValidated) < previousProposers))
    {
        WriteLog (lsDEBUG, LedgerTiming) <<
            "We are waiting for more closes/validations";
        return false;
    }

    return true; // this ledger should close now
}

// Returns whether we have a consensus or not. If so, we expect all honest nodes
// to already have everything they need to accept a consensus. Our vote is 'locked in'.
bool ContinuousLedgerTiming::haveConsensus (
    int previousProposers,      // proposers in the last closing (not including us)
    int currentProposers,       // proposers in this closing so far (not including us)
    int currentAgree,           // proposers who agree with us
    int currentFinished,        // proposers who have validated a ledger after this one
    int previousAgreeTime,      // how long it took to agree on the last ledger
    int currentAgreeTime,       // how long we've been trying to agree
    bool forReal,               // deciding whether to stop consensus process
    bool& failed)               // we can't reach a consensus
{
    WriteLog (lsTRACE, LedgerTiming) <<
        "CLC::haveConsensus: prop=" << currentProposers <<
        "/" << previousProposers <<
        " agree=" << currentAgree << " validated=" << currentFinished <<
        " time=" << currentAgreeTime <<  "/" << previousAgreeTime <<
        (forReal ? "" : "X");

    if (currentAgreeTime <= LEDGER_MIN_CONSENSUS)
        return false;

    if (currentProposers < (previousProposers * 3 / 4))
    {
        // Less than 3/4 of the last ledger's proposers are present, we may need more time
        if (currentAgreeTime < (previousAgreeTime + LEDGER_MIN_CONSENSUS))
        {
            CondLog (forReal, lsTRACE, LedgerTiming) <<
                "too fast, not enough proposers";
            return false;
        }
    }

    // If 80% of current proposers (plus us) agree on a set, we have consensus
    if (((currentAgree * 100 + 100) / (currentProposers + 1)) > 80)
    {
        CondLog (forReal, lsINFO, LedgerTiming) << "normal consensus";
        failed = false;
        return true;
    }

    // If 80% of the nodes on your UNL have moved on, you should declare consensus
    if (((currentFinished * 100) / (currentProposers + 1)) > 80)
    {
        CondLog (forReal, lsWARNING, LedgerTiming) <<
            "We see no consensus, but 80% of nodes have moved on";
        failed = true;
        return true;
    }

    // no consensus yet
    CondLog (forReal, lsTRACE, LedgerTiming) << "no consensus";
    return false;
}

int ContinuousLedgerTiming::getNextLedgerTimeResolution (int previousResolution, bool previousAgree, int ledgerSeq)
{
    assert (ledgerSeq);

    if ((!previousAgree) && ((ledgerSeq % LEDGER_RES_DECREASE) == 0))
    {
        // reduce resolution
        int i = 1;

        while (LedgerTimeResolution[i] != previousResolution)
            ++i;

        return LedgerTimeResolution[i + 1];
    }

    if ((previousAgree) && ((ledgerSeq % LEDGER_RES_INCREASE) == 0))
    {
        // increase resolution
        int i = 1;

        while (LedgerTimeResolution[i] != previousResolution)
            ++i;

        return LedgerTimeResolution[i - 1];
    }

    return previousResolution;
}

} // ripple
