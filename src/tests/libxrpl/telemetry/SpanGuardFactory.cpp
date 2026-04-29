#include <xrpl/telemetry/SpanGuard.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <exception>
#include <stdexcept>
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

    span.setAttribute("xrpl.rpc.command", "test");
    span.setAttribute("xrpl.rpc.status", "success");
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

TEST(SpanGuardFactory, capture_context_returns_invalid_on_null)
{
    auto span = SpanGuard::span(TraceCategory::Rpc, "rpc", "ctx");
    auto ctx = span.captureContext();
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
