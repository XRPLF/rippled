#ifndef XRPL_CONSENSUS_CONSENSUS_PARMS_H_INCLUDED
#define XRPL_CONSENSUS_CONSENSUS_PARMS_H_INCLUDED

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
    std::chrono::seconds const validationVALID_WALL = std::chrono::minutes{5};

    /** Duration a validation remains current after first observed.

       The duration a validation remains current after the time we
       first saw it. This provides faster recovery in very rare cases where the
       number of validations produced by the network is lower than normal
    */
    std::chrono::seconds const validationVALID_LOCAL = std::chrono::minutes{3};

    /**  Duration pre-close in which validations are acceptable.

        The number of seconds before a close time that we consider a validation
        acceptable. This protects against extreme clock errors
    */
    std::chrono::seconds const validationVALID_EARLY = std::chrono::minutes{3};

    //! How long we consider a proposal fresh
    std::chrono::seconds const proposeFRESHNESS = std::chrono::seconds{20};

    //! How often we force generating a new proposal to keep ours fresh
    std::chrono::seconds const proposeINTERVAL = std::chrono::seconds{12};

    //-------------------------------------------------------------------------
    // Consensus durations are relative to the internal Consensus clock and use
    // millisecond resolution.

    //! The percentage threshold above which we can declare consensus.
    std::size_t const minCONSENSUS_PCT = 80;

    //! The duration a ledger may remain idle before closing
    std::chrono::milliseconds const ledgerIDLE_INTERVAL =
        std::chrono::seconds{15};

    //! The number of seconds we wait minimum to ensure participation
    std::chrono::milliseconds const ledgerMIN_CONSENSUS =
        std::chrono::milliseconds{1950};

    /** The maximum amount of time to spend pausing for laggards.
     *
     *  This should be sufficiently less than validationFRESHNESS so that
     *  validators don't appear to be offline that are merely waiting for
     *  laggards.
     */
    std::chrono::milliseconds const ledgerMAX_CONSENSUS =
        std::chrono::seconds{15};

    //! Minimum number of seconds to wait to ensure others have computed the LCL
    std::chrono::milliseconds const ledgerMIN_CLOSE = std::chrono::seconds{2};

    //! How often we check state or change positions
    std::chrono::milliseconds const ledgerGRANULARITY = std::chrono::seconds{1};

    //! How long to wait before completely abandoning consensus
    std::size_t const ledgerABANDON_CONSENSUS_FACTOR = 10;

    /**
     * Maximum amount of time to give a consensus round
     *
     * Does not include the time to build the LCL, so there is no reason for a
     * round to go this long, regardless of how big the ledger is.
     */
    std::chrono::milliseconds const ledgerABANDON_CONSENSUS =
        std::chrono::seconds{120};

    /** The minimum amount of time to consider the previous round
        to have taken.

        The minimum amount of time to consider the previous round
        to have taken. This ensures that there is an opportunity
        for a round at each avalanche threshold even if the
        previous consensus was very fast. This should be at least
        twice the interval between proposals (0.7s) divided by
        the interval between mid and late consensus ([85-50]/100).
    */
    std::chrono::milliseconds const avMIN_CONSENSUS_TIME =
        std::chrono::seconds{5};

    //------------------------------------------------------------------------------
    // Avalanche tuning
    // As a function of the percent this round's duration is of the prior round,
    // we increase the threshold for yes votes to add a transaction to our
    // position.
    enum AvalancheState { init, mid, late, stuck };
    struct AvalancheCutoff
    {
        int const consensusTime;
        std::size_t const consensusPct;
        AvalancheState const next;
    };
    //! Map the consensus requirement avalanche state to the amount of time that
    //! must pass before moving to that state, the agreement percentage required
    //! at that state, and the next state. "stuck" loops back on itself because
    //! once we're stuck, we're stuck.
    //! This structure allows for "looping" of states if needed.
    std::map<AvalancheState, AvalancheCutoff> const avalancheCutoffs{
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

    //! Percentage of nodes required to reach agreement on ledger close time
    std::size_t const avCT_CONSENSUS_PCT = 75;

    //! Number of rounds before certain actions can happen.
    // (Moving to the next avalanche level, considering that votes are stalled
    // without consensus.)
    std::size_t const avMIN_ROUNDS = 2;

    //! Number of rounds before a stuck vote is considered unlikely to change
    //! because voting stalled
    std::size_t const avSTALLED_ROUNDS = 4;
};

inline std::pair<std::size_t, std::optional<ConsensusParms::AvalancheState>>
getNeededWeight(
    ConsensusParms const& p,
    ConsensusParms::AvalancheState currentState,
    int percentTime,
    std::size_t currentRounds,
    std::size_t minimumRounds)
{
    // at() can throw, but the map is built by hand to ensure all valid
    // values are available.
    auto const& currentCutoff = p.avalancheCutoffs.at(currentState);
    // Should we consider moving to the next state?
    if (currentCutoff.next != currentState && currentRounds >= minimumRounds)
    {
        // at() can throw, but the map is built by hand to ensure all
        // valid values are available.
        auto const& nextCutoff = p.avalancheCutoffs.at(currentCutoff.next);
        // See if enough time has passed to move on to the next.
        XRPL_ASSERT(
            nextCutoff.consensusTime >= currentCutoff.consensusTime,
            "ripple::getNeededWeight : next state valid");
        if (percentTime >= nextCutoff.consensusTime)
        {
            return {nextCutoff.consensusPct, currentCutoff.next};
        }
    }
    return {currentCutoff.consensusPct, {}};
}

}  // namespace ripple
#endif
