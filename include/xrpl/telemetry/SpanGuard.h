#pragma once

/** RAII guard for OpenTelemetry trace spans.

    Wraps an OTel Span and Scope together. On construction, the span is
    activated on the current thread's context (via Scope). On destruction,
    the span is ended and the previous context is restored.

    Dependency diagram:

        +------------------------------------+
        |            SpanGuard               |
        +------------------------------------+
        | - span_  : shared_ptr<Span>        |
        | - scope_ : Scope                   |
        +------------------------------------+
                        |  uses
                +-------+-------+
                |               |
           +--------+   +-------------+
           |  Span  |   |    Scope    |
           | (OTel) |   | (OTel, non- |
           |        |   |  movable)   |
           +--------+   +-------------+

    Used by the XRPL_TRACE_* macros in TracingInstrumentation.h. Can also
    be stored in std::optional for conditional tracing (move-constructible).

    Only compiled when XRPL_ENABLE_TELEMETRY is defined.

    Usage examples:

    1. Basic RAII tracing:
    @code
        {
            SpanGuard guard(telemetry.startSpan("rpc.command.submit"));
            guard.setAttribute("xrpl.rpc.command", "submit");
            // ... span is active on this thread's context
        }  // span ended, previous context restored
    @endcode

    2. Conditional tracing with std::optional:
    @code
        std::optional<SpanGuard> guard;
        if (telemetry.isEnabled() && telemetry.shouldTraceRpc())
            guard.emplace(telemetry.startSpan("rpc.request"));
        // ... guard may or may not hold a span
    @endcode

    3. Error recording:
    @code
        SpanGuard guard(telemetry.startSpan("rpc.command.submit"));
        try {
            // ... do work
            guard.setOk();
        } catch (std::exception const& e) {
            guard.recordException(e);  // sets status to error
        }
    @endcode

    @note Thread safety: A SpanGuard must only be used on the thread where
    it was constructed (the Scope binds to the thread-local context stack).
    Use context() to propagate the trace to other threads.

    @note Limitation: Move assignment is deleted because re-scoping a span
    mid-flight would corrupt the context stack. Only move construction is
    supported (for std::optional emplacement).
*/

#ifdef XRPL_ENABLE_TELEMETRY

#include <opentelemetry/context/runtime_context.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/trace/scope.h>
#include <opentelemetry/trace/span.h>

#include <exception>
#include <string_view>

namespace xrpl {
namespace telemetry {

/** RAII wrapper that activates a span on construction and ends it on
    destruction. Non-copyable but move-constructible so it can be held
    in std::optional for conditional tracing.
*/
class SpanGuard
{
    /** The OTel span being guarded. Set to nullptr after move. */
    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span> span_;

    /** Scope that activates span_ on the current thread's context stack. */
    opentelemetry::trace::Scope scope_;

public:
    /** Construct a guard that activates @p span on the current context.

        @param span  The span to guard. Ended in the destructor.
    */
    explicit SpanGuard(opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span> span)
        : span_(std::move(span)), scope_(span_)
    {
    }

    /** Non-copyable. Move-constructible to support std::optional.

        The move constructor creates a new Scope from the transferred span,
        because Scope is not movable.
    */
    SpanGuard(SpanGuard const&) = delete;
    SpanGuard&
    operator=(SpanGuard const&) = delete;
    SpanGuard(SpanGuard&& other) noexcept : span_(std::move(other.span_)), scope_(span_)
    {
        other.span_ = nullptr;
    }
    SpanGuard&
    operator=(SpanGuard&&) = delete;

    ~SpanGuard()
    {
        if (span_)
            span_->End();
    }

    /** @return A mutable reference to the underlying span. */
    opentelemetry::trace::Span&
    span()
    {
        return *span_;
    }

    /** @return A const reference to the underlying span. */
    opentelemetry::trace::Span const&
    span() const
    {
        return *span_;
    }

    /** Mark the span status as OK. */
    void
    setOk()
    {
        span_->SetStatus(opentelemetry::trace::StatusCode::kOk);
    }

    /** Set an explicit status code on the span.

        @param code         The OTel status code.
        @param description  Optional human-readable status description.
    */
    void
    setStatus(opentelemetry::trace::StatusCode code, std::string_view description = "")
    {
        span_->SetStatus(code, std::string(description));
    }

    /** Set a key-value attribute on the span.

        @param key    Attribute name (e.g. "xrpl.rpc.command").
        @param value  Attribute value (string, int, bool, etc.).
    */
    template <typename T>
    void
    setAttribute(std::string_view key, T&& value)
    {
        span_->SetAttribute(
            opentelemetry::nostd::string_view(key.data(), key.size()), std::forward<T>(value));
    }

    /** Add a named event to the span's timeline.

        @param name  Event name.
    */
    void
    addEvent(std::string_view name)
    {
        span_->AddEvent(std::string(name));
    }

    /** Record an exception as a span event following OTel semantic
        conventions, and mark the span status as error.

        @param e  The exception to record.
    */
    void
    recordException(std::exception const& e)
    {
        span_->AddEvent(
            "exception",
            {{"exception.type", "std::exception"}, {"exception.message", std::string(e.what())}});
        span_->SetStatus(opentelemetry::trace::StatusCode::kError, e.what());
    }

    /** Return the current OTel context.

        Useful for creating child spans on a different thread by passing
        this context to Telemetry::startSpan(name, parentContext).
    */
    opentelemetry::context::Context
    context() const
    {
        return opentelemetry::context::RuntimeContext::GetCurrent();
    }
};

}  // namespace telemetry
}  // namespace xrpl

#endif  // XRPL_ENABLE_TELEMETRY
