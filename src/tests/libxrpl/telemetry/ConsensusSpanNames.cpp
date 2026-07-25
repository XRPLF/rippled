/**
 * @file ConsensusSpanNames.cpp
 * Unit tests for the consensus.validation.accept span contract in
 * ConsensusSpanNames.h (WP-B3).
 *
 * Two things are pinned here:
 *
 *  1. The span name and attribute keys, asserted literally. They are a
 *     cross-component contract with no compile-time link between the sides:
 *     the collector aggregates the `validation_status` and `accept_gated`
 *     spanmetrics dimensions on these exact strings, Tempo indexes them as
 *     searchable tags, and the workload validator names the span in
 *     expected_spans.json. A silent rename would break every one of those with
 *     no compile error, so the values are asserted as strings.
 *
 *  2. `validationStatusValue()`, the rule that turns a `ValStatus` into the
 *     attribute value. The caller passes `static_cast<int>(status)`, so the
 *     ENUMERATOR ORDER is part of the contract: asserting the whole domain here
 *     means inserting a `ValStatus` enumerator without updating the mapping
 *     fails this test rather than silently relabelling live spans. The rule is a
 *     pure constexpr function with no dependency on the validation store, so it
 *     is asserted directly -- no Application, no validations container, and no
 *     test-only hook added to production code to reach it.
 *
 * Compiled only when XRPL_ENABLE_TELEMETRY is defined, because that is the
 * configuration in which this test target has `src/` on its include path and can
 * therefore reach <xrpld/consensus/...>. The header itself is not
 * telemetry-conditional (constants and one constexpr function, no OTel types);
 * only this file's ability to include it is.
 */

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpld/consensus/ConsensusSpanNames.h>

#include <xrpl/telemetry/SpanNames.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string_view>

using namespace xrpl::telemetry;

namespace {

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

#endif  // XRPL_ENABLE_TELEMETRY
