/**
 * Pimpl implementation for SpanGuard, ScopedSpanGuard and SpanContext.
 *
 * All OpenTelemetry SDK types are confined to this translation unit.
 * The public SpanGuard.h header contains only standard-library types
 * and forward-declares the Impl / ScopedImpl structs.
 *
 * Two guards with split responsibilities live here:
 * - SpanGuard::Impl holds ONLY the OTel Span (shared_ptr). It never
 *   touches the thread-local context stack, so a SpanGuard is
 *   thread-free and may be moved to and destroyed on any thread.
 * - ScopedSpanGuard::ScopedImpl holds a SpanGuard plus an optional
 *   Scope. The Scope pushes the span onto the constructing thread's
 *   context stack; member order (guard first, scope second) ensures
 *   the Scope pops BEFORE the span ends on destruction. It is
 *   thread-bound: construct and destroy on the same thread.
 *
 * Static factory methods access the global Telemetry instance via
 * Telemetry::getInstance(), check whether the requested TraceCategory
 * is enabled, and return either an active guard with a real Span or a
 * null guard whose methods are all no-ops.
 *
 * @see SpanGuard, ScopedSpanGuard (SpanGuard.h), Telemetry (Telemetry.h),
 * FilteringSpanProcessor (Telemetry.cpp)
 */

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpl/telemetry/SpanGuard.h>

#include <xrpl/basics/random.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/telemetry/DiscardFlag.h>
#include <xrpl/telemetry/Telemetry.h>

#include <opentelemetry/context/context.h>
#include <opentelemetry/context/runtime_context.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/nostd/span.h>
#include <opentelemetry/semconv/exception_attributes.h>
#include <opentelemetry/trace/context.h>
#include <opentelemetry/trace/default_span.h>
#include <opentelemetry/trace/scope.h>
#include <opentelemetry/trace/span.h>
#include <opentelemetry/trace/span_context.h>
#include <opentelemetry/trace/span_id.h>
#include <opentelemetry/trace/span_metadata.h>
#include <opentelemetry/trace/span_startoptions.h>
#include <opentelemetry/trace/trace_flags.h>
#include <opentelemetry/trace/trace_id.h>

#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
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
     * The OTel span being guarded. Set to nullptr after discard() so
     * ~Impl skips End().
     */
    opentelemetry::nostd::shared_ptr<otel_trace::Span> span;

    /**
     * Construct an unscoped guard: the span is owned but NOT pushed
     * onto any thread's context stack. The guard is thread-free.
     * @param s The span to guard.
     */
    explicit Impl(opentelemetry::nostd::shared_ptr<otel_trace::Span> s) : span(std::move(s))
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
SpanGuard&
SpanGuard::operator=(SpanGuard&&) noexcept = default;

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
SpanGuard::freshRoot(TraceCategory cat, std::string_view prefix, std::string_view name)
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

// ===== Hash-derived span (category-gated) ==================================

SpanGuard
SpanGuard::hashSpan(
    TraceCategory const cat,
    std::string_view const name,
    std::uint8_t const* const hashData,
    std::size_t const hashSize)
{
    if (hashSize < 16)
        return {};
    auto* tel = Telemetry::getInstance();
    if ((tel == nullptr) || !tel->isEnabled() || !isCategoryEnabled(*tel, cat))
        return {};

    otel_trace::TraceId const traceId(
        opentelemetry::nostd::span<std::uint8_t const, 16>(hashData, 16));

    auto const rval = defaultPrng()();
    std::uint8_t spanIdBytes[8];
    std::memcpy(spanIdBytes, &rval, sizeof(spanIdBytes));
    otel_trace::SpanId const spanId(
        opentelemetry::nostd::span<std::uint8_t const, 8>(spanIdBytes, 8));

    otel_trace::SpanContext const syntheticCtx(
        traceId, spanId, otel_trace::TraceFlags(1), /* remote = */ false);

    auto parentCtx = opentelemetry::context::Context{}.SetValue(
        otel_trace::kSpanKey,
        opentelemetry::nostd::shared_ptr<otel_trace::Span>(
            new otel_trace::DefaultSpan(syntheticCtx)));

    return SpanGuard(std::make_unique<Impl>(tel->startSpan(std::string(name), parentCtx)));
}

