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
 *   Scope. The Scope pushes the span onto the constructing context
 *   store's stack; member order (guard first, scope second) ensures
 *   the Scope pops BEFORE the span ends on destruction. It is
 *   store-bound: construct and destroy while the same LocalValue
 *   context store is active (the same thread, or the same JobQueue
 *   coroutine even if it resumes on another worker).
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

#include <xrpl/basics/LocalValue.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/telemetry/DeterministicIdGenerator.h>
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

#include <array>
#include <cstdint>
#include <cstring>
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
SpanContext::isValid() const noexcept
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

SpanGuard::SpanGuard(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl))
{
}

SpanGuard::
operator bool() const noexcept
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
SpanGuard::span(TraceCategory cat, std::string_view prefix, std::string_view name) noexcept
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
SpanGuard::freshRoot(TraceCategory cat, std::string_view prefix, std::string_view name) noexcept
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
SpanGuard::childSpan(std::string_view name) const noexcept
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
SpanGuard::childSpan(std::string_view name, SpanContext const& parentCtx) noexcept
{
    if (!parentCtx.isValid())
        return {};
    auto* tel = Telemetry::getInstance();
    if ((tel == nullptr) || !tel->isEnabled())
        return {};
    return SpanGuard(std::make_unique<Impl>(tel->startSpan(name, parentCtx.impl_->ctx)));
}

SpanGuard
SpanGuard::linkedSpan(std::string_view name) const noexcept
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
SpanGuard::linkedSpan(std::string_view name, SpanContext const& linkCtx) noexcept
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

// Standalone hash-derived span: a TRUE root whose trace_id == hashData[0:16].
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

    // Force the deterministic trace_id via the DeterministicIdGenerator, on the
    // SDK root branch, so the span is a TRUE root (empty parent_span_id) whose
    // trace_id == hashData[0:16]. The kIsRootSpanKey context forces the SDK's
    // no-valid-parent branch, where GenerateTraceId() is called and returns the
    // pending id. The PendingTraceId guard scopes that id to this one startSpan.
    std::array<std::uint8_t, 16> tid{};
    std::memcpy(tid.data(), hashData, 16);
    auto rootCtx = opentelemetry::context::Context{otel_trace::kIsRootSpanKey, true};
    PendingTraceId const pending{tid};
    return SpanGuard(
        std::make_unique<Impl>(
            tel->startSpan(std::string(name), rootCtx, categoryToSpanKind(cat))));
}

// Cross-node hash-derived span: a CHILD of the sender's real remote span
// (remote=true), not a root — so it must NOT use PendingTraceId / rootCtx.
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
SpanGuard::spanContext() const noexcept
{
    if (!impl_ || !impl_->span)
        return {};
    // Refer to THIS guard's own span, not the thread's ambient span. A
    // SpanGuard is unscoped (never pushed onto the thread-local stack), so
    // RuntimeContext::GetCurrent() would return an unrelated ambient span.
    // Building the context from impl_->span makes spanContext() correct
    // for every caller regardless of scoping, and lets childSpan(name, ctx)
    // parent explicitly to this span across threads.
    auto ctx = opentelemetry::context::Context{}.SetValue(otel_trace::kSpanKey, impl_->span);
    return SpanContext(std::make_shared<SpanContext::Impl>(std::move(ctx)));
}

