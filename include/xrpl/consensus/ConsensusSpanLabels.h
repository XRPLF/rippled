#pragma once

/**
 * Enum-to-label mappings for consensus span attribute values.
 *
 *  These mappings live in their own header so ConsensusSpanNames.h stays
 *  dependency-free like its siblings: the span-name and attribute-key
 *  constants are included by overlay and app translation units that have no
 *  use for the consensus enums, while these mappings are needed only by
 *  Consensus.h.
 *
 *      ConsensusSpanNames.h  (constants only, no domain deps)
 *              ^
 *              |  includes
 *      ConsensusSpanLabels.h  --includes--> ConsensusParms.h, ConsensusTypes.h
 *              ^
 *              |  includes
 *          Consensus.h
 */

#include <xrpl/consensus/ConsensusParms.h>
#include <xrpl/consensus/ConsensusSpanNames.h>
#include <xrpl/consensus/ConsensusTypes.h>

#include <string_view>

namespace xrpl::telemetry::consensus::span {

/**
 * Map a close-time avalanche state to its `avalanche_state` label.
 *
 * The regime escalates Init -> Mid -> Late -> Stuck, raising the close-time
 * agreement threshold at each step.
 *
 * @param state The state held by Consensus::closeTimeAvalancheState_.
 * @return The wire label; one of val::avalanche*.
 *
 * @note No default arm, so a new enumerator is a -Wswitch warning; the
 * fall-through returns "unknown" rather than a plausible-looking regime.
 */
[[nodiscard]] constexpr std::string_view
avalancheStateLabel(ConsensusParms::AvalancheState const state)
{
    switch (state)
    {
        case ConsensusParms::AvalancheState::Init:
            return val::avalancheInit;
        case ConsensusParms::AvalancheState::Mid:
            return val::avalancheMid;
        case ConsensusParms::AvalancheState::Late:
            return val::avalancheLate;
        case ConsensusParms::AvalancheState::Stuck:
            return val::avalancheStuck;
    }
    return val::unknown;
}

/**
 * Map a ledger-close decision to its `close_reason` label.
 *
 * @param reason The value returned by whyCloseLedger().
 * @return The wire label; one of val::close*.
 *
 * @note No default arm, so a new enumerator is a -Wswitch warning; the
 * fall-through returns "unknown". `keep_open` is mapped but never emitted.
 */
[[nodiscard]] constexpr std::string_view
closeReasonLabel(LedgerCloseReason const reason)
{
    switch (reason)
    {
        case LedgerCloseReason::KeepOpen:
            return val::closeKeepOpen;
        case LedgerCloseReason::Anomaly:
            return val::closeAnomaly;
        case LedgerCloseReason::OthersClosed:
            return val::closeOthersClosed;
        case LedgerCloseReason::Idle:
            return val::closeIdle;
        case LedgerCloseReason::Normal:
            return val::closeNormal;
    }
    return val::unknown;
}

}  // namespace xrpl::telemetry::consensus::span
