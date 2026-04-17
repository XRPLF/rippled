#pragma once

/** Convenience macros for instrumenting code with OpenTelemetry trace spans.

    When XRPL_ENABLE_TELEMETRY is defined, the macros create SpanGuard objects
    that manage span lifetime via RAII. When not defined, all macros expand to
    ((void)0) with zero overhead.

    All span-creation macros produce a std::optional<SpanGuard> named
    _xrpl_guard_. The accessor macros (XRPL_TRACE_SET_ATTR,
    XRPL_TRACE_EXCEPTION) reference this variable by name, so they must
    appear in the same scope after exactly one span-creation macro.

    @note Only one XRPL_TRACE_* span-creation macro may appear per scope,
    because they all declare a variable named _xrpl_guard_. Nested spans
    across function boundaries are fine (each function has its own scope).

    @note These macros must not be used in single-statement if/else without
    braces. The span-creation macros expand to multiple statements that
    declare variables needed by the accessor macros.

    @note Thread safety: Each SpanGuard binds to the constructing thread's
    OTel context stack via Scope. Do not move a guard across threads.

    Usage examples:

    1. Basic RPC tracing:
    @code
        XRPL_TRACE_RPC(app.getTelemetry(), "rpc.command." + name);
        XRPL_TRACE_SET_ATTR("xrpl.rpc.command", name);
        XRPL_TRACE_SET_ATTR("xrpl.rpc.status", "success");
    @endcode

    2. Exception recording:
    @code
        XRPL_TRACE_RPC(telemetry, "rpc.process");
        try {
            doWork();
        } catch (std::exception const& e) {
            XRPL_TRACE_EXCEPTION(e);
            XRPL_TRACE_SET_ATTR("xrpl.rpc.status", "error");
        }
    @endcode

    3. Unconditional span:
    @code
        XRPL_TRACE_SPAN(telemetry, "tx.apply");
        XRPL_TRACE_SET_ATTR("xrpl.tx.hash", txHash);
    @endcode
*/

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpl/telemetry/SpanGuard.h>
#include <xrpl/telemetry/Telemetry.h>

#include <optional>

/** Start an unconditional span, ended when the guard goes out of scope.
    @param _tel_obj_    Telemetry instance reference.
    @param _span_name_  Span name string.
*/
#define XRPL_TRACE_SPAN(_tel_obj_, _span_name_)               \
    std::optional<::xrpl::telemetry::SpanGuard> _xrpl_guard_( \
        std::in_place, (_tel_obj_).startSpan(_span_name_))

/** Start an unconditional span with a specific SpanKind.
    @param _tel_obj_    Telemetry instance reference.
    @param _span_name_  Span name string.
    @param _span_kind_  opentelemetry::trace::SpanKind value.
*/
#define XRPL_TRACE_SPAN_KIND(_tel_obj_, _span_name_, _span_kind_) \
    std::optional<::xrpl::telemetry::SpanGuard> _xrpl_guard_(     \
        std::in_place, (_tel_obj_).startSpan(_span_name_, _span_kind_))

/** Conditionally start a span for RPC tracing.
    The span is only created if shouldTraceRpc() returns true.
    @param _tel_obj_    Telemetry instance reference.
    @param _span_name_  Span name string.
*/
#define XRPL_TRACE_RPC(_tel_obj_, _span_name_)                   \
    auto& _xrpl_tel_ = (_tel_obj_);                              \
    std::optional<::xrpl::telemetry::SpanGuard> _xrpl_guard_;    \
    if (_xrpl_tel_.shouldTraceRpc())                             \
    {                                                            \
        _xrpl_guard_.emplace(_xrpl_tel_.startSpan(_span_name_)); \
    }

/** Conditionally start a span for transaction tracing.
    The span is only created if shouldTraceTransactions() returns true.
    @param _tel_obj_    Telemetry instance reference.
    @param _span_name_  Span name string.
*/
#define XRPL_TRACE_TX(_tel_obj_, _span_name_)                    \
    auto& _xrpl_tel_ = (_tel_obj_);                              \
    std::optional<::xrpl::telemetry::SpanGuard> _xrpl_guard_;    \
    if (_xrpl_tel_.shouldTraceTransactions())                    \
    {                                                            \
        _xrpl_guard_.emplace(_xrpl_tel_.startSpan(_span_name_)); \
    }

/** Conditionally start a span for consensus tracing.
    The span is only created if shouldTraceConsensus() returns true.
    @param _tel_obj_    Telemetry instance reference.
    @param _span_name_  Span name string.
*/
#define XRPL_TRACE_CONSENSUS(_tel_obj_, _span_name_)             \
    auto& _xrpl_tel_ = (_tel_obj_);                              \
    std::optional<::xrpl::telemetry::SpanGuard> _xrpl_guard_;    \
    if (_xrpl_tel_.shouldTraceConsensus())                       \
    {                                                            \
        _xrpl_guard_.emplace(_xrpl_tel_.startSpan(_span_name_)); \
    }

/** Set a key-value attribute on the current span (if it exists).
    Must be used after one of the XRPL_TRACE_* span-creation macros
    in the same scope.
*/
#define XRPL_TRACE_SET_ATTR(key, value)             \
    do                                              \
    {                                               \
        if (_xrpl_guard_.has_value())               \
            _xrpl_guard_->setAttribute(key, value); \
    } while (0)

/** Record an exception on the current span and mark it as error.
    Must be used after one of the XRPL_TRACE_* span-creation macros
    in the same scope.
*/
#define XRPL_TRACE_EXCEPTION(e)               \
    do                                        \
    {                                         \
        if (_xrpl_guard_.has_value())         \
            _xrpl_guard_->recordException(e); \
    } while (0)

#else  // XRPL_ENABLE_TELEMETRY not defined

#define XRPL_TRACE_SPAN(_tel_obj_, _span_name_) ((void)0)
#define XRPL_TRACE_SPAN_KIND(_tel_obj_, _span_name_, _span_kind_) ((void)0)
#define XRPL_TRACE_RPC(_tel_obj_, _span_name_) ((void)0)
#define XRPL_TRACE_TX(_tel_obj_, _span_name_) ((void)0)
#define XRPL_TRACE_CONSENSUS(_tel_obj_, _span_name_) ((void)0)
#define XRPL_TRACE_SET_ATTR(key, value) ((void)0)
#define XRPL_TRACE_EXCEPTION(e) ((void)0)

#endif  // XRPL_ENABLE_TELEMETRY