SpanContext
SpanGuard::threadLocalContext() noexcept
{
    // Snapshot whatever span is currently active on THIS thread's context
    // stack (the ambient context), independent of any guard. This is the
    // OTel "capture current context" operation used for propagation.
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
SpanGuard::setAttribute(std::string_view key, std::string_view value) noexcept
{
    if (impl_)
    {
        impl_->span->SetAttribute(
            opentelemetry::nostd::string_view(key.data(), key.size()),
            opentelemetry::nostd::string_view(value.data(), value.size()));
    }
}

void
SpanGuard::setAttribute(std::string_view key, char const* value) noexcept
{
    setAttribute(key, std::string_view(value));
}

void
SpanGuard::setAttribute(std::string_view key, std::int64_t value) noexcept
{
    if (impl_)
        impl_->span->SetAttribute(opentelemetry::nostd::string_view(key.data(), key.size()), value);
}

void
SpanGuard::setAttribute(std::string_view key, double value) noexcept
{
    if (impl_)
        impl_->span->SetAttribute(opentelemetry::nostd::string_view(key.data(), key.size()), value);
}

void
SpanGuard::setAttribute(std::string_view key, bool value) noexcept
{
    if (impl_)
        impl_->span->SetAttribute(opentelemetry::nostd::string_view(key.data(), key.size()), value);
}

// ===== Status / events =====================================================

void
SpanGuard::setOk() noexcept
{
    if (impl_)
        impl_->span->SetStatus(otel_trace::StatusCode::kOk);
}

void
SpanGuard::setError(std::string_view description) noexcept
{
    if (impl_)
        impl_->span->SetStatus(otel_trace::StatusCode::kError, std::string(description));
}

void
SpanGuard::addEvent(std::string_view name) noexcept
{
    if (impl_)
        impl_->span->AddEvent(std::string(name));
}

void
SpanGuard::recordException(std::exception const& e) noexcept
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
SpanGuard::discard() noexcept
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
     * Active OTel Scope binding the span to the active context store's
     * stack. Present while the guard is active; reset() (via operator
     * SpanGuard) pops it eagerly under the constructing store. Declared
     * AFTER `guard` so destruction order pops the scope before the span
     * ends.
     */
    std::optional<otel_trace::Scope> scope;

    /**
     * Identity of the LocalValue store the Scope was pushed onto (the
     * coroutine's store on a coro, else the thread's own store). Popping
     * the Scope (via ~Scope or reset()) must happen while the SAME store
     * is active, else a different stack is corrupted. Coroutine-aware
     * storage lets a coro legitimately resume on another worker while
     * keeping the same store, so store-identity — not thread-id — is the
     * correct invariant. Checked in the destructor and operator
     * SpanGuard() to turn a silent cross-store pop into an assertion
     * failure in debug/test/fuzzing builds.
     *
     * Assigned in the constructor body AFTER the Scope push, not as a
     * member initializer: the push is the first LocalValue touch on a
     * fresh thread for a root or explicit-parent span (the OTel SDK skips
     * the ambient-context read in those cases), so it materializes the
     * store. Capturing before the push would record nullptr while the
     * destructor sees the now-materialized store. For a null guard no
     * Scope is pushed and the assertions short-circuit, so this holds
     * whatever store is active then.
     */
    void const* owner = nullptr;

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
        // Capture after the push so `owner` names the store the Scope
        // actually lives on, even when the push materialized it.
        owner = detail::getLocalValues().get();
    }
};

// ===== ScopedSpanGuard core lifecycle ======================================

ScopedSpanGuard::ScopedSpanGuard(SpanGuard&& guard) noexcept
    : impl_(std::make_unique<ScopedImpl>(std::move(guard)))
{
}

ScopedSpanGuard::~ScopedSpanGuard()
{
    // A live Scope must be popped while its constructing context store is
    // active; destroying the guard under a different store corrupts that
    // store's context stack. A null guard holds no Scope, so the check is
    // skipped.
    XRPL_ASSERT(
        !impl_ || !impl_->scope.has_value() || impl_->owner == detail::getLocalValues().get(),
        "xrpl::telemetry::ScopedSpanGuard::~ScopedSpanGuard : destroyed on the "
        "constructing context store");
}

// ===== ScopedSpanGuard factory methods =====================================

ScopedSpanGuard::ScopedSpanGuard(
    TraceCategory cat,
    std::string_view prefix,
    std::string_view name) noexcept
    : ScopedSpanGuard(SpanGuard::span(cat, prefix, name))
{
}

ScopedSpanGuard
ScopedSpanGuard::freshRoot(
    TraceCategory cat,
    std::string_view prefix,
    std::string_view name) noexcept
{
    return ScopedSpanGuard(SpanGuard::freshRoot(cat, prefix, name));
}

ScopedSpanGuard
ScopedSpanGuard::childSpan(std::string_view name) const noexcept
{
    return ScopedSpanGuard(impl_->guard.childSpan(name));
}

ScopedSpanGuard
ScopedSpanGuard::childSpan(std::string_view name, SpanContext const& parentCtx) noexcept
{
    return ScopedSpanGuard(SpanGuard::childSpan(name, parentCtx));
}

ScopedSpanGuard
ScopedSpanGuard::linkedSpan(std::string_view name) const noexcept
{
    return ScopedSpanGuard(impl_->guard.linkedSpan(name));
}

ScopedSpanGuard
ScopedSpanGuard::linkedSpan(std::string_view name, SpanContext const& linkCtx) noexcept
{
    return ScopedSpanGuard(SpanGuard::linkedSpan(name, linkCtx));
}

// ===== ScopedSpanGuard handoff bridge ======================================

