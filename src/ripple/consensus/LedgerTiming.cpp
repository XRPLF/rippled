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
#include <ripple/consensus/LedgerTiming.h>
#include <ripple/basics/Log.h>
#include <algorithm>
#include <iterator>

namespace ripple {

bool
shouldCloseLedger (
    bool anyTransactions,
    std::size_t previousProposers,
    std::size_t proposersClosed,
    std::size_t proposersValidated,
    std::chrono::milliseconds previousTime,
    std::chrono::milliseconds currentTime, // Time since last ledger's close time
    std::chrono::milliseconds openTime,    // Time waiting to close this ledger
    std::chrono::seconds idleInterval,
    beast::Journal j)
{
    using namespace std::chrono_literals;
    if ((previousTime < -1s) || (previousTime > 10min) ||
        (currentTime > 10min))
    {
        // These are unexpected cases, we just close the ledger
        JLOG (j.warn()) <<
            "shouldCloseLedger Trans=" << (anyTransactions ? "yes" : "no") <<
            " Prop: " << previousProposers << "/" << proposersClosed <<
            " Secs: " << currentTime.count() << " (last: " <<
            previousTime.count() << ")";
        return true;
    }

    if ((proposersClosed + proposersValidated) > (previousProposers / 2))
    {
        // If more than half of the network has closed, we close
        JLOG (j.trace()) << "Others have closed";
        return true;
    }

    if (!anyTransactions)
    {
        // Only close at the end of the idle interval
        return currentTime >= idleInterval; // normal idle
    }

    // Preserve minimum ledger open time
    if (openTime < LEDGER_MIN_CLOSE)
    {
        JLOG (j.debug()) <<
            "Must wait minimum time before closing";
        return false;
    }

    // Don't let this ledger close more than twice as fast as the previous
    // ledger reached consensus so that slower validators can slow down
    // the network
    if (openTime < (previousTime / 2))
    {
        JLOG (j.debug()) <<
            "Ledger has not been open long enough";
        return false;
    }

    // Close the ledger
    return true;
}

bool
checkConsensusReached (
    std::size_t agreeing,
    std::size_t total,
    bool count_self)
{
    // If we are alone, we have a consensus
    if (total == 0)
        return true;

    if (count_self)
    {
        ++agreeing;
        ++total;
    }

    int currentPercentage = (agreeing * 100) / total;

    return currentPercentage > minimumConsensusPercentage;
}

ConsensusState
checkConsensus (
    std::size_t previousProposers,
    std::size_t currentProposers,
    std::size_t currentAgree,
    std::size_t currentFinished,
    std::chrono::milliseconds previousAgreeTime,
    std::chrono::milliseconds currentAgreeTime,
    bool proposing,
    beast::Journal j)
{
    JLOG (j.trace()) <<
        "checkConsensus: prop=" << currentProposers <<
        "/" << previousProposers <<
        " agree=" << currentAgree << " validated=" << currentFinished <<
        " time=" << currentAgreeTime.count() <<  "/" << previousAgreeTime.count();

    if (currentAgreeTime <= LEDGER_MIN_CONSENSUS)
        return ConsensusState::No;

    if (currentProposers < (previousProposers * 3 / 4))
    {
        // Less than 3/4 of the last ledger's proposers are present; don't
        // rush: we may need more time.
        if (currentAgreeTime < (previousAgreeTime + LEDGER_MIN_CONSENSUS))
        {
            JLOG (j.trace()) <<
                "too fast, not enough proposers";
            return ConsensusState::No;
        }
    }

    // Have we, together with the nodes on our UNL list, reached the threshold
    // to declare consensus?
    if (checkConsensusReached (currentAgree, currentProposers, proposing))
    {
        JLOG (j.debug()) << "normal consensus";
        return ConsensusState::Yes;
    }

    // Have sufficient nodes on our UNL list moved on and reached the threshold
    // to declare consensus?
    if (checkConsensusReached (currentFinished, currentProposers, false))
    {
        JLOG (j.warn()) <<
            "We see no consensus, but 80% of nodes have moved on";
        return ConsensusState::MovedOn;
    }

    // no consensus yet
    JLOG (j.trace()) << "no consensus";
    return ConsensusState::No;
}

} // ripple
