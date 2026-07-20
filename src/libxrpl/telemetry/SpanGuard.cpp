/**
 * Pimpl implementation for SpanGuard and SpanContext.
 *
 * All OpenTelemetry SDK types are confined to this translation unit.
 * The public SpanGuard.h header contains only standard-library types
 * and forward-declares the Impl struct.
 *
 * Static factory methods access the global Telemetry instance via
 * Telemetry::getInstance(), check whether the requested TraceCategory
 * is enabled, and return either an active guard with a real Span+Scope
 * or a null guard whose methods are all no-ops.
 *
 * The Impl struct holds the OTel Span (shared_ptr) and an optional
 * Scope. Scope is non-movable, but since Impl lives behind a
 * unique_ptr, SpanGuard's move constructor simply transfers the
 * pointer — no double-Scope issues. A scoped guard holds the Scope;
 * detached() produces an Impl with no Scope (nullopt) so the guard
 * carries no thread-local binding and is safe to move across threads.
 *
 * @see SpanGuard (SpanGuard.h), Telemetry (Telemetry.h),
 * FilteringSpanProcessor (Telemetry.cpp)
 */

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpl/telemetry/SpanGuard.h>

#include <xrpl/telemetry/DiscardFlag.h>
#include <xrpl/telemetry/Telemetry.h>

#include <opentelemetry/context/context.h>
#include <opentelemetry/context/runtime_context.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/semconv/exception_attributes.h>
#include <opentelemetry/trace/context.h>
#include <opentelemetry/trace/scope.h>
#include <opentelemetry/trace/span.h>
#include <opentelemetry/trace/span_metadata.h>
#include <opentelemetry/trace/span_startoptions.h>

#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>

namespace xrpl::telemetry {

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
    /**
     * The OTel span being guarded. Set to nullptr after discard() or
     * once detached() moves it into a new guard, so ~Impl skips End().
     */
    opentelemetry::nostd::shared_ptr<otel_trace::Span> span;

    /**
     * Scope that activates span on the current thread's context stack.
     * nullopt for a detached guard, which holds the span with no
     * thread-local binding and is therefore safe to move across threads.
     */
    std::optional<otel_trace::Scope> scope;

    /**
     * Construct a scoped guard: the span is pushed onto this thread's
     * active-context stack for the lifetime of the guard.
     * @param s The span to guard.
     */
    explicit Impl(opentelemetry::nostd::shared_ptr<otel_trace::Span> s) : span(std::move(s))
    {
        scope.emplace(span);
    }

    /**
     * Tag type selecting the scope-less (detached) constructor.
     */
    struct Detached
    {
    };