ScopedSpanGuard::
operator SpanGuard() && noexcept
{
    // The scope must be popped while its constructing context store is
    // active; handing off under a different store would pop the wrong stack.
    // A null guard holds no Scope, so the check is skipped.
    XRPL_ASSERT(
        !impl_->scope.has_value() || impl_->owner == detail::getLocalValues().get(),
        "xrpl::telemetry::ScopedSpanGuard::operator SpanGuard : handoff on the "
        "constructing context store");
    impl_->scope.reset();  // eager pop, on the origin store
    return std::move(impl_->guard);
}

// ===== ScopedSpanGuard context capture =====================================

SpanContext
ScopedSpanGuard::spanContext() const noexcept
{
    return impl_->guard.spanContext();
}

// ===== ScopedSpanGuard forwarding methods ==================================

void
ScopedSpanGuard::setAttribute(std::string_view key, std::string_view value) noexcept
{
    impl_->guard.setAttribute(key, value);
}

void
ScopedSpanGuard::setAttribute(std::string_view key, char const* value) noexcept
{
    impl_->guard.setAttribute(key, value);
}

void
ScopedSpanGuard::setAttribute(std::string_view key, std::int64_t value) noexcept
{
    impl_->guard.setAttribute(key, value);
}

void
ScopedSpanGuard::setAttribute(std::string_view key, double value) noexcept
{
    impl_->guard.setAttribute(key, value);
}

void
ScopedSpanGuard::setAttribute(std::string_view key, bool value) noexcept
{
    impl_->guard.setAttribute(key, value);
}

void
ScopedSpanGuard::setOk() noexcept
{
    impl_->guard.setOk();
}

void
ScopedSpanGuard::setError(std::string_view description) noexcept
{
    impl_->guard.setError(description);
}

void
ScopedSpanGuard::addEvent(std::string_view name) noexcept
{
    impl_->guard.addEvent(name);
}

void
ScopedSpanGuard::recordException(std::exception const& e) noexcept
{
    impl_->guard.recordException(e);
}

void
ScopedSpanGuard::discard() noexcept
{
    // A live Scope must be popped while its constructing context store is
    // active; discarding under a different store corrupts that store's
    // context stack. A null guard holds no Scope, so the check is skipped.
    XRPL_ASSERT(
        !impl_ || !impl_->scope.has_value() || impl_->owner == detail::getLocalValues().get(),
        "xrpl::telemetry::ScopedSpanGuard::discard : discarded on the "
        "constructing context store");
    // Pop the scope first (under the constructing store) so no span stays
    // active on the stack, then discard the span via the owned guard.
    impl_->scope.reset();
    impl_->guard.discard();
}

ScopedSpanGuard::
operator bool() const noexcept
{
    return impl_ && static_cast<bool>(impl_->guard);
}

// ===== ScopedActivation ====================================================

struct ScopedActivation::Impl
{
    /**
     * Active OTel Scope binding an externally-owned span to the current
     * context store. Popped on destruction; never ends the span.
     */
    otel_trace::Scope scope;

    /**
     * Identity of the LocalValue store the Scope was pushed onto. The Scope
     * must pop while the SAME store is active (see
     * ScopedSpanGuard::ScopedImpl::owner). Initialized AFTER `scope` in the
     * member init list: members initialize in declaration order, so the
     * Scope's push (the first LocalValue touch on a fresh worker, which
     * materializes the store) runs first, then this captures the
     * now-materialized store. Capturing before the push would record
     * nullptr while the destructor sees the materialized store.
     */
    void const* owner;

    /**
     * Push an externally-owned span onto the current context store.
     * @param span The already-owned span to activate (not owned here).
     */
    explicit Impl(opentelemetry::nostd::shared_ptr<otel_trace::Span> const& span)
        : scope(span), owner(detail::getLocalValues().get())
    {
    }
};

ScopedActivation::ScopedActivation() = default;

ScopedActivation::ScopedActivation(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl))
{
}

ScopedActivation::~ScopedActivation()
{
    // A live Scope must be popped while its constructing context store is
    // active; destroying the activation under a different store corrupts
    // that store's context stack. A null activation holds no Scope, so the
    // check is skipped.
    XRPL_ASSERT(
        !impl_ || impl_->owner == detail::getLocalValues().get(),
        "xrpl::telemetry::ScopedActivation::~ScopedActivation : destroyed on the "
        "constructing context store");
}

ScopedActivation
SpanGuard::activate() const noexcept
{
    if (!impl_ || !impl_->span)
        return {};
    return ScopedActivation(std::make_unique<ScopedActivation::Impl>(impl_->span));
}

}  // namespace xrpl::telemetry

#endif  // XRPL_ENABLE_TELEMETRY
