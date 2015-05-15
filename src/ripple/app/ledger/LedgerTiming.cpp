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

#include <BeastConfig.h>
#include <ripple/app/ledger/LedgerTiming.h>
#include <ripple/basics/Log.h>
#include <algorithm>
#include <iterator>

namespace ripple {

bool shouldCloseLedger (
    bool anyTransactions,
    int previousProposers,
    int proposersClosed,
    int proposersValidated,
    int previousMSeconds,
    int currentMSeconds,
    int openMSeconds,
    int idleInterval)
{
    if ((previousMSeconds < -1000) || (previousMSeconds > 600000) ||
            (currentMSeconds < -1000) || (currentMSeconds > 600000))
    {
        WriteLog (lsWARNING, LedgerTiming) <<
            "shouldCloseLedger Trans=" << (anyTransactions ? "yes" : "no") <<
            " Prop: " << previousProposers << "/" << proposersClosed <<
            " Secs: " << currentMSeconds << " (last: " << previousMSeconds << ")";
        return true;
    }

    if (!anyTransactions)
    {
        // did we miss a transaction?
        if (proposersClosed > (previousProposers / 4))
        {
            WriteLog (lsTRACE, LedgerTiming) <<
                "no transactions, many proposers: now (" << proposersClosed <<
                " closed, " << previousProposers << " before)";
            return true;
        }


        // Only close if we have idled for too long.
        return currentMSeconds >= (idleInterval * 1000); // normal idle
    }

    // If we have any transactions, we don't want to close too frequently:
    if (openMSeconds < LEDGER_MIN_CLOSE)
    {
        if ((proposersClosed + proposersValidated) < (previousProposers / 2 ))
        {
            WriteLog (lsDEBUG, LedgerTiming) <<
                "Must wait minimum time before closing";
            return false;
        }
    }

    if (currentMSeconds < previousMSeconds)
    {
        if ((proposersClosed + proposersValidated) < previousProposers)
        {
            WriteLog (lsDEBUG, LedgerTiming) <<
                "We are waiting for more closes/validations";
            return false;
        }
    }

    return true;
}

bool
checkConsensusReached (int agreeing, int proposing)
{
    int currentPercentage = (agreeing * 100) / (proposing + 1);

    return currentPercentage > minimumConsensusPercentage;
}

ConsensusState checkConsensus (
    int previousProposers,
    int currentProposers,
    int currentAgree,
    int currentFinished,
    int previousAgreeTime,
    int currentAgreeTime)
{
    WriteLog (lsTRACE, LedgerTiming) <<
        "checkConsensus: prop=" << currentProposers <<
        "/" << previousProposers <<
        " agree=" << currentAgree << " validated=" << currentFinished <<
        " time=" << currentAgreeTime <<  "/" << previousAgreeTime;

    if (currentAgreeTime <= LEDGER_MIN_CONSENSUS)
        return ConsensusState::No;

    if (currentProposers < (previousProposers * 3 / 4))
    {
        // Less than 3/4 of the last ledger's proposers are present; don't
        // rush: we may need more time.
        if (currentAgreeTime < (previousAgreeTime + LEDGER_MIN_CONSENSUS))
        {
            WriteLog (lsTRACE, LedgerTiming) <<
                "too fast, not enough proposers";
            return ConsensusState::No;
        }
    }

    // Have we, together with the nodes on our UNL list, reached the treshold
    // to declare consensus?
    if (checkConsensusReached (currentAgree + 1, currentProposers))
    {
        WriteLog (lsDEBUG, LedgerTiming) << "normal consensus";
        return ConsensusState::Yes;
    }

    // Have sufficient nodes on our UNL list moved on and reached the threshold
    // to declare consensus?
    if (checkConsensusReached (currentFinished, currentProposers))
    {
        WriteLog (lsWARNING, LedgerTiming) <<
            "We see no consensus, but 80% of nodes have moved on";
        return ConsensusState::MovedOn;
    }

    // no consensus yet
    WriteLog (lsTRACE, LedgerTiming) << "no consensus";
    return ConsensusState::No;
}

int getNextLedgerTimeResolution (
    int previousResolution,
    bool previousAgree,
    std::uint32_t ledgerSeq)
{
    assert (ledgerSeq);

    // Find the current resolution:
    auto iter = std::find (std::begin (ledgerPossibleTimeResolutions),
        std::end (ledgerPossibleTimeResolutions), previousResolution);
    assert (iter != std::end (ledgerPossibleTimeResolutions));

    // This should never happen, but just as a precaution
    if (iter == std::end (ledgerPossibleTimeResolutions))
        return previousResolution;

    // If we did not previously agree, we try to decrease the resolution to
    // improve the chance that we will agree now.
    if (!previousAgree && ((ledgerSeq % decreaseLedgerTimeResolutionEvery) == 0))
    {
        if (++iter != std::end (ledgerPossibleTimeResolutions))
            return *iter;
    }

    // If we previously agreed, we try to increase the resolution to determine
    // if we can continue to agree.
    if (previousAgree && ((ledgerSeq % increaseLedgerTimeResolutionEvery) == 0))
    {
        if (iter-- != std::begin (ledgerPossibleTimeResolutions))
            return *iter;
    }

    return previousResolution;
}

} // ripple
