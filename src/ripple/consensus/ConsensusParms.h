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

#include <chrono>
#include <cstddef>

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
    std::chrono::seconds validationVALID_WALL = std::chrono::minutes {5};

    /** Duration a validation remains current after first observed.

       The duration a validation remains current after the time we
       first saw it. This provides faster recovery in very rare cases where the
       number of validations produced by the network is lower than normal
    */
    std::chrono::seconds validationVALID_LOCAL = std::chrono::minutes {3};

    /**  Duration pre-close in which validations are acceptable.

        The number of seconds before a close time that we consider a validation
        acceptable. This protects against extreme clock errors
    */
    std::chrono::seconds validationVALID_EARLY = std::chrono::minutes {3};


    //! How long we consider a proposal fresh
    std::chrono::seconds proposeFRESHNESS = std::chrono::seconds {20};

    //! How often we force generating a new proposal to keep ours fresh
    std::chrono::seconds proposeINTERVAL = std::chrono::seconds {12};


    //-------------------------------------------------------------------------
    // Consensus durations are relative to the internal Consenus clock and use
    // millisecond resolution.

    //! The percentage threshold above which we can declare consensus.
    std::size_t minCONSENSUS_PCT = 80;

    //! The duration a ledger may remain idle before closing
    std::chrono::milliseconds ledgerIDLE_INTERVAL = std::chrono::seconds {15};

    //! The number of seconds we wait minimum to ensure participation
    std::chrono::milliseconds ledgerMIN_CONSENSUS =
        std::chrono::milliseconds {1950};

    //! Minimum number of seconds to wait to ensure others have computed the LCL
    std::chrono::milliseconds ledgerMIN_CLOSE = std::chrono::seconds {2};

    //! How often we check state or change positions
    std::chrono::milliseconds ledgerGRANULARITY = std::chrono::seconds {1};

    /** The minimum amount of time to consider the previous round
        to have taken.

        The minimum amount of time to consider the previous round
        to have taken. This ensures that there is an opportunity
        for a round at each avalanche threshold even if the
        previous consensus was very fast. This should be at least
        twice the interval between proposals (0.7s) divided by
        the interval between mid and late consensus ([85-50]/100).
    */
    std::chrono::milliseconds avMIN_CONSENSUS_TIME = std::chrono::seconds {5};

    //------------------------------------------------------------------------------
    // Avalanche tuning
    // As a function of the percent this round's duration is of the prior round,
    // we increase the threshold for yes vots to add a tranasaction to our
    // position.

    //! Percentage of nodes on our UNL that must vote yes
    std::size_t avINIT_CONSENSUS_PCT = 50;

    //! Percentage of previous round duration before we advance
    std::size_t avMID_CONSENSUS_TIME = 50;

    //! Percentage of nodes that most vote yes after advancing
    std::size_t avMID_CONSENSUS_PCT = 65;

    //! Percentage of previous round duration before we advance
    std::size_t avLATE_CONSENSUS_TIME = 85;

    //! Percentage of nodes that most vote yes after advancing
    std::size_t avLATE_CONSENSUS_PCT = 70;

    //! Percentage of previous round duration before we are stuck
    std::size_t avSTUCK_CONSENSUS_TIME = 200;

    //! Percentage of nodes that must vote yes after we are stuck
    std::size_t avSTUCK_CONSENSUS_PCT = 95;

    //! Percentage of nodes required to reach agreement on ledger close time
    std::size_t avCT_CONSENSUS_PCT = 75;

    //--------------------------------------------------------------------------

    /** Whether to use roundCloseTime or effCloseTime for reaching close time
        consensus.
        This was added to migrate from effCloseTime to roundCloseTime on the
        live network. The desired behavior (as given by the default value) is
        to use roundCloseTime during consensus voting and then use effCloseTime
        when accepting the consensus ledger.
    */
    bool useRoundedCloseTime = true;
};

}  // ripple
#endif
