#include <xrpl/consensus/ConsensusSpanNames.h>
#include <xrpl/telemetry/SpanGuard.h>
#include <xrpl/telemetry/SpanNames.h>
#include <xrpl/telemetry/Telemetry.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string_view>
#include <utility>

using namespace xrpl;
using namespace xrpl::telemetry;

TEST(SpanGuardFactory, null_guard_methods_are_safe)
{
    auto span = SpanGuard::span(TraceCategory::Rpc, "rpc", "nonexistent");
    EXPECT_FALSE(span);

    span.setAttribute("key", "value");
    span.setAttribute("int_key", static_cast<int64_t>(42));
    span.setAttribute("bool_key", true);
    span.setOk();
    span.setError("test");
    span.addEvent("event");
}

TEST(SpanGuardFactory, category_span_returns_null_when_disabled)
{
    auto span = SpanGuard::span(TraceCategory::Rpc, "rpc", "test");
    EXPECT_FALSE(span);

    // Attribute keys use the underscore convention for span attributes (the
    // dotted xrpl.<domain>. form is reserved for resource attributes). These
    // rpc_* constants live in an xrpld-level header, so they are literals here;
    // libxrpl-level headers such as ConsensusSpanNames.h can be included
    // directly, as ConsensusSpanNames.cpp does.
    span.setAttribute("command", "test");
    span.setAttribute("rpc_status", "success");
}

TEST(SpanGuardFactory, child_span_null_when_no_parent)
{
    auto span = SpanGuard::span(TraceCategory::Rpc, "rpc", "parent");
    auto child = span.childSpan("child.test");
    EXPECT_FALSE(child);
}

TEST(SpanGuardFactory, linked_span_null_when_no_context)
{
    auto span = SpanGuard::span(TraceCategory::Rpc, "rpc", "source");
    auto linked = span.linkedSpan("linked.test");
    EXPECT_FALSE(linked);
}

TEST(SpanGuardFactory, span_context_returns_invalid_on_null)
{
    auto span = SpanGuard::span(TraceCategory::Rpc, "rpc", "ctx");
    auto ctx = span.spanContext();
    EXPECT_FALSE(ctx.isValid());
}

TEST(SpanGuardFactory, move_construction_transfers_ownership)
{
    auto span = SpanGuard::span(TraceCategory::Rpc, "rpc", "move");
    auto moved = std::move(span);
    EXPECT_FALSE(span);  // NOLINT(bugprone-use-after-move,hicpp-invalid-access-moved)
    moved.setAttribute("key", "value");
}

TEST(SpanGuardFactory, record_exception_safe_on_null)
{
    auto span = SpanGuard::span(TraceCategory::Rpc, "rpc.command", "test");
    try
    {
        throw std::runtime_error("test error");
    }
    catch (std::exception const& e)
    {
        span.recordException(e);
    }
}

TEST(SpanGuardFactory, discard_safe_on_null)
{
    auto span = SpanGuard::span(TraceCategory::Transactions, "tx", "process");
    span.discard();
    EXPECT_FALSE(span);
}

TEST(SpanGuardFactory, consensus_accept_apply_attributes_are_inert_on_null_guard)
{
    namespace cs = consensus::span;

    // Nothing in this binary starts telemetry, so span() returns a null guard
    // before it even joins the name. Pinning that here says which of the
    // factory's exits produced the null guard the rest of the test relies on.
    ASSERT_EQ(Telemetry::getInstance(), nullptr);

    // The attribute set RCLConsensus::doAccept() writes on consensus.accept.apply,
    // read from the same constants the emitter uses rather than copied as
    // literals. Both close-time outcomes are written below: the values differ,
    // the guard's inertness does not.
    auto applySpan = SpanGuard::span(TraceCategory::Consensus, seg::consensus, cs::op::acceptApply);
    ASSERT_FALSE(applySpan);

    applySpan.setAttribute(cs::attr::ledgerSeq, static_cast<std::int64_t>(42));
    applySpan.setAttribute(cs::attr::closeTimeRippleEpochS, static_cast<std::int64_t>(780000000));
    applySpan.setAttribute(cs::attr::closeTimeCorrect, true);
    applySpan.setAttribute(cs::attr::closeResolutionMs, static_cast<std::int64_t>(30000));
    applySpan.setAttribute(cs::attr::consensusState, std::string_view{cs::val::finished});
    applySpan.setAttribute(cs::attr::proposing, true);
    applySpan.setAttribute(cs::attr::roundTimeMs, static_cast<std::int64_t>(3500));

    // A write cannot activate a guard, so it still holds no span and hands out
    // no propagation bytes for an outgoing message to carry.
    EXPECT_FALSE(applySpan);
    EXPECT_FALSE(applySpan.getTraceBytes().valid);

    // The consensus-failed branch writes the other value over the same two keys,
    // and reaches the same inert guard.
    auto movedOnSpan =
        SpanGuard::span(TraceCategory::Consensus, seg::consensus, cs::op::acceptApply);
    ASSERT_FALSE(movedOnSpan);

    movedOnSpan.setAttribute(cs::attr::closeTimeCorrect, false);
    movedOnSpan.setAttribute(cs::attr::consensusState, std::string_view{cs::val::movedOn});

    EXPECT_FALSE(movedOnSpan);
    EXPECT_FALSE(movedOnSpan.getTraceBytes().valid);
}
