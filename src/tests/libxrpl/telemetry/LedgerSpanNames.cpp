/**
 * @file LedgerSpanNames.cpp
 * Unit tests for the sync-diagnostic span contracts in LedgerSpanNames.h and
 * PeerSpanNames.h: `ledger.acquire` and, in the block at the end of this
 * file, `txset.acquire`, the three `ledger.acquire.{header,astree,txtree}`
 * phase children, `ledger.serve` and `peer.dial`.
 *
 * Two things are pinned here:
 *
 *  1. The literal attribute keys and outcome values. They are a cross-component
 *     contract: the collector aggregates the spanmetrics `outcome` dimension on
 *     these exact strings, Tempo indexes `ledger_hash` as a dedicated span
 *     column, and the workload validator asserts them by name in
 *     expected_spans.json. A silent rename would break every one of those with
 *     no compile error, so the values are asserted literally.
 *
 *  2. `acquireOutcome()`, the rule behind "a ledger.acquire span always carries
 *     an outcome". InboundLedger has four exits -- done(), the local-store
 *     shortcut, the "can never be acquired" exit, and the destructor when the
 *     sweeper drops a fetch -- and every one derives its value from this
 *     function. Asserting the function over its whole input domain therefore
 *     asserts the outcome of every exit path, including the destructor path --
 *     the one exit with no explicit success or failure of its own, and so the
 *     one most easily left without an outcome.
 *     The rule is a pure constexpr function with no dependency on
 *     InboundLedger, so it is asserted directly here: no Application, no peer
 *     set, and no test-only hook added to production code to reach it.
 *
 * The spans in that block follow the same two rules, with three more pure functions
 * standing in for their emitters' exits: `phaseOutcome()` for the acquire
 * phases and tx-set fetch, and `serveObjectType()` / `serveOutcome()` for the
 * eight exits of PeerImp::processLedgerRequest. Every one is asserted over its
 * whole input domain, which is what proves no exit can leave a span without an
 * outcome -- the property the emitters rely on and that no compiler enforces.
 *
 * Compiled in every build, telemetry on or off. Nothing here needs the OTel
 * SDK: the span-name and outcome headers hold only constants and constexpr
 * functions, `src/` is on this target's include path unconditionally, and the
 * few guard assertions below use a default-constructed SpanGuard, which is
 * inactive in either configuration.
 */

#include <xrpld/app/ledger/detail/LedgerSpanNames.h>

#include <xrpld/overlay/detail/PeerSpanNames.h>

#include <xrpl/telemetry/SpanGuard.h>
#include <xrpl/telemetry/SpanNames.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

using namespace xrpl::telemetry;

