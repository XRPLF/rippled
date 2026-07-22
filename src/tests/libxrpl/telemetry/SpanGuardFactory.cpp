#include <xrpl/telemetry/SpanGuard.h>
#include <xrpl/telemetry/SpanNames.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
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
    // dotted xrpl.<domain>. form is reserved for resource attributes). The
    // canonical constants live in the xrpld-level *SpanNames.h headers, which a
    // libxrpl test cannot include, so the keys are written as literals here.
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

TEST(SpanGuardFactory, consensus_close_time_attributes)
{
    // Verify the consensus attribute pattern compiles and doesn't crash with a
    // null SpanGuard. Attribute keys/values use the underscore convention; the
    // canonical consensus::span constants are defined in the xrpld-level
    // ConsensusSpanNames.h, which a libxrpl test cannot include, so the keys are
    // written as literals here.
    {
        auto span = telemetry::SpanGuard::span(
            telemetry::TraceCategory::Consensus, telemetry::seg::consensus, "accept.apply");
        span.setAttribute("ledger_seq", static_cast<int64_t>(42));
        span.setAttribute("close_time", static_cast<int64_t>(780000000));
        span.setAttribute("close_time_correct", true);
        span.setAttribute("close_resolution_ms", static_cast<int64_t>(30000));
        span.setAttribute("consensus_state", std::string("finished"));
        span.setAttribute("proposing", true);
        span.setAttribute("round_time_ms", static_cast<int64_t>(3500));
    }
    {
        auto span = telemetry::SpanGuard::span(
            telemetry::TraceCategory::Consensus, telemetry::seg::consensus, "accept.apply");
        span.setAttribute("close_time_correct", false);
        span.setAttribute("consensus_state", std::string("moved_on"));
    }
}
