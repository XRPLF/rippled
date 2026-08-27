/**
 * No-op implementation of the Telemetry interface.
 *
 * Always compiled (regardless of XRPL_ENABLE_TELEMETRY). Provides the
 * makeTelemetry() factory when telemetry is compiled out (#ifndef), which
 * unconditionally returns a NullTelemetry that does nothing.
 *
 * When XRPL_ENABLE_TELEMETRY IS defined, the OTel virtual methods
 * (getTracer, startSpan) return noop tracers/spans. The makeTelemetry()
 * factory in this file is not used in that case -- Telemetry.cpp provides
 * its own factory that can return the real TelemetryImpl.
 */

#include <xrpl/telemetry/Telemetry.h>

#ifndef XRPL_ENABLE_TELEMETRY
// beast::Journal is named only by the compiled-out makeTelemetry() below, so
// this include belongs to that configuration and would be unused in the other.
#include <xrpl/beast/utility/Journal.h>
#endif

#ifdef XRPL_ENABLE_TELEMETRY
#include <opentelemetry/context/context.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/trace/noop.h>
#include <opentelemetry/trace/span.h>
#include <opentelemetry/trace/span_metadata.h>
#include <opentelemetry/trace/tracer.h>

#include <string_view>
#endif

#include <memory>
#include <string>
#include <utility>

namespace xrpl::telemetry {

namespace {

/**
 * No-op Telemetry that returns immediately from every method.
 *
 * Used as the sole implementation when XRPL_ENABLE_TELEMETRY is not
 * defined, or as a fallback when it is defined but enabled=0.
 */
class NullTelemetry : public Telemetry
{
    /**
     * Retained configuration (unused, kept for diagnostic access).
     */
    Setup const setup_;

public:
    explicit NullTelemetry(Setup setup) : setup_{std::move(setup)}
    {
    }

    void
    start() override
    {
        Telemetry::setInstance(this);
    }

    void
    stop() override
    {
        Telemetry::setInstance(nullptr);
    }

    [[nodiscard]] bool
    isEnabled() const override
    {
        return false;
    }

    [[nodiscard]] bool
    shouldTraceTransactions() const override
    {
        return false;
    }

    [[nodiscard]] bool
    shouldTraceConsensus() const override
    {
        return false;
    }

    [[nodiscard]] bool
    shouldTraceRpc() const override
    {
        return false;
    }

    [[nodiscard]] bool
    shouldTracePeer() const override
    {
        return false;
    }

    [[nodiscard]] bool
    shouldTraceLedger() const override
    {
        return false;
    }

    [[nodiscard]] std::string const&
    getConsensusTraceStrategy() const override
    {
        return setup_.consensusTraceStrategy;
    }

#ifdef XRPL_ENABLE_TELEMETRY
    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer>
    getTracer(std::string_view) override
    {
        static auto noopTracer = opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer>(
            new opentelemetry::trace::NoopTracer());
        return noopTracer;
    }

    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>
    startSpan(std::string_view, opentelemetry::trace::SpanKind) override
    {
        return opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>(
            new opentelemetry::trace::NoopSpan(nullptr));
    }

    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>
    startSpan(
        std::string_view,
        opentelemetry::context::Context const&,
        opentelemetry::trace::SpanKind) override
    {
        return opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>(
            new opentelemetry::trace::NoopSpan(nullptr));
    }
#endif
};

}  // namespace

/**
 * Factory used when XRPL_ENABLE_TELEMETRY is not defined.
 * Unconditionally returns a NullTelemetry instance.
 */
#ifndef XRPL_ENABLE_TELEMETRY
std::unique_ptr<Telemetry>
makeTelemetry(Telemetry::Setup const& setup, beast::Journal)
{
    return std::make_unique<NullTelemetry>(setup);
}
#endif

}  // namespace xrpl::telemetry