namespace {

TEST(LedgerSpanNames, acquire_span_name_is_dot_qualified)
{
    // InboundLedger::init builds the span name as seg::ledger + "." + this
    // suffix, which is the "ledger.acquire" every dashboard and the validator
    // query by. Assert both halves so the composed name cannot drift.
    EXPECT_EQ(std::string_view(seg::ledger), "ledger");
    EXPECT_EQ(std::string_view(ledger_span::op::acquire), "acquire");
}

TEST(LedgerSpanNames, acquire_attribute_keys_match_collector_and_tempo)
{
    // `outcome` and `acquire_reason` are the two spanmetrics dimensions listed
    // under "# ledger.acquire dimensions" in BOTH collector configs.
    EXPECT_EQ(std::string_view(ledger_span::attr::outcome), "outcome");
    EXPECT_EQ(std::string_view(ledger_span::attr::acquireReason), "acquire_reason");
    // Span-only attributes: asserted by expected_spans.json, and ledger_hash is
    // the dedicated Parquet span column in tempo.yaml.
    EXPECT_EQ(std::string_view(ledger_span::attr::ledgerHash), "ledger_hash");
    EXPECT_EQ(std::string_view(ledger_span::attr::ledgerSeq), "ledger_seq");
    EXPECT_EQ(std::string_view(ledger_span::attr::timeouts), "timeouts");
    EXPECT_EQ(std::string_view(ledger_span::attr::peerCount), "peer_count");
}

TEST(LedgerSpanNames, attribute_keys_are_bare_underscore_never_dotted)
{
    // The naming spec reserves dotted keys for resource attributes; a dotted
    // span-attribute key fails the CI naming check. Assert the property, not
    // just the spelling, so a future key added here is covered too.
    for (std::string_view const key :
         {std::string_view(ledger_span::attr::outcome),
          std::string_view(ledger_span::attr::acquireReason),
          std::string_view(ledger_span::attr::ledgerHash),
          std::string_view(ledger_span::attr::ledgerSeq),
          std::string_view(ledger_span::attr::timeouts),
          std::string_view(ledger_span::attr::peerCount)})
    {
        EXPECT_EQ(key.find('.'), std::string_view::npos) << "dotted span-attr key: " << key;
        EXPECT_FALSE(key.empty());
    }
}

TEST(LedgerSpanNames, outcome_values_are_the_three_terminal_states)
{
    // These become the `outcome` dimension's value set (cardinality 3), which
    // is what makes it safe as a metric dimension.
    EXPECT_EQ(std::string_view(ledger_span::val::complete), "complete");
    EXPECT_EQ(std::string_view(ledger_span::val::failed), "failed");
    EXPECT_EQ(std::string_view(ledger_span::val::abandoned), "abandoned");
}

TEST(LedgerSpanNames, outcome_values_are_mutually_distinct)
{
    // Cause, not just state: the panel splits acquisitions by this attribute,
    // so two outcomes sharing a value would silently merge two different
    // failure modes into one line.
    EXPECT_NE(
        std::string_view(ledger_span::val::abandoned),
        std::string_view(ledger_span::val::complete));
    EXPECT_NE(
        std::string_view(ledger_span::val::abandoned), std::string_view(ledger_span::val::failed));
    EXPECT_NE(
        std::string_view(ledger_span::val::complete), std::string_view(ledger_span::val::failed));
}

TEST(LedgerSpanNames, acquire_reason_values_mirror_the_reason_enum)
{
    // One value per InboundLedger::Reason, mapped by the switch in init().
    EXPECT_EQ(std::string_view(ledger_span::val::history), "history");
    EXPECT_EQ(std::string_view(ledger_span::val::consensus), "consensus");
    EXPECT_EQ(std::string_view(ledger_span::val::generic), "generic");
}

TEST(LedgerSpanNames, acquireOutcome_normal_done_path_is_complete)
{
    // done() after all data was assembled: complete_ set, failed_ clear.
    EXPECT_EQ(ledger_span::acquireOutcome(/*failed=*/false, /*complete=*/true), "complete");
}

TEST(LedgerSpanNames, acquireOutcome_local_complete_path_is_complete)
{
    // The tryDB local-store shortcut in init() reaches the same flag state as
    // done(), so it must produce the same outcome -- this is the exit that used
    // to end the span with no outcome at all.
    EXPECT_EQ(ledger_span::acquireOutcome(/*failed=*/false, /*complete=*/true), "complete");
}

TEST(LedgerSpanNames, acquireOutcome_failed_path_is_failed)
{
    // Terminal error: bad data, a zero account hash, or the retry budget ran
    // out. Reached from done() and from the early-return in init().
    EXPECT_EQ(ledger_span::acquireOutcome(/*failed=*/true, /*complete=*/false), "failed");
}

TEST(LedgerSpanNames, acquireOutcome_abort_path_is_abandoned)
{
    // The destructor / sweep path: neither flag set, because the fetch never
    // reached a result. This is the assertion the whole change exists for -- an
    // acquire swept while stuck is still counted, with `abandoned` naming why.
    EXPECT_EQ(ledger_span::acquireOutcome(/*failed=*/false, /*complete=*/false), "abandoned");
}

TEST(LedgerSpanNames, acquireOutcome_failure_wins_over_completion)
{
    // Edge case: both flags set. A fetch that hit a terminal error is not a
    // success regardless of what was assembled, so `failed` must win. Pinned
    // because flipping the precedence would quietly relabel real failures as
    // successes and hide them from the outcome rate.
    EXPECT_EQ(ledger_span::acquireOutcome(/*failed=*/true, /*complete=*/true), "failed");
}

TEST(LedgerSpanNames, acquireOutcome_covers_its_whole_input_domain)
{
    // No input combination yields an empty or unknown value, which is the
    // property that guarantees an exit path can never end up with a blank
    // outcome. Also asserts every result is one of the three declared values,
    // so the spanmetrics dimension can never gain an unexpected fourth.
    for (bool const failed : {false, true})
    {
        for (bool const complete : {false, true})
        {
            auto const outcome = ledger_span::acquireOutcome(failed, complete);
            EXPECT_FALSE(outcome.empty()) << "failed=" << failed << " complete=" << complete;
            EXPECT_TRUE(
                outcome == std::string_view(ledger_span::val::complete) ||
                outcome == std::string_view(ledger_span::val::failed) ||
                outcome == std::string_view(ledger_span::val::abandoned))
                << "undeclared outcome '" << outcome << "' for failed=" << failed
                << " complete=" << complete;
        }
    }
}

TEST(LedgerSpanNames, acquireOutcome_is_a_compile_time_rule)
{
    // constexpr, so the rule costs nothing at the four call sites and can be
    // asserted by the compiler itself. static_assert here is the strongest
    // available statement that the mapping is fixed.
    static_assert(ledger_span::acquireOutcome(false, true) == std::string_view("complete"));
    static_assert(ledger_span::acquireOutcome(true, false) == std::string_view("failed"));
    static_assert(ledger_span::acquireOutcome(false, false) == std::string_view("abandoned"));
    static_assert(ledger_span::acquireOutcome(true, true) == std::string_view("failed"));
    SUCCEED();
}

TEST(LedgerSpanNames, inactive_guard_finalize_sequence_is_a_no_op)
{
    // Negative / disabled path. A default-constructed SpanGuard is the exact
    // state InboundLedger::acquireSpan_ holds when telemetry is off or the
    // ledger trace category is disabled: `operator bool()` is false and every
    // setter is inert. Drive the whole finalize sequence against it -- the same
    // calls, in the same order, that finalizeAcquireSpan() makes -- and assert
    // the guard stays inactive and nothing crashes. This is what proves the
    // added abort-path finalization emits nothing on a node with telemetry
    // disabled, including from the destructor.
    SpanGuard guard;
    ASSERT_FALSE(static_cast<bool>(guard));

    guard.setAttribute(ledger_span::attr::ledgerHash, "0123456789ABCDEF");
    guard.setAttribute(ledger_span::attr::ledgerSeq, static_cast<std::int64_t>(12345));
    guard.setAttribute(ledger_span::attr::acquireReason, ledger_span::val::history);
    guard.setAttribute(
        ledger_span::attr::outcome,
        ledger_span::acquireOutcome(/*failed=*/false, /*complete=*/false));
    guard.setAttribute(ledger_span::attr::timeouts, static_cast<std::int64_t>(6));
    guard.setAttribute(ledger_span::attr::peerCount, static_cast<std::int64_t>(0));

    // Still inactive: no span was created, so none can be exported.
    EXPECT_FALSE(static_cast<bool>(guard));
    // activateIfLive on an empty handle yields a null activation, which is the
    // path both the destructor's abort log and done()'s outcome log take when
    // telemetry is disabled.
    std::optional<SpanGuard> const empty;
    {
        auto const activation = activateIfLive(empty);
    }
    EXPECT_FALSE(empty.has_value());
}

// ===========================================================================
// The sync-diagnostic spans
//
// Same contract as the ledger.acquire block above and asserted the same way:
// the wire names and attribute keys are pinned literally because they are a
// cross-component contract (the collector aggregates on them, Tempo indexes
// them, expected_spans.json asserts them by name, and a dashboard PromQL
// selector matches them -- a rename would break all four with no compile
// error), and the outcome rules are pinned over their whole input domain
// because they are what guarantees every exit path of every new span records
// an outcome.
// ===========================================================================

TEST(LedgerSpanNames, txset_acquire_span_name_is_dot_qualified)
{
    // TransactionAcquire::init builds the name as prefix::txset + "." +
    // op::acquire. A tx set is not a ledger, so it gets its own root segment
    // rather than hiding under `ledger.` -- assert both halves so the composed
    // "txset.acquire" cannot drift from what the dashboard queries.
    EXPECT_EQ(std::string_view(ledger_span::prefix::txset), "txset");
    EXPECT_EQ(std::string_view(ledger_span::op::acquire), "acquire");
}

TEST(LedgerSpanNames, txset_round_request_event_name_is_composed_not_literal)
{
    // The event TransactionAcquire fires once per requesting round. Pinned
    // literally for the same reason the span names are: a TraceQL query for
    // `event.name = "round.request"` and the runbook both name this exact
    // string, and neither would break at compile time if it were renamed.
    EXPECT_EQ(std::string_view(ledger_span::event::roundRequest), "round.request");
}

TEST(LedgerSpanNames, txset_round_request_event_name_is_one_dotted_pair)
{
    // The property, not just the spelling: two lower_snake_case segments joined
    // by exactly one dot, the same shape as every other event constant. A
    // camelCase or spaced name would pass the compiler and fail the CI naming
    // check.
    auto const name = std::string_view(ledger_span::event::roundRequest);
    auto const dot = name.find('.');
    ASSERT_NE(dot, std::string_view::npos);
    EXPECT_EQ(name.find('.', dot + 1), std::string_view::npos);
    for (auto const segment : {name.substr(0, dot), name.substr(dot + 1)})
    {
        EXPECT_FALSE(segment.empty());
        for (char const c : segment)
        {
            EXPECT_TRUE((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')
                << "not lower_snake_case: " << name;
        }
    }
}

TEST(LedgerSpanNames, txset_round_request_event_name_is_not_a_span_name)
{
    // Negative case. Events and spans live in different TraceQL scopes, so a
    // name shared with a span would make a query for one silently match the
    // other.
    EXPECT_NE(
        std::string_view(ledger_span::event::roundRequest),
        std::string_view(ledger_span::acquireFull));
    EXPECT_NE(
        std::string_view(ledger_span::event::roundRequest),
        std::string_view(ledger_span::acquireHeader));
    EXPECT_NE(
        std::string_view(ledger_span::event::roundRequest),
        std::string_view(ledger_span::op::acquire));
}

TEST(LedgerSpanNames, round_identity_event_attribute_keys_match_the_shared_constants)
{
    // The two keys the round.request event carries. Bare underscore, never
    // dotted -- a dotted key is reserved for resource attributes and fails the
    // CI naming check.
    EXPECT_EQ(std::string_view(ledger_span::attr::currentLedgerHash), "current_ledger_hash");
    EXPECT_EQ(std::string_view(ledger_span::attr::currentLedgerSeq), "current_ledger_seq");
    for (std::string_view const key :
         {std::string_view(ledger_span::attr::currentLedgerHash),
          std::string_view(ledger_span::attr::currentLedgerSeq)})
    {
        EXPECT_EQ(key.find('.'), std::string_view::npos) << "dotted span-attr key: " << key;
        EXPECT_FALSE(key.empty());
    }
}

TEST(LedgerSpanNames, round_identity_keys_are_re_exports_not_copies)
{
    // The load-bearing assertion behind "no new attribute constants". These are
    // `using` re-exports of the shared keys in SpanNames.h, so they are the SAME
    // objects -- assert the addresses, because two separate definitions with
    // equal text would pass a string comparison and then drift apart the first
    // time one of them is edited.
    //
    // The shared side is spelled out in full rather than as bare `attr::`. A
    // unity build compiles this file alongside the consensus span-name tests,
    // whose own directive imports consensus::span::attr, and a bare `attr::`
    // then matches both that and the xrpl::telemetry::attr this file's
    // directive supplies.
    EXPECT_EQ(
        static_cast<void const*>(&ledger_span::attr::currentLedgerHash),
        static_cast<void const*>(&xrpl::telemetry::attr::currentLedgerHash));
    EXPECT_EQ(
        static_cast<void const*>(&ledger_span::attr::currentLedgerSeq),
        static_cast<void const*>(&xrpl::telemetry::attr::currentLedgerSeq));
    // Distinct from each other, and from the ledger.acquire keys they read
    // similarly to: `ledger_hash` is the ledger being fetched, whereas
    // `current_ledger_hash` is the parent of the round doing the asking.
    EXPECT_NE(
        std::string_view(ledger_span::attr::currentLedgerHash),
        std::string_view(ledger_span::attr::currentLedgerSeq));
    EXPECT_NE(
        std::string_view(ledger_span::attr::currentLedgerHash),
        std::string_view(ledger_span::attr::ledgerHash));
    EXPECT_NE(
        std::string_view(ledger_span::attr::currentLedgerSeq),
        std::string_view(ledger_span::attr::ledgerSeq));
}

TEST(LedgerSpanNames, phase_child_span_names_are_fully_composed)
{
    // These are used with childSpan(name, ctx), which takes ONE complete name,
    // so unlike the parent they are pre-joined here. Assert the exact composed
    // strings: they are what the phase-duration panel selects on and what
    // expected_spans.json lists.
    EXPECT_EQ(std::string_view(ledger_span::acquireHeader), "ledger.acquire.header");
    EXPECT_EQ(std::string_view(ledger_span::acquireAsTree), "ledger.acquire.astree");
    EXPECT_EQ(std::string_view(ledger_span::acquireTxTree), "ledger.acquire.txtree");
}

TEST(LedgerSpanNames, phase_child_span_names_are_children_of_the_acquire_name)
{
    // The naming property, not just the spelling: each phase name must extend
    // the parent's "ledger.acquire" exactly, because that shared prefix is what
    // the panel's span_name=~"ledger.acquire..*" selector relies on to pick up
    // all three phases and no other span.
    auto const parent = std::string_view("ledger.acquire");
    for (std::string_view const phase :
         {std::string_view(ledger_span::acquireHeader),
          std::string_view(ledger_span::acquireAsTree),
          std::string_view(ledger_span::acquireTxTree)})
    {
        EXPECT_TRUE(phase.starts_with(parent)) << "phase not under the parent name: " << phase;
        // A '.' immediately after the parent, and a non-empty leaf after that.
        ASSERT_GT(phase.size(), parent.size() + 1);
        EXPECT_EQ(phase[parent.size()], '.');
        EXPECT_FALSE(phase.substr(parent.size() + 1).empty());
        // The leaf must be one segment: a further dot would make the panel's
        // selector pick up a grandchild that does not exist.
        EXPECT_EQ(phase.substr(parent.size() + 1).find('.'), std::string_view::npos);
    }
}

TEST(LedgerSpanNames, phase_child_span_names_are_mutually_distinct)
{
    // Cause, not just state: the duration panel plots one series per phase, so
    // two phases sharing a name would silently merge the account-state tree
    // (nearly all of a fresh sync) into another phase's line.
    EXPECT_NE(
        std::string_view(ledger_span::acquireHeader), std::string_view(ledger_span::acquireAsTree));
    EXPECT_NE(
        std::string_view(ledger_span::acquireHeader), std::string_view(ledger_span::acquireTxTree));
    EXPECT_NE(
        std::string_view(ledger_span::acquireAsTree), std::string_view(ledger_span::acquireTxTree));
}

TEST(LedgerSpanNames, serve_span_name_is_dot_qualified)
{
    EXPECT_EQ(std::string_view(seg::ledger), "ledger");
    EXPECT_EQ(std::string_view(ledger_span::op::serve), "serve");
}

TEST(LedgerSpanNames, b2_attribute_keys_match_collector_and_tempo)
{
    // `timed_out` and `object_type` are the two NEW spanmetrics dimensions
    // listed in BOTH collector configs; the rest are span-only.
    EXPECT_EQ(std::string_view(ledger_span::attr::timedOut), "timed_out");
    EXPECT_EQ(std::string_view(ledger_span::attr::objectType), "object_type");
    // Span-only, asserted by expected_spans.json. txset_hash is additionally a
    // dedicated Parquet span column in tempo.yaml, for the same per-object
    // reason ledger_hash is: it identifies WHICH set stalled, and as a metric
    // dimension it would mint one series per consensus round.
    EXPECT_EQ(std::string_view(ledger_span::attr::txSetHash), "txset_hash");
    EXPECT_EQ(std::string_view(ledger_span::attr::missingNodes), "missing_nodes");
    EXPECT_EQ(std::string_view(ledger_span::attr::servedNodes), "served_nodes");
    EXPECT_EQ(std::string_view(ledger_span::attr::durationMs), "duration_ms");
}

TEST(LedgerSpanNames, b2_attribute_keys_are_bare_underscore_never_dotted)
{
    // The naming spec reserves dotted keys for resource attributes; a dotted
    // span-attribute key fails the CI naming check. Assert the property so a
    // key added to this group later is covered too.
    for (std::string_view const key :
         {std::string_view(ledger_span::attr::timedOut),
          std::string_view(ledger_span::attr::objectType),
          std::string_view(ledger_span::attr::txSetHash),
          std::string_view(ledger_span::attr::missingNodes),
          std::string_view(ledger_span::attr::servedNodes),
          std::string_view(ledger_span::attr::durationMs)})
    {
        EXPECT_EQ(key.find('.'), std::string_view::npos) << "dotted span-attr key: " << key;
        EXPECT_FALSE(key.empty());
    }
}

TEST(LedgerSpanNames, timeout_outcome_value_is_distinct_from_the_other_three)
{
    // `timeout` is a fourth value in the outcome set the collector
    // aggregates. It must be distinct from the other three, because
    // the whole point is separating "peers never supplied the data" from
    // "the data was bad" (failed) and "we stopped waiting" (abandoned).
    EXPECT_EQ(std::string_view(ledger_span::val::timeout), "timeout");
    EXPECT_NE(
        std::string_view(ledger_span::val::timeout), std::string_view(ledger_span::val::failed));
    EXPECT_NE(
        std::string_view(ledger_span::val::timeout), std::string_view(ledger_span::val::complete));
    EXPECT_NE(
        std::string_view(ledger_span::val::timeout), std::string_view(ledger_span::val::abandoned));
}

TEST(LedgerSpanNames, serve_object_type_values_are_the_four_request_kinds)
{
    // These become the `object_type` dimension's value set (cardinality 4),
    // which is what makes it safe as a metric dimension.
    EXPECT_EQ(std::string_view(ledger_span::val::header), "header");
    EXPECT_EQ(std::string_view(ledger_span::val::txTree), "tx");
    EXPECT_EQ(std::string_view(ledger_span::val::asTree), "as");
    EXPECT_EQ(std::string_view(ledger_span::val::txSet), "txset");
}

TEST(LedgerSpanNames, serve_outcome_values_are_the_three_terminal_states)
{
    // `complete` is shared with the acquire outcomes (same concept, told apart
    // by span name); `partial` and `refused` are serve-specific.
    EXPECT_EQ(std::string_view(ledger_span::val::partial), "partial");
    EXPECT_EQ(std::string_view(ledger_span::val::refused), "refused");
    EXPECT_NE(
        std::string_view(ledger_span::val::partial), std::string_view(ledger_span::val::refused));
    EXPECT_NE(
        std::string_view(ledger_span::val::partial), std::string_view(ledger_span::val::complete));
    EXPECT_NE(
        std::string_view(ledger_span::val::refused), std::string_view(ledger_span::val::complete));
}

TEST(LedgerSpanNames, phaseOutcome_normal_completion_is_complete)
{
    // A phase whose tree assembled, or a tx set that arrived: complete_ set,
    // nothing else. Reached from receiveNode()/trigger() for a phase and from
    // done() for a tx set.
    EXPECT_EQ(
        ledger_span::phaseOutcome(/*failed=*/false, /*complete=*/true, /*timedOut=*/false),
        "complete");
}

TEST(LedgerSpanNames, phaseOutcome_bad_data_is_failed)
{
    // A terminal data fault with no timeout: a peer served a tree or set that
    // would not build. This is the case `timeout` must NOT absorb.
    EXPECT_EQ(
        ledger_span::phaseOutcome(/*failed=*/true, /*complete=*/false, /*timedOut=*/false),
        "failed");
}

TEST(LedgerSpanNames, phaseOutcome_exhausted_budget_reports_timeout_not_failed)
{
    // THE assertion this rule exists for, and the one that would regress
    // silently. Both emitters' exhausted-budget path sets timedOut_ AND
    // failed_ -- failed_ is how the TimeoutCounter base stops its timer loop --
    // so if `failed` were checked first, every timeout would be relabelled as a
    // data fault and the "peers are not serving this" signal would vanish
    // exactly when a node is stuck.
    EXPECT_EQ(
        ledger_span::phaseOutcome(/*failed=*/true, /*complete=*/false, /*timedOut=*/true),
        "timeout");
}

TEST(LedgerSpanNames, phaseOutcome_timeout_outranks_a_late_completion)
{
    // Edge case: the budget expired and the data then arrived. It still reports
    // `timeout`, because the retry budget was really spent -- counting it as a
    // success would hide the cost.
    EXPECT_EQ(
        ledger_span::phaseOutcome(/*failed=*/false, /*complete=*/true, /*timedOut=*/true),
        "timeout");
}

TEST(LedgerSpanNames, phaseOutcome_dropped_mid_fetch_is_abandoned)
{
    // No flag at all: the object was destroyed while still fetching (the
    // InboundLedger sweep, or InboundTransactions::newRound dropping a set).
    // Reporting a value here is what keeps a stuck-then-swept unit in the
    // outcome rate instead of vanishing from it.
    EXPECT_EQ(
        ledger_span::phaseOutcome(/*failed=*/false, /*complete=*/false, /*timedOut=*/false),
        "abandoned");
}

TEST(LedgerSpanNames, phaseOutcome_covers_its_whole_input_domain)
{
    // No input combination yields an empty or undeclared value, which is the
    // property that guarantees no exit can end up with a blank outcome and that
    // the spanmetrics dimension can never gain an unexpected fifth value.
    for (bool const failed : {false, true})
    {
        for (bool const complete : {false, true})
        {
            for (bool const timedOut : {false, true})
            {
                auto const outcome = ledger_span::phaseOutcome(failed, complete, timedOut);
                EXPECT_FALSE(outcome.empty())
                    << "failed=" << failed << " complete=" << complete << " timedOut=" << timedOut;
                EXPECT_TRUE(
                    outcome == std::string_view(ledger_span::val::complete) ||
                    outcome == std::string_view(ledger_span::val::failed) ||
                    outcome == std::string_view(ledger_span::val::timeout) ||
                    outcome == std::string_view(ledger_span::val::abandoned))
                    << "undeclared outcome '" << outcome << "' for failed=" << failed
                    << " complete=" << complete << " timedOut=" << timedOut;
                // Whenever the budget expired, the answer is `timeout`
                // regardless of the other two -- the precedence property, not
                // just the four sampled points above.
                // Braced deliberately: EXPECT_EQ expands to an if/else, so an
                // unbraced if around it is a dangling else, which gcc rejects.
                if (timedOut)
                {
                    EXPECT_EQ(outcome, std::string_view(ledger_span::val::timeout));
                }
            }
        }
    }
}

TEST(LedgerSpanNames, phaseOutcome_is_a_compile_time_rule)
{
    // constexpr, so the rule costs nothing at its call sites and the mapping is
    // fixed by the compiler itself.
    static_assert(ledger_span::phaseOutcome(false, true, false) == std::string_view("complete"));
    static_assert(ledger_span::phaseOutcome(true, false, false) == std::string_view("failed"));
    static_assert(ledger_span::phaseOutcome(true, false, true) == std::string_view("timeout"));
    static_assert(ledger_span::phaseOutcome(false, false, false) == std::string_view("abandoned"));
    SUCCEED();
}

TEST(LedgerSpanNames, serveObjectType_maps_every_protobuf_itype)
{
    // The exact protobuf TMLedgerInfoType values, which are fixed by the wire
    // protocol: liBASE=0, liTX_NODE=1, liAS_NODE=2, liTS_CANDIDATE=3. Passed as
    // an int so this rule stays free of protobuf headers and assertable here.
    EXPECT_EQ(ledger_span::serveObjectType(0), "header");
    EXPECT_EQ(ledger_span::serveObjectType(1), "tx");
    EXPECT_EQ(ledger_span::serveObjectType(2), "as");
    EXPECT_EQ(ledger_span::serveObjectType(3), "txset");
}

TEST(LedgerSpanNames, serveObjectType_never_yields_an_undeclared_value)
{
    // Edge case: an out-of-range itype cannot occur -- PeerImp::onMessage
    // rejects the request before the worker runs -- but the rule must still
    // produce a declared value rather than an empty attribute, so the
    // object_type dimension's value set stays closed at four.
    for (int const itype : {-1, 4, 99})
    {
        auto const value = ledger_span::serveObjectType(itype);
        EXPECT_EQ(value, std::string_view(ledger_span::val::header))
            << "unexpected fallback for itype=" << itype;
    }
}

TEST(LedgerSpanNames, serveOutcome_empty_reply_is_refused)
{
    // Seven of the eight exits of processLedgerRequest send nothing, and all of
    // them reach this through a zero node count. Deriving the value from the
    // reply is what makes those seven impossible to mislabel.
    EXPECT_EQ(ledger_span::serveOutcome(/*servedNodes=*/0, /*softCap=*/128), "refused");
}

TEST(LedgerSpanNames, serveOutcome_partial_reply_below_cap_is_complete)
{
    EXPECT_EQ(ledger_span::serveOutcome(/*servedNodes=*/12, /*softCap=*/128), "complete");
    EXPECT_EQ(ledger_span::serveOutcome(/*servedNodes=*/127, /*softCap=*/128), "complete");
}

TEST(LedgerSpanNames, serveOutcome_reply_at_the_cap_is_partial)
{
    // Edge case at the exact boundary: the assembly loop stops here, so the
    // requester must come back for the rest. Counting it as a success would
    // hide the extra round trips a large tree really costs.
    EXPECT_EQ(ledger_span::serveOutcome(/*servedNodes=*/128, /*softCap=*/128), "partial");
    EXPECT_EQ(ledger_span::serveOutcome(/*servedNodes=*/256, /*softCap=*/128), "partial");
}

TEST(LedgerSpanNames, serveOutcome_never_yields_an_undeclared_value)
{
    // Negative counts cannot occur (nodes_size() is non-negative) but must
    // still map to a declared value rather than an empty attribute.
    for (int const served : {-5, 0, 1, 64, 128, 4096})
    {
        auto const outcome = ledger_span::serveOutcome(served, 128);
        EXPECT_TRUE(
            outcome == std::string_view(ledger_span::val::complete) ||
            outcome == std::string_view(ledger_span::val::partial) ||
            outcome == std::string_view(ledger_span::val::refused))
            << "undeclared serve outcome '" << outcome << "' for servedNodes=" << served;
    }
}

TEST(LedgerSpanNames, serveOutcome_is_a_compile_time_rule)
{
    static_assert(ledger_span::serveOutcome(0, 128) == std::string_view("refused"));
    static_assert(ledger_span::serveOutcome(1, 128) == std::string_view("complete"));
    static_assert(ledger_span::serveOutcome(128, 128) == std::string_view("partial"));
    SUCCEED();
}

TEST(LedgerSpanNames, peer_dial_span_name_is_dot_qualified)
{
    // ConnectAttempt::run builds the name as seg::peer + "." + op::dial.
    EXPECT_EQ(std::string_view(seg::peer), "peer");
    EXPECT_EQ(std::string_view(peer_span::op::dial), "dial");
}

TEST(LedgerSpanNames, peer_dial_attribute_keys_are_bare_underscore)
{
    // remote_endpoint is the dedicated Parquet span column in tempo.yaml and
    // is deliberately NOT a spanmetrics dimension: one series per peer address
    // would be unbounded cardinality.
    EXPECT_EQ(std::string_view(peer_span::attr::remoteEndpoint), "remote_endpoint");
    EXPECT_EQ(std::string_view(peer_span::attr::durationMs), "duration_ms");
    EXPECT_EQ(std::string_view(peer_span::attr::outcome), "outcome");
    for (std::string_view const key :
         {std::string_view(peer_span::attr::remoteEndpoint),
          std::string_view(peer_span::attr::durationMs),
          std::string_view(peer_span::attr::outcome)})
    {
        EXPECT_EQ(key.find('.'), std::string_view::npos) << "dotted span-attr key: " << key;
    }
}

TEST(LedgerSpanNames, peer_dial_outcome_values_match_the_counter_label_set)
{
    // These six ARE the values ConnectAttempt::reportOutcome passes to the
    // overlay_connect_total counter -- the span and the counter read the same
    // constants from the same funnel, which is what stops them drifting apart.
    // Pinned literally because the Bootstrap-row dial panel and the runbook
    // both name them.
    EXPECT_EQ(std::string_view(peer_span::val::connected), "connected");
    EXPECT_EQ(std::string_view(peer_span::val::tcpFail), "tcp_fail");
    EXPECT_EQ(std::string_view(peer_span::val::tlsFail), "tls_fail");
    EXPECT_EQ(std::string_view(peer_span::val::upgradeFail), "upgrade_fail");
    EXPECT_EQ(std::string_view(peer_span::val::timeout), "timeout");

    // Reuses the slug handshake_negotiation_fail_total already publishes for the
    // same fault, so one misconfiguration reads identically on both signals.
    EXPECT_EQ(std::string_view(peer_span::val::selfConnection), "self_connection");
}

TEST(LedgerSpanNames, peer_dial_outcome_values_are_mutually_distinct)
{
    // The dial panel splits by this attribute, so two outcomes sharing a value
    // would merge two different failure stages into one line -- and the stage
    // is the whole diagnostic content of the dial signal.
    std::array<std::string_view, 6> const values{
        peer_span::val::connected,
        peer_span::val::tcpFail,
        peer_span::val::tlsFail,
        peer_span::val::selfConnection,
        peer_span::val::upgradeFail,
        peer_span::val::timeout};
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        EXPECT_FALSE(values[i].empty());
        for (std::size_t j = i + 1; j < values.size(); ++j)
            EXPECT_NE(values[i], values[j]) << "duplicate dial outcome at " << i << "," << j;
    }
}

TEST(LedgerSpanNames, b2_inactive_guard_finalize_sequences_are_no_ops)
{
    // Negative / disabled path for all four new spans. A default-constructed
    // SpanGuard is the exact state TransactionAcquire::acquireSpan_,
    // InboundLedger's three phase handles and ConnectAttempt::dialSpan_ hold
    // when telemetry is off or the category is disabled: operator bool() is
    // false and every setter is inert. Drive the full finalize sequence of each
    // emitter -- the same calls, in the same order -- and assert the guard
    // stays inactive and nothing crashes. This is what proves the new spans
    // emit nothing on a node with telemetry disabled, including from a
    // destructor.
    SpanGuard guard;
    ASSERT_FALSE(static_cast<bool>(guard));

    // TransactionAcquire::addRoundRequestEvent(). The emitter checks the guard
    // before converting anything, so on the disabled path this call is never
    // even reached -- driven here anyway to prove the API itself is inert, which
    // is what makes that check an optimisation rather than the correctness
    // barrier.
    guard.addEvent(
        ledger_span::event::roundRequest,
        {{ledger_span::attr::currentLedgerHash, "0123456789ABCDEF"},
         {ledger_span::attr::currentLedgerSeq, "12345"}});

    // TransactionAcquire::finalizeAcquireSpan()
    guard.setAttribute(ledger_span::attr::txSetHash, "0123456789ABCDEF");
    guard.setAttribute(
        ledger_span::attr::outcome,
        ledger_span::phaseOutcome(/*failed=*/false, /*complete=*/false, /*timedOut=*/false));
    guard.setAttribute(ledger_span::attr::timeouts, static_cast<std::int64_t>(21));
    guard.setAttribute(ledger_span::attr::durationMs, static_cast<std::int64_t>(5250));
    guard.setAttribute(ledger_span::attr::peerCount, static_cast<std::int64_t>(0));

    // InboundLedger::beginPhaseSpan() / endPhaseSpan()
    guard.setAttribute(ledger_span::attr::ledgerHash, "FEDCBA9876543210");
    guard.setAttribute(ledger_span::attr::ledgerSeq, static_cast<std::int64_t>(9000));
    guard.setAttribute(ledger_span::attr::timedOut, true);
    guard.setAttribute(ledger_span::attr::missingNodes, static_cast<std::int64_t>(256));

    // PeerImp::processLedgerRequest()'s scope-exit finalizer
    guard.setAttribute(ledger_span::attr::objectType, ledger_span::serveObjectType(/*itype=*/2));
    guard.setAttribute(ledger_span::attr::servedNodes, static_cast<std::int64_t>(0));
    guard.setAttribute(
        ledger_span::attr::outcome, ledger_span::serveOutcome(/*servedNodes=*/0, /*softCap=*/128));

    // ConnectAttempt::reportOutcome()
    guard.setAttribute(peer_span::attr::remoteEndpoint, "10.0.0.5:51235");
    guard.setAttribute(peer_span::attr::outcome, peer_span::val::timeout);
    guard.setAttribute(peer_span::attr::durationMs, static_cast<std::int64_t>(15000));

    // Still inactive: no span was created, so none can be exported.
    EXPECT_FALSE(static_cast<bool>(guard));

    // childSpan on an inactive guard yields another inactive guard, which is
    // what makes InboundLedger::beginPhaseSpan() a no-op when telemetry is off:
    // it never creates a phase span at all, so the whole per-phase feature
    // costs one branch on the disabled path.
    auto const child = guard.childSpan(ledger_span::acquireAsTree);
    EXPECT_FALSE(static_cast<bool>(child));
    // Same via the explicit-parent overload, the one beginPhaseSpan() actually
    // calls: an invalid parent context yields an inactive child.
    auto const childOfCtx = SpanGuard::childSpan(ledger_span::acquireTxTree, guard.spanContext());
    EXPECT_FALSE(static_cast<bool>(childOfCtx));
    EXPECT_FALSE(guard.spanContext().isValid());
}

}  // namespace
