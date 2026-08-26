/**
 * @file ConsensusSpanNames.cpp
 * Contract tests for the consensus span constants in ConsensusSpanNames.h.
 *
 * The keys and values here are the single source of truth (L1) that
 * `.github/scripts/otel-naming/check_otel_naming.py` derives its valid key set
 * from, and that the collector's spanmetrics dimensions, the Tempo span filters
 * and the Grafana dashboards query by literal string. A silent rename compiles
 * cleanly but blanks panels and breaks expected_spans.json, so these tests pin
 * the wire values as strings rather than comparing a constant to itself.
 *
 * Two groups, from the two work packages that built this surface:
 *
 *  1. The phase spans -- `consensus.phase.open` and `consensus.establish` --
 *     and the enum-to-value label mappings for close reason, start reason and
 *     avalanche state. The round-level attrs are deliberately NOT duplicated
 *     onto the phase children: a child span does not inherit parent attributes,
 *     but copying `ledger_seq` down would store the same value twice per trace.
 *
 *  2. The `consensus.validation.accept` span (WP-B3) and
 *     `validationStatusValue()`, the rule turning a `ValStatus` into the
 *     attribute value. The caller passes `static_cast<int>(status)`, so the
 *     ENUMERATOR ORDER is part of the contract: asserting the whole domain here
 *     means inserting an enumerator without updating the mapping fails this test
 *     rather than silently relabelling live spans. The rule is a pure constexpr
 *     function, so it is asserted directly -- no Application, no validations
 *     container, and no test-only hook added to production code to reach it.
 *
 * No XRPL_ENABLE_TELEMETRY guard, and none is needed: ConsensusSpanNames.h is
 * not telemetry-conditional, and SpanNames.h documents that its constants are
 * deliberately unguarded because call sites reference them even when SpanGuard
 * is a no-op. So these tests run in every build, including -Dtelemetry=OFF.
 */

#include <xrpl/consensus/ConsensusSpanNames.h>

#include <xrpl/consensus/ConsensusParms.h>
#include <xrpl/consensus/ConsensusSpanLabels.h>
#include <xrpl/consensus/ConsensusTypes.h>
#include <xrpl/telemetry/SpanNames.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string_view>

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

TEST(ConsensusSpanNames, establish_attribute_keys)
{
    // Qualified: DisputedTx tracks a second, per-transaction avalanche.
    EXPECT_EQ(std::string_view(attr::closeTimeAvalancheState), "close_time_avalanche_state");
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
    EXPECT_EQ(std::string_view(val::unknown), "unknown");
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

// Both mappings are constexpr, so the labels cost nothing at their call sites.
// These fire at compile time and need no test body.
static_assert(
    closeReasonLabel(xrpl::LedgerCloseReason::Idle) == "idle",
    "closeReasonLabel must be constexpr-evaluable");
static_assert(
    avalancheStateLabel(xrpl::ConsensusParms::AvalancheState::Stuck) == "stuck",
    "avalancheStateLabel must be constexpr-evaluable");

// The full span name is what hashSpan receives, what the collector keys span
// metrics on, and what expected_spans.json asserts. Check the composed value and
// both halves, so neither the segment nor the suffix can drift unnoticed.
TEST(ConsensusSpanNames, validation_accept_span_name_is_fully_composed)
{
    EXPECT_EQ(std::string_view(consensus::span::validationAccept), "consensus.validation.accept");
    EXPECT_EQ(std::string_view(seg::consensus), "consensus");
    EXPECT_EQ(std::string_view(consensus::span::op::validationAccept), "validation.accept");
}

// validation.accept and validation.receive are DIFFERENT events: receive is the
// overlay decoding a validation message on a peer thread; accept is the later,
// per-ledger step where a trusted validation drives the acceptance gate. They
// live in different traces (accept is keyed on the validated ledger hash), so
// collapsing the two names would merge two unrelated signals.
TEST(ConsensusSpanNames, validation_accept_is_distinct_from_validation_receive)
{
    EXPECT_NE(
        std::string_view(consensus::span::validationAccept),
        std::string_view(consensus::span::validationReceive));
    EXPECT_EQ(std::string_view(consensus::span::validationReceive), "consensus.validation.receive");
    // Nor is it the send-side span, which is this node emitting its own.
    EXPECT_NE(
        std::string_view(consensus::span::validationAccept),
        std::string_view(consensus::span::validationSend));
}

// The two attribute keys are bare lower_snake_case, never dotted: the dotted
// xrpl.<domain>.<field> form is reserved for resource attributes and fails the
// naming check (Rule A), and a non-snake_case key fails Rule G.
TEST(ConsensusSpanNames, validation_accept_attribute_keys_are_bare_underscore)
{
    std::array<std::string_view, 2> const keys{
        consensus::span::attr::validationStatus, consensus::span::attr::acceptGated};
    for (auto const key : keys)
    {
        EXPECT_EQ(key.find('.'), std::string_view::npos) << "dotted span attr key: " << key;
        EXPECT_FALSE(key.empty());
        for (char const c : key)
            EXPECT_TRUE((c >= 'a' && c <= 'z') || c == '_') << "bad char in key: " << key;
    }
    // Exact values: these are the collector dimension and Tempo tag names.
    EXPECT_EQ(std::string_view(consensus::span::attr::validationStatus), "validation_status");
    EXPECT_EQ(std::string_view(consensus::span::attr::acceptGated), "accept_gated");
}

// The status values are aggregated as a spanmetrics dimension, so each must be
// lower_snake_case too. `bad_seq` is the one that matters: to_string(ValStatus)
// returns camelCase "badSeq", which is why the values are spelled out in the
// header rather than reusing that function.
TEST(ConsensusSpanNames, validation_status_values_are_lower_snake_case)
{
    std::array<std::string_view, 6> const values{
        consensus::span::val::statusCurrent,
        consensus::span::val::statusStale,
        consensus::span::val::statusBadSeq,
        consensus::span::val::statusMultiple,
        consensus::span::val::statusConflicting,
        consensus::span::val::statusUnknown};
    for (auto const value : values)
    {
        EXPECT_FALSE(value.empty());
        for (char const c : value)
            EXPECT_TRUE((c >= 'a' && c <= 'z') || c == '_') << "bad char in value: " << value;
    }
    EXPECT_EQ(std::string_view(consensus::span::val::statusBadSeq), "bad_seq");
}

// Every value distinct: two ValStatus outcomes sharing a string would silently
// merge two series on the aggregated dimension.
TEST(ConsensusSpanNames, validation_status_values_are_mutually_distinct)
{
    std::array<std::string_view, 6> const values{
        consensus::span::val::statusCurrent,
        consensus::span::val::statusStale,
        consensus::span::val::statusBadSeq,
        consensus::span::val::statusMultiple,
        consensus::span::val::statusConflicting,
        consensus::span::val::statusUnknown};
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        for (std::size_t k = i + 1; k < values.size(); ++k)
            EXPECT_NE(values[i], values[k]) << "duplicate status value at " << i << "," << k;
    }
}

