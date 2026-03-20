/** No-op implementation of the Telemetry interface.

    Always compiled (regardless of XRPL_ENABLE_TELEMETRY). Provides the
    make_Telemetry() factory when telemetry is compiled out (#ifndef), which
    unconditionally returns a NullTelemetry that does nothing.

    When XRPL_ENABLE_TELEMETRY IS defined, the OTel virtual methods
    (getTracer, startSpan) return noop tracers/spans. The make_Telemetry()
    factory in this file is not used in that case -- Telemetry.cpp provides
    its own factory that can return the real TelemetryImpl.
*/

#include <xrpl/telemetry/Telemetry.h>

#ifdef XRPL_ENABLE_TELEMETRY
#include <opentelemetry/trace/noop.h>
#endif

namespace xrpl {
namespace telemetry {

namespace {

/** No-op Telemetry that returns immediately from every method.

    Used as the sole implementation when XRPL_ENABLE_TELEMETRY is not
    defined, or as a fallback when it is defined but enabled=0.
*/
class NullTelemetry : public Telemetry
{
    /** Retained configuration (unused, kept for diagnostic access). */
    Setup const setup_;

public:
    explicit NullTelemetry(Setup const& setup) : setup_(setup)
    {
    }

    void
    start() override
    {
    }

    void
    stop() override
    {
    }

    bool
    isEnabled() const override
    {
        return false;
    }

    bool
    shouldTraceTransactions() const override
    {
        return false;
    }

    bool
    shouldTraceConsensus() const override
    {
        return false;
    }

    bool
    shouldTraceRpc() const override
    {
        return false;
    }

    bool
    shouldTracePeer() const override
    {
        return false;
    }

    bool
    shouldTraceLedger() const override
    {
        return false;
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

/** Factory used when XRPL_ENABLE_TELEMETRY is not defined.
    Unconditionally returns a NullTelemetry instance.
*/
#ifndef XRPL_ENABLE_TELEMETRY
std::unique_ptr<Telemetry>
make_Telemetry(Telemetry::Setup const& setup, beast::Journal)
{
    return std::make_unique<NullTelemetry>(setup);
}
#endif

}  // namespace telemetry
}  // namespace xrpl
