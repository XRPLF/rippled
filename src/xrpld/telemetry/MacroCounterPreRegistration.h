#pragma once

/**
 * Startup pre-registration of the call-site macro counters.
 *
 * An XRPL_METRIC_COUNTER_* macro creates each series on its first Add, so
 * Prometheus first sees the series at the event's value, and increase() and
 * rate() read that event as 0. preRegisterMacroCounters() creates the
 * closed-domain call-site counters listed below, whose label values come from
 * a fixed list in the code, and records 0 on each label set. Called at
 * startup, before anything records, it gives an event after the first export
 * an earlier sample to be counted against. An event before the first export
 * shares that first sample with the zero, so it still reads as 0.
 *
 * @code
 *   ApplicationImp::setup()
 *     |  once, before ledger load, the overlay and consensus
 *     v
 *   preRegisterMacroCounters(app)
 *     |
 *     +--> XRPL_METRIC_COUNTER_PREREGISTER[_LABELED]      MetricMacros.h
 *            name, description   MetricNames.h
 *            label values        lval::*::all, lval::*::emittedPairs,
 *                                OperatingMode, ConsensusMode
 * @endcode
 *
 * A template on the app, as the macros are duck-typed on it, so the unit test
 * drives this exact list through a fake app.
 *
 * @code
 * // In ApplicationImp::setup(), after the registry is built and before
 * // anything can record into these counters:
 * telemetry::preRegisterMacroCounters(*this);
 *
 * // Edge case: telemetry disabled, or the registry stopped. The macros skip,
 * // so no instrument is created and meter() is never read.
 * telemetry::preRegisterMacroCounters(app);
 * @endcode
 *
 * @note Not covered: unl_fetch_total, whose `site` label comes from
 *       [validator_list_sites] rather than from the code;
 *       ledgers_closed_total and getobject_lookups_total, which record on
 *       every ledger close or object request; and getobject_rejected_total,
 *       which preRegisterGetObjectCounters() in PreRegisteredCounters.h
 *       creates.
 * @note Safe from any thread while the registry records, like the recording
 *       macros. Call it once, at startup; a second call adds no series. It
 *       reads the metrics registry and NetworkOPs::strOperatingMode(), both
 *       built before ApplicationImp::setup() runs.
 */

#ifdef XRPL_ENABLE_TELEMETRY
#include <xrpl/basics/MallocTrim.h>
#include <xrpl/consensus/ConsensusTypes.h>
#include <xrpl/server/NetworkOPs.h>
#include <xrpl/telemetry/MetricMacros.h>
#include <xrpl/telemetry/MetricNames.h>

#include <ranges>
#include <string>
#include <utility>
#include <vector>
#endif

