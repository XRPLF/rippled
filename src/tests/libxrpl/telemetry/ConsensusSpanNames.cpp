#include <xrpl/consensus/ConsensusSpanNames.h>

#include <xrpl/consensus/ConsensusParms.h>

#include <gtest/gtest.h>

#include <string_view>

/**
 * Contract tests for the consensus phase-span attribute constants.
 *
 *  The keys in ConsensusSpanNames.h are the single source of truth (L1) that
 *  `.github/scripts/otel-naming/check_otel_naming.py` derives its valid key
 *  set from, and that the collector's spanmetrics dimensions, the Tempo span
 *  filters and the Grafana dashboards query by literal string. A silent rename
 *  here compiles cleanly but blanks panels, so these tests pin the wire values.
 *  They need no telemetry runtime and run in every build.
 *
 *  Scope: the attributes carried by `consensus.phase.open` and
 *  `consensus.establish`. The round-level attrs are covered by the
 *  pre-existing key set and are deliberately NOT duplicated onto the phase
 *  children (a child span does not inherit parent attributes, but copying
 *  `ledger_seq` down would store the same value twice per trace).
 */

using namespace xrpl::telemetry::consensus::span;

TEST(ConsensusSpanNames, phase_open_start_attribute_keys)
{
    // Set once when the open-phase span is created in startRoundInternal().
    EXPECT_EQ(std::string_view(attr::startReason), "start_reason");
    EXPECT_EQ(std::string_view(attr::previousCloseAgree), "previous_close_agree");
    EXPECT_EQ(std::string_view(attr::peerPositionsAtOpen), "peer_positions_at_open");
    EXPECT_EQ(std::string_view(attr::earlyCloseTriggered), "early_close_triggered");
}

TEST(ConsensusSpanNames, phase_open_end_attribute_keys)
{
    // Existing end-of-phase metadata, pinned alongside the new key so a rename
    // of either shows up here.
    EXPECT_EQ(std::string_view(attr::openDurationMs), "open_duration_ms");
    EXPECT_EQ(std::string_view(attr::peerPositionsAtClose), "peer_positions_at_close");
    EXPECT_EQ(std::string_view(attr::txSetsAcquired), "tx_sets_acquired");
    EXPECT_EQ(std::string_view(attr::closeReason), "close_reason");
    EXPECT_EQ(std::string_view(attr::proposersValidated), "proposers_validated");
}

TEST(ConsensusSpanNames, close_reason_values_are_the_close_paths)
{
    // One per branch of whyCloseLedger() that closes the ledger. keep_open is
    // never emitted (the attribute is only set on the closing path) but is
    // labelled rather than left blank so the mapping is total.
    EXPECT_EQ(std::string_view(val::closeKeepOpen), "keep_open");
    EXPECT_EQ(std::string_view(val::closeAnomaly), "anomaly");
    EXPECT_EQ(std::string_view(val::closeOthersClosed), "others_closed");
    EXPECT_EQ(std::string_view(val::closeIdle), "idle");
    EXPECT_EQ(std::string_view(val::closeNormal), "normal");
}

TEST(ConsensusSpanNames, close_reason_label_maps_every_enum_state)
{
    // A missed branch would attribute a close to the wrong cause, which is the
    // whole point of the attribute, so every enumerator is asserted.
    EXPECT_EQ(closeReasonLabel(xrpl::LedgerCloseReason::KeepOpen), "keep_open");
    EXPECT_EQ(closeReasonLabel(xrpl::LedgerCloseReason::Anomaly), "anomaly");
    EXPECT_EQ(closeReasonLabel(xrpl::LedgerCloseReason::OthersClosed), "others_closed");
    EXPECT_EQ(closeReasonLabel(xrpl::LedgerCloseReason::Idle), "idle");
    EXPECT_EQ(closeReasonLabel(xrpl::LedgerCloseReason::Normal), "normal");
}

TEST(ConsensusSpanNames, close_reason_label_is_usable_at_compile_time)
{
    static_assert(
        closeReasonLabel(xrpl::LedgerCloseReason::Idle) == "idle",
        "closeReasonLabel must be constexpr-evaluable");
    SUCCEED();
}

TEST(ConsensusSpanNames, establish_attribute_keys)
{
    EXPECT_EQ(std::string_view(attr::disputesCountInitial), "disputes_count_initial");
    EXPECT_EQ(std::string_view(attr::avalancheState), "avalanche_state");
}

TEST(ConsensusSpanNames, start_reason_values_are_the_two_entry_paths)
{
    // startRoundInternal() is entered fresh, or re-entered by handleWrongLedger
    // after acquiring the correct prior ledger. A round that recovers emits a
    // SECOND consensus.phase.open span, so the label is what tells them apart.
    EXPECT_EQ(std::string_view(val::startInitial), "initial");
    EXPECT_EQ(std::string_view(val::startRecovered), "recovered");
}

TEST(ConsensusSpanNames, avalanche_state_values_match_the_parms_enum)
{
    EXPECT_EQ(std::string_view(val::avalancheInit), "init");
    EXPECT_EQ(std::string_view(val::avalancheMid), "mid");
    EXPECT_EQ(std::string_view(val::avalancheLate), "late");
    EXPECT_EQ(std::string_view(val::avalancheStuck), "stuck");
}

TEST(ConsensusSpanNames, avalanche_state_label_maps_every_enum_state)
{
    // A missed branch here would silently report the wrong convergence regime
    // for the round, so every enumerator is asserted explicitly rather than
    // round-tripped through a table.
    using AvalancheState = xrpl::ConsensusParms::AvalancheState;

    EXPECT_EQ(avalancheStateLabel(AvalancheState::Init), "init");
    EXPECT_EQ(avalancheStateLabel(AvalancheState::Mid), "mid");
    EXPECT_EQ(avalancheStateLabel(AvalancheState::Late), "late");
    EXPECT_EQ(avalancheStateLabel(AvalancheState::Stuck), "stuck");
}

TEST(ConsensusSpanNames, avalanche_state_label_is_usable_at_compile_time)
{
    // The mapping is consteval-safe so the label costs nothing at the call
    // site in endEstablishTracing().
    static_assert(
        avalancheStateLabel(xrpl::ConsensusParms::AvalancheState::Stuck) == "stuck",
        "avalancheStateLabel must be constexpr-evaluable");
    SUCCEED();
}
