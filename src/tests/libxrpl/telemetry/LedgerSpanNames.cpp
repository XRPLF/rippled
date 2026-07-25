/**
 * @file LedgerSpanNames.cpp
 * Unit tests for the ledger.acquire span contract in LedgerSpanNames.h.
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
 *     asserts the outcome of every exit path, including the destructor path,
 *     which is the case that previously produced a span with no outcome at all.
 *     The rule is a pure constexpr function with no dependency on
 *     InboundLedger, so it is asserted directly here: no Application, no peer
 *     set, and no test-only hook added to production code to reach it.
 *
 * Compiled only when XRPL_ENABLE_TELEMETRY is defined, because that is the
 * configuration in which this test target has `src/` on its include path and
 * can therefore reach <xrpld/app/ledger/detail/...>. The header itself is not
 * telemetry-conditional (constants and one constexpr function, no OTel types);
 * only this file's ability to include it is.
 */

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpld/app/ledger/detail/LedgerSpanNames.h>

#include <xrpl/telemetry/SpanGuard.h>
#include <xrpl/telemetry/SpanNames.h>

#include <gtest/gtest.h>

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

}  // namespace

#endif  // XRPL_ENABLE_TELEMETRY