namespace xrpl::telemetry {

#ifdef XRPL_ENABLE_TELEMETRY

/**
 * Every (from, to) pair of distinct operating modes, named as
 * NetworkOPs::setMode() labels state_changes_total.
 *
 * setMode() returns early when the mode does not change, so a mode never
 * moves to itself. OperatingMode numbers its modes from 0 to FULL.
 *
 * @param app Source of getOPs().strOperatingMode(), the recording site's
 *            naming function.
 * @return One label set per ordered pair, 20 for the five modes.
 */
template <class App>
[[nodiscard]] std::vector<CounterLabelSet>
modeTransitionLabelSets(App& app)
{
    auto const& ops = app.getOPs();
    auto const lastMode = std::to_underlying(OperatingMode::FULL);
    std::vector<CounterLabelSet> labelSets;
    for (auto const from : std::views::iota(0, lastMode + 1))
    {
        for (auto const to : std::views::iota(0, lastMode + 1))
        {
            if (from == to)
                continue;
            labelSets.push_back(
                CounterLabelSet{
                    {label::from, ops.strOperatingMode(static_cast<OperatingMode>(from), false)},
                    {label::to, ops.strOperatingMode(static_cast<OperatingMode>(to), false)}});
        }
    }
    return labelSets;
}

/**
 * The display names of every consensus mode a view change can leave.
 *
 * RCLConsensus counts a view change on entry to WrongLedger, labelled with
 * the mode being left, so WrongLedger itself is never a value. ConsensusMode
 * numbers its modes from 0 to SwitchedLedger.
 *
 * @return The names, as toDisplayString() spells them.
 */
[[nodiscard]] inline std::vector<std::string>
viewChangeModeNames()
{
    std::vector<std::string> names;
    for (auto const value :
         std::views::iota(0, std::to_underlying(ConsensusMode::SwitchedLedger) + 1))
    {
        if (auto const mode = static_cast<ConsensusMode>(value); mode != ConsensusMode::WrongLedger)
            names.push_back(toDisplayString(mode));
    }
    return names;
}

/**
 * Pre-register the overlay and peer counters.
 *
 * @param app Holds the metrics registry.
 */
template <class App>
void
preRegisterPeerCounters(App& app)
{
    XRPL_METRIC_COUNTER_PREREGISTER_LABELED(
        app,
        metric::overlayConnectTotal,
        overlayConnectTotalDesc,
        labelSetsFor(label::outcome, lval::overlay_connect::all));
    XRPL_METRIC_COUNTER_PREREGISTER_LABELED(
        app,
        metric::handshakeNegotiationFailTotal,
        handshakeNegotiationFailTotalDesc,
        labelSetsFor(label::reason, lval::handshake_fail::all));
    XRPL_METRIC_COUNTER_PREREGISTER_LABELED(
        app,
        metric::dnsResolveTotal,
        dnsResolveTotalDesc,
        labelSetsFor(label::outcome, lval::dns_resolve::all));
    XRPL_METRIC_COUNTER_PREREGISTER_LABELED(
        app,
        metric::peerAcceptTotal,
        peerAcceptTotalDesc,
        labelSetsFor(label::outcome, lval::peer_accept::all));
    // Only the pairs close() can report: two reasons end one direction only.
    XRPL_METRIC_COUNTER_PREREGISTER_LABELED(
        app,
        metric::peerDisconnectTotal,
        peerDisconnectTotalDesc,
        labelSetsForPairs(label::reason, label::direction, lval::disconnect::emittedPairs));
    XRPL_METRIC_COUNTER_PREREGISTER_LABELED(
        app,
        metric::serveRefusedTotal,
        serveRefusedTotalDesc,
        labelSetsForPairs(label::request, label::reason, lval::serve_refused::emittedPairs));
    XRPL_METRIC_COUNTER_PREREGISTER_LABELED(
        app,
        metric::peerTxRejectedTotal,
        peerTxRejectedTotalDesc,
        labelSetsFor(label::reason, lval::tx_rejected::all));
}

/**
 * Pre-register the ledger acquire, replay, validation, rotation and sweep
 * counters.
 *
 * @param app Holds the metrics registry.
 */
template <class App>
void
preRegisterLedgerCounters(App& app)
{
    XRPL_METRIC_COUNTER_PREREGISTER_LABELED(
        app,
        metric::syncAcquireSourceTotal,
        syncAcquireSourceTotalDesc,
        labelSetsFor(label::source, lval::acquire_source::all));
    XRPL_METRIC_COUNTER_PREREGISTER(
        app, metric::syncAcquireNoProgressTotal, syncAcquireNoProgressTotalDesc);
    XRPL_METRIC_COUNTER_PREREGISTER_LABELED(
        app,
        metric::syncAddnodeTotal,
        syncAddnodeTotalDesc,
        labelSetsFor(label::outcome, lval::addnode::all));
    XRPL_METRIC_COUNTER_PREREGISTER_LABELED(
        app,
        metric::ledgerReplayFallbackTotal,
        ledgerReplayFallbackTotalDesc,
        labelSetsFor(label::stage, lval::replay_fallback::all));
    XRPL_METRIC_COUNTER_PREREGISTER_LABELED(
        app,
        metric::ledgerReplayOutcomeTotal,
        ledgerReplayOutcomeTotalDesc,
        labelSetsFor(label::outcome, lval::replay_outcome::all));
    XRPL_METRIC_COUNTER_PREREGISTER_LABELED(
        app,
        metric::ledgerQuorumShortfallTotal,
        ledgerQuorumShortfallTotalDesc,
        labelSetsFor(label::stage, lval::quorum_shortfall::all));
    XRPL_METRIC_COUNTER_PREREGISTER(app, metric::ledgerJumpTotal, ledgerJumpTotalDesc);
    XRPL_METRIC_COUNTER_PREREGISTER(
        app, metric::rotationCopyNodeRestoreTotal, rotationCopyNodeRestoreTotalDesc);
    // The sweep publishes no trim counter where the trim is not measured, and
    // a zero there would report a trim that never ran.
    if constexpr (kMallocTrimSupported)
    {
        XRPL_METRIC_COUNTER_PREREGISTER(
            app, metric::sweepMallocTrimMinorFaultsTotal, sweepMallocTrimMinorFaultsTotalDesc);
        XRPL_METRIC_COUNTER_PREREGISTER(
            app, metric::sweepMallocTrimReclaimedKbTotal, sweepMallocTrimReclaimedKbTotalDesc);
    }
}

/**
 * Pre-register the two counters labelled with a mode.
 *
 * @param app Holds the metrics registry, and names the operating modes.
 */
template <class App>
void
preRegisterModeCounters(App& app)
{
    XRPL_METRIC_COUNTER_PREREGISTER_LABELED(
        app, metric::stateChangesTotal, stateChangesTotalDesc, modeTransitionLabelSets(app));
    XRPL_METRIC_COUNTER_PREREGISTER_LABELED(
        app,
        metric::consensusViewChangeTotal,
        consensusViewChangeTotalDesc,
        labelSetsFor(label::consensusMode, viewChangeModeNames()));
}

#endif  // XRPL_ENABLE_TELEMETRY

/**
 * Create the counters listed in preRegisterPeerCounters(),
 * preRegisterLedgerCounters() and preRegisterModeCounters(), at 0 on each
 * label set.
 *
 * Does nothing when telemetry is compiled out, disabled, or stopped.
 *
 * @param app The application, or a test fake that has the same
 *            getMetricsRegistry() and getOPs().strOperatingMode().
 */
template <class App>
void
preRegisterMacroCounters([[maybe_unused]] App& app)
{
#ifdef XRPL_ENABLE_TELEMETRY
    preRegisterPeerCounters(app);
    preRegisterLedgerCounters(app);
    preRegisterModeCounters(app);
#endif
}

}  // namespace xrpl::telemetry
