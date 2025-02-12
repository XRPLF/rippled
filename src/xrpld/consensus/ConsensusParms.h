//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc.

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

#ifndef RIPPLE_CONSENSUS_CONSENSUS_PARMS_H_INCLUDED
#define RIPPLE_CONSENSUS_CONSENSUS_PARMS_H_INCLUDED

#include <xrpl/beast/utility/instrumentation.h>
#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>

namespace ripple {

/** Consensus algorithm parameters

    Parameters which control the consensus algorithm.  This are not
    meant to be changed arbitrarily.
*/
struct ConsensusParms
{
    explicit ConsensusParms() = default;

    //-------------------------------------------------------------------------
    // Validation and proposal durations are relative to NetClock times, so use
    // second resolution
    /** The duration a validation remains current after its ledger's
       close time.

        This is a safety to protect against very old validations and the time
        it takes to adjust the close time accuracy window.
    */
    std::chrono::seconds validationVALID_WALL = std::chrono::minutes{5};

    /** Duration a validation remains current after first observed.

       The duration a validation remains current after the time we
       first saw it. This provides faster recovery in very rare cases where the
       number of validations produced by the network is lower than normal
    */
    std::chrono::seconds validationVALID_LOCAL = std::chrono::minutes{3};

    /**  Duration pre-close in which validations are acceptable.

        The number of seconds before a close time that we consider a validation
        acceptable. This protects against extreme clock errors
    */
    std::chrono::seconds validationVALID_EARLY = std::chrono::minutes{3};

    //! How long we consider a proposal fresh
    std::chrono::seconds proposeFRESHNESS = std::chrono::seconds{20};

    //! How often we force generating a new proposal to keep ours fresh
    std::chrono::seconds proposeINTERVAL = std::chrono::seconds{12};

    //-------------------------------------------------------------------------
    // Consensus durations are relative to the internal Consensus clock and use
    // millisecond resolution.

    //! The percentage threshold above which we can declare consensus.
    std::size_t minCONSENSUS_PCT = 80;

    //! The duration a ledger may remain idle before closing
    std::chrono::milliseconds ledgerIDLE_INTERVAL = std::chrono::seconds{15};

    //! The number of seconds we wait minimum to ensure participation
    std::chrono::milliseconds ledgerMIN_CONSENSUS =
        std::chrono::milliseconds{1950};

    /** The maximum amount of time to spend pausing for laggards.
     *
     *  This should be sufficiently less than validationFRESHNESS so that
     *  validators don't appear to be offline that are merely waiting for
     *  laggards.
     */
    std::chrono::milliseconds ledgerMAX_CONSENSUS = std::chrono::seconds{15};

    //! Minimum number of seconds to wait to ensure others have computed the LCL
    std::chrono::milliseconds ledgerMIN_CLOSE = std::chrono::seconds{2};

    //! How often we check state or change positions
    std::chrono::milliseconds ledgerGRANULARITY = std::chrono::seconds{1};

    //! How long to wait before completely abandoning consensus
    std::size_t ledgerABANDON_CONSENSUS_FACTOR = 10;

    /**
     * Maximum amount of time to give a consensus round
     *
     * Does not include the time to build the LCL, so there is no reason for a
     * round to go this long, regardless of how big the ledger is.
     */
    std::chrono::milliseconds ledgerABANDON_CONSENSUS =
        std::chrono::seconds{60};

    /** The minimum amount of time to consider the previous round
        to have taken.

        The minimum amount of time to consider the previous round
        to have taken. This ensures that there is an opportunity
        for a round at each avalanche threshold even if the
        previous consensus was very fast. This should be at least
        twice the interval between proposals (0.7s) divided by
        the interval between mid and late consensus ([85-50]/100).
    */
    std::chrono::milliseconds avMIN_CONSENSUS_TIME = std::chrono::seconds{5};

    //------------------------------------------------------------------------------
    // Avalanche tuning
    // As a function of the percent this round's duration is of the prior round,
    // we increase the threshold for yes votes to add a transaction to our
    // position.

    //! Percentage of nodes required to reach agreement on ledger close time
    std::size_t avCT_CONSENSUS_PCT = 75;

    //! Number of rounds before certain actions can happen.
    // (Moving to the next avalanche level, considering that votes are in a
    // stable state without consensus.)
    std::size_t avMIN_ROUNDS = 2;

    //! Number of rounds before a stuck vote is considered unlikely to change
    //! because voting is in an undesriable stable state
    std::size_t avSTUCK_VOTE_ROUNDS = 5;

    enum AvalancheState { init, mid, late, stuck };
    struct AvalancheCutoff
    {
        std::size_t const consensusTime;
        std::size_t const consensusPct;
        AvalancheState const next;
    };
    std::map<AvalancheState, AvalancheCutoff> avalancheCutoffs = {
        // {state, {time, percent, nextState}},
        // Initial state: 50% of nodes must vote yes
        {init, {0, 50, mid}},
        // mid-consensus starts after 50% of the previous round time, and
        // requires 65% yes
        {mid, {50, 65, late}},
        // late consensus starts after 85% time, and requires 70% yes
        {late, {85, 70, stuck}},
        // we're stuck after 2x time, requires 95% yes votes
        {stuck, {200, 95, stuck}},
    };
};

inline std::pair<std::size_t, std::optional<ConsensusParms::AvalancheState>>
getNeededWeight(
    ConsensusParms const& p,
    ConsensusParms::AvalancheState currentState,
    int percentTime,
    std::function<bool(ConsensusParms::AvalancheCutoff const&)>
        considerNextCallback)
{
    // at() can throw, but the map is built by hand to ensure all valid
    // values are available.
    auto const& currentCutoff = p.avalancheCutoffs.at(currentState);
    // Should we consider moving to the next state?
    if (currentCutoff.next != currentState &&
        (!considerNextCallback || considerNextCallback(currentCutoff)))
    {
        // at() can throw, but the map is built by hand to ensure all
        // valid values are available.
        auto const& nextCutoff = p.avalancheCutoffs.at(currentCutoff.next);
        // See if enough time has passed to move on to the next.
        XRPL_ASSERT(
            nextCutoff.consensusTime,
            "ripple::DisputedTx::updateVote next state valid");
        if (percentTime >= nextCutoff.consensusTime)
        {
            return {nextCutoff.consensusPct, currentCutoff.next};
        }
    }
    return {currentCutoff.consensusPct, {}};
}

}  // namespace ripple
#endif