SpanGuard
SpanGuard::hashSpan(
    TraceCategory const cat,
    std::string_view const name,
    std::uint8_t const* const hashData,
    std::size_t const hashSize,
    std::uint8_t const* const parentSpanId,
    std::size_t const parentSpanSize,
    std::uint8_t const traceFlags)
{
    if (hashSize < 16 || parentSpanSize != 8)
        return {};
    auto* tel = Telemetry::getInstance();
    if ((tel == nullptr) || !tel->isEnabled() || !isCategoryEnabled(*tel, cat))
        return {};

    otel_trace::TraceId const traceId(
        opentelemetry::nostd::span<std::uint8_t const, 16>(hashData, 16));

    otel_trace::SpanId const parentSpan(
        opentelemetry::nostd::span<std::uint8_t const, 8>(parentSpanId, 8));

    otel_trace::SpanContext const combinedCtx(
        traceId,
        parentSpan,
        otel_trace::TraceFlags(traceFlags),
        /* remote = */ true);

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
    return SpanContext(std::make_shared<SpanContext::Impl>(std::move(ctx)));
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

// ===== ScopedSpanGuard::ScopedImpl =========================================

struct ScopedSpanGuard::ScopedImpl
{
    /**
     * The unscoped guard that owns the span. Declared FIRST so it is
     * destroyed AFTER `scope`: the Scope must pop before the span ends.
     */
    SpanGuard guard;

    /**
     * Active OTel Scope binding the span to this thread's context stack.
     * Present while the guard is active; reset() (via operator SpanGuard)
     * pops it eagerly on the origin thread. Declared AFTER `guard` so
     * destruction order pops the scope before the span ends.
     */
    std::optional<otel_trace::Scope> scope;

    /**
     * Thread that constructed this ScopedImpl. The Scope is bound to
     * this thread's context stack, so popping it (via ~Scope or reset())
     * on a different thread corrupts that thread's stack. Checked in the
     * destructor and operator SpanGuard() to turn a silent cross-thread
     * corruption into an assertion failure in debug/test/fuzzing builds.
     */
    std::thread::id owner{std::this_thread::get_id()};

    /**
     * Wrap a SpanGuard and activate its span on this thread. If the
     * guard is active, push its span onto the thread-local context
     * stack; a null guard leaves `scope` empty.
     * @param g The span-owning guard to activate.
     */
    explicit ScopedImpl(SpanGuard g) : guard(std::move(g))
    {
        if (guard)
            scope.emplace(guard.impl_->span);
    }
};

// ===== ScopedSpanGuard core lifecycle ======================================

ScopedSpanGuard::ScopedSpanGuard(SpanGuard&& guard)
    : impl_(std::make_unique<ScopedImpl>(std::move(guard)))
{
}

ScopedSpanGuard::~ScopedSpanGuard()
{
    // A live Scope must be popped on the thread that pushed it; destroying
    // the guard on any other thread corrupts that thread's context stack.
    // A null guard holds no Scope, so the check is skipped.
    XRPL_ASSERT(
        !impl_ || !impl_->scope.has_value() || impl_->owner == std::this_thread::get_id(),
        "xrpl::telemetry::ScopedSpanGuard::~ScopedSpanGuard : destroyed on "
        "constructing thread");
}

// ===== ScopedSpanGuard factory methods =====================================

ScopedSpanGuard::ScopedSpanGuard(TraceCategory cat, std::string_view prefix, std::string_view name)
    : ScopedSpanGuard(SpanGuard::span(cat, prefix, name))
{
}

ScopedSpanGuard
ScopedSpanGuard::freshRoot(TraceCategory cat, std::string_view prefix, std::string_view name)
{
    return ScopedSpanGuard(SpanGuard::freshRoot(cat, prefix, name));
}

ScopedSpanGuard
ScopedSpanGuard::childSpan(std::string_view name) const
{
    return ScopedSpanGuard(impl_->guard.childSpan(name));
}

ScopedSpanGuard
ScopedSpanGuard::childSpan(std::string_view name, SpanContext const& parentCtx)
{
    return ScopedSpanGuard(SpanGuard::childSpan(name, parentCtx));
}

ScopedSpanGuard
ScopedSpanGuard::linkedSpan(std::string_view name) const
{
    return ScopedSpanGuard(impl_->guard.linkedSpan(name));
}

ScopedSpanGuard
ScopedSpanGuard::linkedSpan(std::string_view name, SpanContext const& linkCtx)
{
    return ScopedSpanGuard(SpanGuard::linkedSpan(name, linkCtx));
}

// ===== ScopedSpanGuard handoff bridge ======================================

ScopedSpanGuard::
operator SpanGuard() &&
{
    // The scope must be popped on the thread that pushed it; handing off
    // elsewhere would pop the wrong stack.
    XRPL_ASSERT(
        impl_->owner == std::this_thread::get_id(),
        "xrpl::telemetry::ScopedSpanGuard::operator SpanGuard : handoff on "
        "constructing thread");
    impl_->scope.reset();  // eager pop, on the origin thread
    return std::move(impl_->guard);
}

// ===== ScopedSpanGuard context capture =====================================

SpanContext
ScopedSpanGuard::captureContext() const
{
    return impl_->guard.captureContext();
}

// ===== ScopedSpanGuard forwarding methods ==================================

void
ScopedSpanGuard::setAttribute(std::string_view key, std::string_view value)
{
    impl_->guard.setAttribute(key, value);
}

void
ScopedSpanGuard::setAttribute(std::string_view key, char const* value)
{
    impl_->guard.setAttribute(key, value);
}

void
ScopedSpanGuard::setAttribute(std::string_view key, std::int64_t value)
{
    impl_->guard.setAttribute(key, value);
}

void
ScopedSpanGuard::setAttribute(std::string_view key, double value)
{
    impl_->guard.setAttribute(key, value);
}

void
ScopedSpanGuard::setAttribute(std::string_view key, bool value)
{
    impl_->guard.setAttribute(key, value);
}

void
ScopedSpanGuard::setOk()
{
    impl_->guard.setOk();
}

void
ScopedSpanGuard::setError(std::string_view description)
{
    impl_->guard.setError(description);
}

void
ScopedSpanGuard::addEvent(std::string_view name)
{
    impl_->guard.addEvent(name);
}

void
ScopedSpanGuard::recordException(std::exception const& e)
{
    impl_->guard.recordException(e);
}

void
ScopedSpanGuard::discard()
{
    // A live Scope must be popped on the thread that pushed it; discarding
    // on any other thread corrupts that thread's context stack. A null guard
    // holds no Scope, so the check is skipped.
    XRPL_ASSERT(
        !impl_ || !impl_->scope.has_value() || impl_->owner == std::this_thread::get_id(),
        "xrpl::telemetry::ScopedSpanGuard::discard : discarded on constructing thread");
    // Pop the scope first (on this thread) so no span stays active on the
    // stack, then discard the span via the owned guard.
    impl_->scope.reset();
    impl_->guard.discard();
}

ScopedSpanGuard::
operator bool() const
{
    return impl_ && static_cast<bool>(impl_->guard);
}

}  // namespace xrpl::telemetry

#endif  // XRPL_ENABLE_TELEMETRY
