/** Pimpl implementation for SpanGuard and SpanContext.

    All OpenTelemetry SDK types are confined to this translation unit.
    The public SpanGuard.h header contains only standard-library types
    and forward-declares the Impl struct.

    Static factory methods access the global Telemetry instance via
    Telemetry::getInstance(), check whether the requested TraceCategory
    is enabled, and return either an active guard with a real Span+Scope
    or a null guard whose methods are all no-ops.

    The Impl struct holds the OTel Span (shared_ptr) and Scope.
    Scope is non-movable, but since Impl lives behind a unique_ptr,
    SpanGuard's move constructor simply transfers the pointer — no
    double-Scope issues.

    @see SpanGuard (SpanGuard.h), Telemetry (Telemetry.h),
         FilteringSpanProcessor (Telemetry.cpp)
*/

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpl/telemetry/SpanGuard.h>

#include <xrpl/basics/random.h>
#include <xrpl/telemetry/DiscardFlag.h>
#include <xrpl/telemetry/SpanNames.h>
#include <xrpl/telemetry/Telemetry.h>

#include <opentelemetry/common/attribute_value.h>
#include <opentelemetry/context/runtime_context.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/trace/context.h>
#include <opentelemetry/trace/default_span.h>
#include <opentelemetry/trace/provider.h>
#include <opentelemetry/trace/scope.h>
#include <opentelemetry/trace/span.h>
#include <opentelemetry/trace/span_context.h>
#include <opentelemetry/trace/span_startoptions.h>
#include <opentelemetry/trace/trace_flags.h>
#include <opentelemetry/trace/trace_id.h>
#include <opentelemetry/trace/tracer.h>

#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace xrpl {
namespace telemetry {

namespace otel_trace = opentelemetry::trace;

// ===== SpanContext::Impl ===================================================

struct SpanContext::Impl
{
    opentelemetry::context::Context ctx;

    explicit Impl(opentelemetry::context::Context c) : ctx(std::move(c))
    {
    }
};

SpanContext::SpanContext(std::shared_ptr<Impl> impl) : impl_(std::move(impl))
{
}

bool
SpanContext::isValid() const
{
    return impl_ != nullptr;
}

// ===== SpanGuard::Impl ====================================================

struct SpanGuard::Impl
{
    /** The OTel span being guarded. Set to nullptr after discard(). */
    opentelemetry::nostd::shared_ptr<otel_trace::Span> span;

    /** Scope that activates span on the current thread's context stack. */
    otel_trace::Scope scope;

    explicit Impl(opentelemetry::nostd::shared_ptr<otel_trace::Span> s)
        : span(std::move(s)), scope(span)
    {
    }

    ~Impl()
    {
        if (span)
            span->End();
    }

