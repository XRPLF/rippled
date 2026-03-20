#pragma once

/** Convenience macros for instrumenting code with OpenTelemetry trace spans.

    When XRPL_ENABLE_TELEMETRY is defined, the macros create SpanGuard objects
    that manage span lifetime via RAII. When not defined, all macros expand to
    ((void)0) with zero overhead.

    Usage in instrumented code:
    @code
        XRPL_TRACE_RPC(app.getTelemetry(), "rpc.command." + name);
        XRPL_TRACE_SET_ATTR("xrpl.rpc.command", name);
        XRPL_TRACE_SET_ATTR("xrpl.rpc.status", "success");
    @endcode

    @note Macro parameter names use leading/trailing underscores
    (e.g. _tel_obj_) to avoid colliding with identifiers in the macro body,
    specifically the ::xrpl::telemetry:: namespace qualifier.
*/

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpl/telemetry/SpanGuard.h>
#include <xrpl/telemetry/Telemetry.h>

#include <optional>

namespace xrpl {
namespace telemetry {

/** Start an unconditional span, ended when the guard goes out of scope.
    @param _tel_obj_    Telemetry instance reference.
    @param _span_name_  Span name string.
*/
#define XRPL_TRACE_SPAN(_tel_obj_, _span_name_)            \
    auto _xrpl_span_ = (_tel_obj_).startSpan(_span_name_); \
    ::xrpl::telemetry::SpanGuard _xrpl_guard_(_xrpl_span_)

/** Start an unconditional span with a specific SpanKind.
    @param _tel_obj_    Telemetry instance reference.
    @param _span_name_  Span name string.
    @param _span_kind_  opentelemetry::trace::SpanKind value.
*/
#define XRPL_TRACE_SPAN_KIND(_tel_obj_, _span_name_, _span_kind_)       \
    auto _xrpl_span_ = (_tel_obj_).startSpan(_span_name_, _span_kind_); \
    ::xrpl::telemetry::SpanGuard _xrpl_guard_(_xrpl_span_)

/** Conditionally start a span for RPC tracing.
    The span is only created if shouldTraceRpc() returns true.
    @param _tel_obj_    Telemetry instance reference.
    @param _span_name_  Span name string.
*/
#define XRPL_TRACE_RPC(_tel_obj_, _span_name_)                    \
    std::optional<::xrpl::telemetry::SpanGuard> _xrpl_guard_;     \
    if ((_tel_obj_).shouldTraceRpc())                             \
    {                                                             \
        _xrpl_guard_.emplace((_tel_obj_).startSpan(_span_name_)); \
    }

/** Conditionally start a span for transaction tracing.
    The span is only created if shouldTraceTransactions() returns true.
    @param _tel_obj_    Telemetry instance reference.
    @param _span_name_  Span name string.
*/
#define XRPL_TRACE_TX(_tel_obj_, _span_name_)                     \
    std::optional<::xrpl::telemetry::SpanGuard> _xrpl_guard_;     \
    if ((_tel_obj_).shouldTraceTransactions())                    \
    {                                                             \
        _xrpl_guard_.emplace((_tel_obj_).startSpan(_span_name_)); \
    }

/** Conditionally start a span for consensus tracing.
    The span is only created if shouldTraceConsensus() returns true.
    @param _tel_obj_    Telemetry instance reference.
    @param _span_name_  Span name string.
*/
#define XRPL_TRACE_CONSENSUS(_tel_obj_, _span_name_)              \
    std::optional<::xrpl::telemetry::SpanGuard> _xrpl_guard_;     \
    if ((_tel_obj_).shouldTraceConsensus())                       \
    {                                                             \
        _xrpl_guard_.emplace((_tel_obj_).startSpan(_span_name_)); \
    }

/** Conditionally start a span for peer message tracing.
    The span is only created if shouldTracePeer() returns true.
    @param _tel_obj_    Telemetry instance reference.
    @param _span_name_  Span name string.
*/
#define XRPL_TRACE_PEER(_tel_obj_, _span_name_)                   \
    std::optional<::xrpl::telemetry::SpanGuard> _xrpl_guard_;     \
    if ((_tel_obj_).shouldTracePeer())                            \
    {                                                             \
        _xrpl_guard_.emplace((_tel_obj_).startSpan(_span_name_)); \
    }

/** Conditionally start a span for ledger tracing.
    The span is only created if shouldTraceLedger() returns true.
    @param _tel_obj_    Telemetry instance reference.
    @param _span_name_  Span name string.
*/
#define XRPL_TRACE_LEDGER(_tel_obj_, _span_name_)                 \
    std::optional<::xrpl::telemetry::SpanGuard> _xrpl_guard_;     \
    if ((_tel_obj_).shouldTraceLedger())                          \
    {                                                             \
        _xrpl_guard_.emplace((_tel_obj_).startSpan(_span_name_)); \
    }

/** Set a key-value attribute on the current span (if it exists).
    Must be used after one of the XRPL_TRACE_* span macros.
*/
#define XRPL_TRACE_SET_ATTR(key, value)         \
    if (_xrpl_guard_.has_value())               \
    {                                           \
        _xrpl_guard_->setAttribute(key, value); \
    }

/** Record an exception on the current span and mark it as error.
    Must be used after one of the XRPL_TRACE_* span macros.
*/
#define XRPL_TRACE_EXCEPTION(e)           \
    if (_xrpl_guard_.has_value())         \
    {                                     \
        _xrpl_guard_->recordException(e); \
    }

}  // namespace telemetry
}  // namespace xrpl

#else  // XRPL_ENABLE_TELEMETRY not defined

#define XRPL_TRACE_SPAN(_tel_obj_, _span_name_) ((void)0)
#define XRPL_TRACE_SPAN_KIND(_tel_obj_, _span_name_, _span_kind_) ((void)0)
#define XRPL_TRACE_RPC(_tel_obj_, _span_name_) ((void)0)
#define XRPL_TRACE_TX(_tel_obj_, _span_name_) ((void)0)
#define XRPL_TRACE_CONSENSUS(_tel_obj_, _span_name_) ((void)0)
#define XRPL_TRACE_PEER(_tel_obj_, _span_name_) ((void)0)
#define XRPL_TRACE_LEDGER(_tel_obj_, _span_name_) ((void)0)
#define XRPL_TRACE_SET_ATTR(key, value) ((void)0)
#define XRPL_TRACE_EXCEPTION(e) ((void)0)

#endif  // XRPL_ENABLE_TELEMETRY