// THE RULE, over its whole live domain. The int arguments are
// static_cast<int>(ValStatus), so this also pins the enumerator order: Current,
// Stale, BadSeq, Multiple, Conflicting.
TEST(ConsensusSpanNames, validationStatusValue_maps_every_val_status)
{
    EXPECT_EQ(consensus::span::validationStatusValue(0), consensus::span::val::statusCurrent);
    EXPECT_EQ(consensus::span::validationStatusValue(1), consensus::span::val::statusStale);
    EXPECT_EQ(consensus::span::validationStatusValue(2), consensus::span::val::statusBadSeq);
    EXPECT_EQ(consensus::span::validationStatusValue(3), consensus::span::val::statusMultiple);
    EXPECT_EQ(consensus::span::validationStatusValue(4), consensus::span::val::statusConflicting);
}

// Only `current` continues to the acceptance gate; the other four mean the
// validation was counted for nothing. That split is the diagnostic value of the
// attribute -- a node stuck below quorum receiving validations that are all
// stale or bad-seq looks, from the outside, exactly like one receiving good ones.
TEST(ConsensusSpanNames, validationStatusValue_separates_counted_from_rejected)
{
    EXPECT_EQ(consensus::span::validationStatusValue(0), "current");
    for (int const rejected : {1, 2, 3, 4})
    {
        EXPECT_NE(consensus::span::validationStatusValue(rejected), "current")
            << "input: " << rejected;
    }
}

// NEGATIVE: an out-of-domain value yields the sentinel, never an empty string.
// An empty attribute value would add a blank series to the aggregated dimension,
// which is worse than a value labelled "unknown".
TEST(ConsensusSpanNames, validationStatusValue_out_of_domain_is_unknown_not_empty)
{
    for (int const bad : {-1, 5, 6, 99})
    {
        EXPECT_EQ(consensus::span::validationStatusValue(bad), consensus::span::val::statusUnknown)
            << "input: " << bad;
        EXPECT_FALSE(consensus::span::validationStatusValue(bad).empty());
    }
}

// The mapping is a compile-time rule, so a wrong value cannot even be built --
// the strongest form of the guarantee, checked by the compiler rather than at
// run time.
TEST(ConsensusSpanNames, validationStatusValue_is_a_compile_time_rule)
{
    static_assert(consensus::span::validationStatusValue(0) == "current");
    static_assert(consensus::span::validationStatusValue(2) == "bad_seq");
    static_assert(consensus::span::validationStatusValue(4) == "conflicting");
    static_assert(consensus::span::validationStatusValue(7) == "unknown");
    static_assert(consensus::span::validationStatusValue(-1) == "unknown");
    SUCCEED();
}

}  // namespace