    Impl(Impl const&) = delete;
    Impl&
    operator=(Impl const&) = delete;
    Impl(Impl&&) = delete;
    Impl&
    operator=(Impl&&) = delete;
};

// ===== SpanGuard core lifecycle ============================================

SpanGuard::SpanGuard() = default;
SpanGuard::~SpanGuard() = default;
SpanGuard::SpanGuard(SpanGuard&&) noexcept = default;

SpanGuard::SpanGuard(std::unique_ptr<Impl> impl) : impl_(std::move(impl))
{
}

SpanGuard::
operator bool() const
{
    return impl_ != nullptr;
}

// ===== Static factory methods ==============================================

/** Check whether the given TraceCategory is enabled on the Telemetry instance.
    @return true if the category's shouldTrace*() flag is on.
*/
static bool
isCategoryEnabled(Telemetry const& tel, TraceCategory cat)
{
    switch (cat)
    {
        case TraceCategory::Rpc:
            return tel.shouldTraceRpc();
        case TraceCategory::Transactions:
            return tel.shouldTraceTransactions();
        case TraceCategory::Consensus:
            return tel.shouldTraceConsensus();
        case TraceCategory::Peer:
            return tel.shouldTracePeer();
        case TraceCategory::Ledger:
            return tel.shouldTraceLedger();
    }
    return false;  // unreachable, silences compiler warning
}

SpanGuard
SpanGuard::span(TraceCategory cat, std::string_view prefix, std::string_view name)
{
    auto* tel = Telemetry::getInstance();
    if (!tel || !tel->isEnabled() || !isCategoryEnabled(*tel, cat))
        return {};
    auto fullName = std::string(prefix) + "." + std::string(name);
    return SpanGuard(std::make_unique<Impl>(tel->startSpan(fullName)));
}

// ===== Child / linked span creation ========================================

SpanGuard
SpanGuard::childSpan(std::string_view name) const
{
    if (!impl_)
        return {};
    auto* tel = Telemetry::getInstance();
    if (!tel || !tel->isEnabled())
        return {};
    auto ctx = opentelemetry::context::RuntimeContext::GetCurrent();
    return SpanGuard(std::make_unique<Impl>(tel->startSpan(name, ctx)));
}

SpanGuard
SpanGuard::childSpan(std::string_view name, SpanContext const& parentCtx)
{
    if (!parentCtx.isValid())
        return {};
    auto* tel = Telemetry::getInstance();
    if (!tel || !tel->isEnabled())
        return {};
    return SpanGuard(std::make_unique<Impl>(tel->startSpan(name, parentCtx.impl_->ctx)));
}

SpanGuard
SpanGuard::linkedSpan(std::string_view name) const
{
    if (!impl_)
        return {};
    auto* tel = Telemetry::getInstance();
    if (!tel || !tel->isEnabled())
        return {};

    auto tracer = tel->getTracer("xrpld");
    auto spanCtx = impl_->span->GetContext();

    // Mark as root span so it starts a new trace sub-tree rather than
    // inheriting the current thread's active span as parent.
    otel_trace::StartSpanOptions opts;
    opentelemetry::context::Context rootCtx;
    rootCtx = rootCtx.SetValue(otel_trace::kIsRootSpanKey, true);
    opts.parent = rootCtx;

    return SpanGuard(
        std::make_unique<Impl>(tracer->StartSpan(
            std::string(name),
            {},
            {{spanCtx, {{std::string(attr::linkType), std::string(attr_val::followsFrom)}}}},
            opts)));
}

SpanGuard
SpanGuard::linkedSpan(std::string_view name, SpanContext const& linkCtx)
{
    if (!linkCtx.isValid())
        return {};
    auto* tel = Telemetry::getInstance();
    if (!tel || !tel->isEnabled())
        return {};

    auto tracer = tel->getTracer("xrpld");

    // Extract the span from the captured context to get its SpanContext.
    auto linkSpan = otel_trace::GetSpan(linkCtx.impl_->ctx);
    if (!linkSpan || !linkSpan->GetContext().IsValid())
        return {};

    // Mark as root span so it starts a new trace sub-tree rather than
    // inheriting the current thread's active span as parent.
    otel_trace::StartSpanOptions opts;
    opentelemetry::context::Context rootCtx;
    rootCtx = rootCtx.SetValue(otel_trace::kIsRootSpanKey, true);
    opts.parent = rootCtx;

    return SpanGuard(
        std::make_unique<Impl>(tracer->StartSpan(
            std::string(name),
            {},
            {{linkSpan->GetContext(),
              {{std::string(attr::linkType), std::string(attr_val::followsFrom)}}}},
            opts)));
}

// ===== Hash-derived span (category-gated) ==================================

SpanGuard
SpanGuard::hashSpan(
    TraceCategory cat,
    std::string_view name,
    std::uint8_t const* hashData,
    std::size_t hashSize)
{
    if (hashSize < 16)
        return {};
    auto* tel = Telemetry::getInstance();
    if (!tel || !tel->isEnabled() || !isCategoryEnabled(*tel, cat))
        return {};

    otel_trace::TraceId traceId(opentelemetry::nostd::span<std::uint8_t const, 16>(hashData, 16));

    auto const rval = default_prng()();
    std::uint8_t spanIdBytes[8];
    std::memcpy(spanIdBytes, &rval, sizeof(spanIdBytes));
    otel_trace::SpanId spanId(opentelemetry::nostd::span<std::uint8_t const, 8>(spanIdBytes, 8));

    otel_trace::SpanContext syntheticCtx(
        traceId, spanId, otel_trace::TraceFlags(1), /* remote = */ false);

    auto parentCtx = opentelemetry::context::Context{}.SetValue(
        otel_trace::kSpanKey,
        opentelemetry::nostd::shared_ptr<otel_trace::Span>(
            new otel_trace::DefaultSpan(syntheticCtx)));

    return SpanGuard(std::make_unique<Impl>(tel->startSpan(std::string(name), parentCtx)));
}

SpanGuard
SpanGuard::hashSpan(
    TraceCategory cat,
    std::string_view name,
    std::uint8_t const* hashData,
    std::size_t hashSize,
    std::uint8_t const* parentSpanId,
    std::size_t parentSpanSize,
    std::uint8_t traceFlags)
{
    if (hashSize < 16 || parentSpanSize != 8)
        return {};
    auto* tel = Telemetry::getInstance();
    if (!tel || !tel->isEnabled() || !isCategoryEnabled(*tel, cat))
        return {};

    otel_trace::TraceId traceId(opentelemetry::nostd::span<std::uint8_t const, 16>(hashData, 16));

    otel_trace::SpanId parentSpan(
        opentelemetry::nostd::span<std::uint8_t const, 8>(parentSpanId, 8));

    otel_trace::SpanContext combinedCtx(
        traceId, parentSpan, otel_trace::TraceFlags(traceFlags), /* remote = */ true);

    auto parentCtx = opentelemetry::context::Context{}.SetValue(
        otel_trace::kSpanKey,
        opentelemetry::nostd::shared_ptr<otel_trace::Span>(
            new otel_trace::DefaultSpan(combinedCtx)));

    return SpanGuard(std::make_unique<Impl>(tel->startSpan(std::string(name), parentCtx)));
}

// ===== Context capture =====================================================

SpanContext
SpanGuard::captureContext() const
{
    if (!impl_)
        return {};
    auto ctx = opentelemetry::context::RuntimeContext::GetCurrent();
    return SpanContext(std::make_shared<SpanContext::Impl>(ctx));
}

TraceBytes
SpanGuard::getTraceBytes() const
{
    if (!impl_ || !impl_->span)
        return {};

    auto const& spanCtx = impl_->span->GetContext();
    if (!spanCtx.IsValid())
        return {};

    TraceBytes result;
    auto const& tid = spanCtx.trace_id();
    std::memcpy(result.traceId.data(), tid.Id().data(), 16);
    auto const& sid = spanCtx.span_id();
    std::memcpy(result.spanId.data(), sid.Id().data(), 8);
    result.traceFlags = spanCtx.trace_flags().flags();
    result.valid = true;
    return result;
}

// ===== Attribute setters ===================================================

void
SpanGuard::setAttribute(std::string_view key, std::string_view value)
{
    if (impl_)
        impl_->span->SetAttribute(
            opentelemetry::nostd::string_view(key.data(), key.size()),
            opentelemetry::nostd::string_view(value.data(), value.size()));
}

void
SpanGuard::setAttribute(std::string_view key, char const* value)
{
    setAttribute(key, std::string_view(value));
}

void
SpanGuard::setAttribute(std::string_view key, std::int64_t value)
{
    if (impl_)
        impl_->span->SetAttribute(opentelemetry::nostd::string_view(key.data(), key.size()), value);
}

void
SpanGuard::setAttribute(std::string_view key, double value)
{
    if (impl_)
        impl_->span->SetAttribute(opentelemetry::nostd::string_view(key.data(), key.size()), value);
}

void
SpanGuard::setAttribute(std::string_view key, bool value)
{
    if (impl_)
        impl_->span->SetAttribute(opentelemetry::nostd::string_view(key.data(), key.size()), value);
}

// ===== Status / events =====================================================

void
SpanGuard::setOk()
{
    if (impl_)
        impl_->span->SetStatus(otel_trace::StatusCode::kOk);
}

void
SpanGuard::setError(std::string_view description)
{
    if (impl_)
        impl_->span->SetStatus(otel_trace::StatusCode::kError, std::string(description));
}

void
SpanGuard::addEvent(std::string_view name)
{
    if (impl_)
        impl_->span->AddEvent(std::string(name));
}

void
SpanGuard::addEvent(std::string_view name, std::initializer_list<EventAttribute> attrs)
{
    if (!impl_)
        return;
    std::vector<std::pair<std::string_view, opentelemetry::common::AttributeValue>> otelAttrs;
    otelAttrs.reserve(attrs.size());
    for (auto const& [k, v] : attrs)
        otelAttrs.emplace_back(k, opentelemetry::common::AttributeValue{v});
    impl_->span->AddEvent(std::string(name), otelAttrs);
}

void
SpanGuard::recordException(std::exception const& e)
{
    if (!impl_)
        return;
    impl_->span->AddEvent(
        "exception",
        {{"exception.type", "std::exception"}, {"exception.message", std::string(e.what())}});
    impl_->span->SetStatus(otel_trace::StatusCode::kError, e.what());
}

void
SpanGuard::discard()
{
    if (impl_)
    {
        tl_discardCurrentSpan = true;
        impl_->span->End();
        impl_->span = nullptr;  // prevent ~Impl from calling End() again
        impl_.reset();
    }
}

}  // namespace telemetry
}  // namespace xrpl

#endif  // XRPL_ENABLE_TELEMETRY