    /**
     * Construct a detached guard: the span is held with NO thread-local
     * Scope, so the guard carries no context-stack binding and is safe
     * to move to and destroy on another thread.
     * @param s The span to guard.
     */
    Impl(opentelemetry::nostd::shared_ptr<otel_trace::Span> s, Detached) : span(std::move(s))
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

/**
 * Check whether the given TraceCategory is enabled on the Telemetry instance.
 * @return true if the category's shouldTrace*() flag is on.
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

namespace {

// Span-link attribute marking a "follows-from" (causal, non-parent) link,
// emitted by both linkedSpan() overloads. Custom xrpl attribute — not part
// of the OTel semantic conventions, so defined here rather than pulled from
// <opentelemetry/semconv/...>.
constexpr char const* kLinkTypeKey = "link_type";
constexpr char const* kLinkTypeFollowsFrom = "follows_from";

// Map a TraceCategory to an OTel SpanKind so Tempo's service-graph /
// RED metrics see the correct direction. RPC spans are emitted at the
// server entry point (handler dispatch), Peer spans at inbound-message
// receipt. Transactions / Consensus / Ledger are internal processing
// and keep the default kInternal.
otel_trace::SpanKind
categoryToSpanKind(TraceCategory cat)
{
    switch (cat)
    {
        case TraceCategory::Rpc:
            return otel_trace::SpanKind::kServer;
        case TraceCategory::Peer:
            return otel_trace::SpanKind::kConsumer;
        case TraceCategory::Transactions:
        case TraceCategory::Consensus:
        case TraceCategory::Ledger:
            return otel_trace::SpanKind::kInternal;
    }
    return otel_trace::SpanKind::kInternal;  // unreachable
}

}  // namespace

SpanGuard
SpanGuard::span(TraceCategory cat, std::string_view prefix, std::string_view name)
{
    auto* tel = Telemetry::getInstance();
    if ((tel == nullptr) || !tel->isEnabled() || !isCategoryEnabled(*tel, cat))
        return {};
    std::string fullName;
    fullName.reserve(prefix.size() + 1 + name.size());
    fullName.append(prefix).append(1, '.').append(name);
    return SpanGuard(std::make_unique<Impl>(tel->startSpan(fullName, categoryToSpanKind(cat))));
}

SpanGuard
SpanGuard::rootSpan(TraceCategory cat, std::string_view prefix, std::string_view name)
{
    auto* tel = Telemetry::getInstance();
    if ((tel == nullptr) || !tel->isEnabled() || !isCategoryEnabled(*tel, cat))
        return {};
    std::string fullName;
    fullName.reserve(prefix.size() + 1 + name.size());
    fullName.append(prefix).append(1, '.').append(name);
    // Force a fresh trace root: do NOT inherit this thread's active span.
    auto rootCtx = opentelemetry::context::Context{otel_trace::kIsRootSpanKey, true};
    return SpanGuard(
        std::make_unique<Impl>(tel->startSpan(fullName, rootCtx, categoryToSpanKind(cat))));
}

// ===== Child / linked span creation ========================================

SpanGuard
SpanGuard::childSpan(std::string_view name) const
{
    if (!impl_)
        return {};
    auto* tel = Telemetry::getInstance();
    if ((tel == nullptr) || !tel->isEnabled())
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
    if ((tel == nullptr) || !tel->isEnabled())
        return {};
    return SpanGuard(std::make_unique<Impl>(tel->startSpan(name, parentCtx.impl_->ctx)));
}

SpanGuard
SpanGuard::linkedSpan(std::string_view name) const
{
    if (!impl_)
        return {};
    auto* tel = Telemetry::getInstance();
    if ((tel == nullptr) || !tel->isEnabled())
        return {};

    auto tracer = tel->getTracer();
    auto spanCtx = impl_->span->GetContext();

    // Mark as root span so it starts a new trace sub-tree rather than
    // inheriting the current thread's active span as parent.
    otel_trace::StartSpanOptions opts;
    opts.parent = opentelemetry::context::Context{otel_trace::kIsRootSpanKey, true};

    // LCOV_EXCL_START
    return SpanGuard(
        std::make_unique<Impl>(tracer->StartSpan(
            std::string(name), {}, {{spanCtx, {{kLinkTypeKey, kLinkTypeFollowsFrom}}}}, opts)));
    // LCOV_EXCL_STOP
}

SpanGuard
SpanGuard::linkedSpan(std::string_view name, SpanContext const& linkCtx)
{
    if (!linkCtx.isValid())
        return {};
    auto* tel = Telemetry::getInstance();
    if ((tel == nullptr) || !tel->isEnabled())
        return {};

    auto tracer = tel->getTracer();

    // Extract the span from the captured context to get its SpanContext.
    auto linkSpan = otel_trace::GetSpan(linkCtx.impl_->ctx);
    if (!linkSpan || !linkSpan->GetContext().IsValid())
        return {};

    // Mark as root span so it starts a new trace sub-tree rather than
    // inheriting the current thread's active span as parent.
    otel_trace::StartSpanOptions opts;
    opts.parent = opentelemetry::context::Context{otel_trace::kIsRootSpanKey, true};

    // LCOV_EXCL_START
    return SpanGuard(
        std::make_unique<Impl>(tracer->StartSpan(
            std::string(name),
            {},
            {{linkSpan->GetContext(), {{kLinkTypeKey, kLinkTypeFollowsFrom}}}},
            opts)));
    // LCOV_EXCL_STOP
}

SpanGuard
SpanGuard::detached() &&
{
    if (!impl_)
        return {};
    // Take the span out; the old Impl.span is now null so ~Impl won't End().
    auto s = std::move(impl_->span);
    // Resetting the old Impl destroys its Scope HERE, on the origin thread,
    // popping this thread's context stack correctly. The returned guard holds
    // the span with no Scope, so it is safe to move to another thread.
    impl_.reset();
    return SpanGuard(std::make_unique<Impl>(std::move(s), Impl::Detached{}));
}

// ===== Detach-in-place helpers =============================================

void
detachInPlace(std::optional<SpanGuard>& guard)
{
    if (!guard || !*guard)
        return;
    guard.emplace(std::move(*guard).detached());
}

std::shared_ptr<SpanGuard>
detachInPlace(std::shared_ptr<SpanGuard> guard)
{
    if (!guard || !*guard)
        return guard;
    return std::make_shared<SpanGuard>(std::move(*guard).detached());
}

// ===== Context capture =====================================================

SpanContext
SpanGuard::captureContext() const
{
    if (!impl_)
        return {};
    auto ctx = opentelemetry::context::RuntimeContext::GetCurrent();
    return SpanContext(std::make_shared<SpanContext::Impl>(std::move(ctx)));
}

// ===== Attribute setters ===================================================

void
SpanGuard::setAttribute(std::string_view key, std::string_view value)
{
    if (impl_)
    {
        impl_->span->SetAttribute(
            opentelemetry::nostd::string_view(key.data(), key.size()),
            opentelemetry::nostd::string_view(value.data(), value.size()));
    }
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
SpanGuard::recordException(std::exception const& e)
{
    if (!impl_)
        return;
    namespace semconv_exc = opentelemetry::semconv::exception;
    // Event name "exception" and the attribute keys follow the OTel semantic
    // conventions; the keys come from semconv constants rather than literals.
    impl_->span->AddEvent(
        "exception",
        {{semconv_exc::kExceptionType, typeid(e).name()},
         {semconv_exc::kExceptionMessage, std::string(e.what())}});
    impl_->span->SetStatus(otel_trace::StatusCode::kError, e.what());
}

void
SpanGuard::discard()
{
    if (impl_)
    {
        {
            // DiscardScope owns the flag's whole lifetime: it sets the flag,
            // and clears it on scope exit — even if End() were to throw. The
            // SDK invokes FilteringSpanProcessor::OnEnd() synchronously from
            // End() on this thread, so the flag is observed while still set.
            // Today every valid guard wraps a recording span (head sampling is
            // 1.0), so OnEnd() always runs — but scoping set/clear keeps the
            // flag leak-proof if a later phase can hand back a non-recording
            // span (e.g. honoring a non-sampled remote parent during
            // propagation), so it can never spill onto the next span.
            DiscardScope const discardScope;
            impl_->span->End();
        }
        impl_->span = nullptr;  // prevent ~Impl from calling End() again
        impl_.reset();
    }
}

}  // namespace xrpl::telemetry

#endif  // XRPL_ENABLE_TELEMETRY
