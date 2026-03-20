#include <xrpld/telemetry/TracingInstrumentation.h>

#include <xrpl/telemetry/Telemetry.h>

#include <gtest/gtest.h>

using namespace xrpl;

TEST(TracingMacros, macros_with_null_telemetry)
{
    telemetry::Telemetry::Setup setup;
    setup.enabled = false;
    beast::Journal::Sink& sink = beast::Journal::getNullSink();
    beast::Journal j(sink);
    auto tel = telemetry::make_Telemetry(setup, j);
    tel->start();

    // Each macro should compile and execute without crashing.
    {
        XRPL_TRACE_RPC(*tel, "rpc.test.command");
        XRPL_TRACE_SET_ATTR("xrpl.rpc.command", "test");
        XRPL_TRACE_SET_ATTR("xrpl.rpc.status", "success");
    }
    {
        XRPL_TRACE_TX(*tel, "tx.test.process");
        XRPL_TRACE_SET_ATTR("xrpl.tx.hash", "abc123");
    }
    {
        XRPL_TRACE_CONSENSUS(*tel, "consensus.test");
        XRPL_TRACE_SET_ATTR("xrpl.consensus.mode", "proposing");
    }
    {
        XRPL_TRACE_PEER(*tel, "peer.test");
    }
    {
        XRPL_TRACE_LEDGER(*tel, "ledger.test");
    }

    tel->stop();
}

TEST(TracingMacros, separate_scopes)
{
    // Multiple macros in separate scopes should not collide on
    // the _xrpl_guard_ variable name.
    telemetry::Telemetry::Setup setup;
    setup.enabled = false;
    beast::Journal::Sink& sink = beast::Journal::getNullSink();
    beast::Journal j(sink);
    auto tel = telemetry::make_Telemetry(setup, j);

    {
        XRPL_TRACE_RPC(*tel, "rpc.outer");
    }
    {
        XRPL_TRACE_TX(*tel, "tx.inner");
    }
    {
        XRPL_TRACE_CONSENSUS(*tel, "consensus.other");
    }
}

TEST(TracingMacros, conditional_guards)
{
    // NullTelemetry returns false for all shouldTrace* methods.
    // XRPL_TRACE_SET_ATTR on an empty guard must be safe.
    telemetry::Telemetry::Setup setup;
    setup.enabled = false;
    beast::Journal::Sink& sink = beast::Journal::getNullSink();
    beast::Journal j(sink);
    auto tel = telemetry::make_Telemetry(setup, j);

    EXPECT_FALSE(tel->shouldTraceRpc());
    EXPECT_FALSE(tel->shouldTraceTransactions());
    EXPECT_FALSE(tel->shouldTraceConsensus());
    EXPECT_FALSE(tel->shouldTracePeer());
    EXPECT_FALSE(tel->shouldTraceLedger());

    {
        XRPL_TRACE_RPC(*tel, "should.not.create");
        XRPL_TRACE_SET_ATTR("key", "value");
    }
}

#ifdef XRPL_ENABLE_TELEMETRY

TEST(TracingMacros, span_guard_raii)
{
    telemetry::Telemetry::Setup setup;
    setup.enabled = false;
    beast::Journal::Sink& sink = beast::Journal::getNullSink();
    beast::Journal j(sink);
    auto tel = telemetry::make_Telemetry(setup, j);

    auto span = tel->startSpan("test.guard");
    {
        telemetry::SpanGuard guard(span);
        guard.setAttribute("key", "value");
        guard.addEvent("test_event");
        guard.setOk();
    }
}

TEST(TracingMacros, span_guard_move)
{
    telemetry::Telemetry::Setup setup;
    setup.enabled = false;
    beast::Journal::Sink& sink = beast::Journal::getNullSink();
    beast::Journal j(sink);
    auto tel = telemetry::make_Telemetry(setup, j);

    auto span = tel->startSpan("test.move");
    std::optional<telemetry::SpanGuard> opt;
    opt.emplace(span);
    EXPECT_TRUE(opt.has_value());
    opt.reset();
}

TEST(TracingMacros, span_guard_exception)
{
    telemetry::Telemetry::Setup setup;
    setup.enabled = false;
    beast::Journal::Sink& sink = beast::Journal::getNullSink();
    beast::Journal j(sink);
    auto tel = telemetry::make_Telemetry(setup, j);

    auto span = tel->startSpan("test.exception");
    {
        telemetry::SpanGuard guard(span);
        try
        {
            throw std::runtime_error("test error");
        }
        catch (std::exception const& e)
        {
            guard.recordException(e);
        }
    }
}

#endif  // XRPL_ENABLE_TELEMETRY
