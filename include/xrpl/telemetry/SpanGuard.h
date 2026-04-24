#pragma once

/** RAII guard for OpenTelemetry trace spans.

    Wraps an OTel Span and Scope behind the pimpl idiom so that no
    opentelemetry headers are exposed in this public header. When
    XRPL_ENABLE_TELEMETRY is not defined, SpanGuard is an empty class
    with all-inline no-op methods — zero overhead, zero dependencies.

    Dependency diagram:

        +------------------------------------------------+
        |                 SpanGuard                      |
        +------------------------------------------------+
        | - impl_ : unique_ptr<Impl>  (pimpl)            |
        +------------------------------------------------+
        | + span(cat, prefix, name)              [static] |
        | + childSpan(name) : SpanGuard                   |
        | + linkedSpan(name) : SpanGuard                  |
        | + hashSpan(cat, name, hash)            [static] |
        | + hashSpan(cat, name, hash, parent)    [static] |
        | + captureContext() : SpanContext                 |
        | + setAttribute(key, value)                      |
        | + setOk() / setError(desc)                      |
        | + addEvent(name)                                |
        | + recordException(e)                            |
        | + discard()                                     |
        | + operator bool()                               |
        +------------------------------------------------+
                        |  hides (pimpl)
                +-------+-------+
                |               |
           +--------+   +-------------+
           |  Span  |   |    Scope    |
           | (OTel) |   | (OTel, non- |
           |        |   |  movable)   |
           +--------+   +-------------+

    Static factory methods access the global Telemetry instance
    internally (via Telemetry::getInstance()), check whether tracing
    is enabled for the requested subsystem, and return either an
    active guard or a null (no-op) guard. Callers never need a
    Telemetry reference.

    Usage examples:

    1. Basic RPC tracing (factory method with category):
    @code
        #include <xrpld/rpc/detail/RpcSpanNames.h>

        // At the call site (constants from RpcSpanNames.h):
        auto span = SpanGuard::span(
            TraceCategory::Rpc, rpc_span::prefix::command, "submit");
        span.setAttribute(rpc_span::attr::command, "submit");
        span.setAttribute(rpc_span::attr::status, rpc_span::val::success);
        // span ended automatically on scope exit
    @endcode

    2. Error recording:
    @code
        auto span = SpanGuard::span(
            TraceCategory::Rpc, rpc_span::prefix::command, "submit");
        try {
            doWork();
            span.setOk();
        } catch (std::exception const& e) {
            span.recordException(e);
        }
    @endcode

    3. Cross-thread context propagation:
    @code
        // Thread A: create span and capture context
        auto span = SpanGuard::span(
            TraceCategory::Consensus, seg::consensus, "round");
        auto ctx = span.captureContext();

        // Thread B: create child with captured context
        auto child = SpanGuard::childSpan("consensus.accept", ctx);
    @endcode

    4. Conditional check (rarely needed — methods are no-ops on null):
    @code
        auto span = SpanGuard::span(
            TraceCategory::Rpc, rpc_span::prefix::rpc, "request");
        if (span) {
            // expensive attribute computation only when active
            span.setAttribute(rpc_span::attr::payloadSize, computeSize());
        }
    @endcode

    5. Tail-based filtering via discard():
    @code
        auto span = SpanGuard::span(
            TraceCategory::Transactions, seg::tx, "process");
        auto result = preflight(tx);
        if (result != tesSUCCESS) {
            span.discard();  // drop span, never exported
            return result;
        }
    @endcode

    @note Thread safety: A SpanGuard must only be used on the thread
    where it was constructed (the internal Scope binds to the
    thread-local context stack). Use captureContext() to propagate
    the trace to other threads.

    @note Move semantics: Move construction transfers ownership of
    the pimpl pointer — no double-Scope issues. Move assignment is
    deleted to prevent re-scoping mid-flight.

    @note Known limitations:
    - Attributes cannot be removed per the OTel spec; use
      setAttribute with an empty value as a convention.
    - SpanGuard::span() (raw Span access) is intentionally not
      exposed — all interaction goes through the public methods.
*/

#include <cstdint>
#include <exception>
#include <initializer_list>
#include <memory>
#include <string_view>
#include <utility>

namespace xrpl::telemetry {

/** Trace subsystem categories for conditional span creation.

    Each value maps to a runtime config flag (e.g. `trace_rpc=1`).
    Used by SpanGuard::span(TraceCategory, prefix, name) to decide
    whether to create a real span or return a null guard.
*/
enum class TraceCategory { Rpc, Transactions, Consensus, Peer, Ledger };

/** Key-value pair for span event attributes.
    Used by addEvent(name, attrs) to attach structured metadata to events.
*/
using EventAttribute = std::pair<std::string_view, std::string_view>;

/** Opaque wrapper for an OTel context snapshot.

    Used to propagate trace context across threads. Created by
    SpanGuard::captureContext(), consumed by SpanGuard::childSpan()
    or SpanGuard::linkedSpan() with an explicit parent/link context.
*/
class SpanContext
{
    friend class SpanGuard;

#ifdef XRPL_ENABLE_TELEMETRY
    struct Impl;
    std::shared_ptr<Impl> impl_;
    explicit SpanContext(std::shared_ptr<Impl> impl);
#endif

public:
    SpanContext() = default;

    /** @return true if this context holds a valid trace context. */
#ifdef XRPL_ENABLE_TELEMETRY
    [[nodiscard]] bool
    isValid() const;
#else
    // NOLINTBEGIN(readability-convert-member-functions-to-static)
    [[nodiscard]] bool
    isValid() const
    {
        return false;
    }
    // NOLINTEND(readability-convert-member-functions-to-static)
#endif
};

// ---------------------------------------------------------------------------
// Real implementation (pimpl, compiled in SpanGuard.cpp)
// ---------------------------------------------------------------------------
#ifdef XRPL_ENABLE_TELEMETRY

/** RAII wrapper that activates a span on construction and ends it on
    destruction. All OTel types are hidden behind the Impl pointer.
    Non-copyable, move-constructible.
*/
class SpanGuard
{
    struct Impl;
    std::unique_ptr<Impl> impl_;

    explicit SpanGuard(std::unique_ptr<Impl> impl);

public:
    /** Construct a null (no-op) guard. All methods are safe to call. */
    SpanGuard();
    ~SpanGuard();

    SpanGuard(SpanGuard&& other) noexcept;
    SpanGuard&
    operator=(SpanGuard&&) = delete;
    SpanGuard(SpanGuard const&) = delete;
    SpanGuard&
    operator=(SpanGuard const&) = delete;

    // --- Static factory methods ----------------------------------------

    /** Create a span guarded by a TraceCategory flag.
        The span name is built as "prefix.name". Returns a null guard
        if the category is disabled in config.
        @param cat     Trace subsystem category.
        @param prefix  Span name prefix (e.g. "rpc.command").
        @param name    Span name suffix (e.g. "submit").
    */
    [[nodiscard]] static SpanGuard
    span(TraceCategory cat, std::string_view prefix, std::string_view name);

    // --- Child / linked span creation ----------------------------------

    /** Create a child span parented to this guard's active context.
        @param name  Span name for the child.
        @return A new guard, or null if this guard is inactive.
    */
    [[nodiscard]] SpanGuard
    childSpan(std::string_view name) const;

    /** Create a child span parented to an explicit captured context.
        @param name       Span name for the child.
        @param parentCtx  Context captured via captureContext().
        @return A new guard, or null if parentCtx is invalid.
    */
    [[nodiscard]] static SpanGuard
    childSpan(std::string_view name, SpanContext const& parentCtx);

    /** Create a span linked (follows-from) to this guard's span.
        The new span is NOT a child — it starts a new sub-tree but
        carries a causal link to this span.
        @param name  Span name for the linked span.
        @return A new guard, or null if this guard is inactive.
    */
    [[nodiscard]] SpanGuard
    linkedSpan(std::string_view name) const;

    /** Create a span linked to an explicit captured context.
        @param name     Span name for the linked span.
        @param linkCtx  Context to link from.
        @return A new guard, or null if linkCtx is invalid.
    */
    [[nodiscard]] static SpanGuard
    linkedSpan(std::string_view name, SpanContext const& linkCtx);

    // --- Hash-derived span (category-gated) -----------------------------

    /** Create a span whose trace_id is derived from arbitrary hash data.
        trace_id = hashData[0:16], span_id = random. Gated by the given
        TraceCategory. All nodes using the same hash independently produce
        spans under the same trace_id, enabling cross-node correlation
        without context propagation.
        @param cat       Trace subsystem category.
        @param name      Full span name (e.g. "tx.receive").
        @param hashData  Pointer to at least 16 bytes of hash data.
        @param hashSize  Size of the hash buffer (must be >= 16).
    */
    static SpanGuard
    hashSpan(
        TraceCategory cat,
        std::string_view name,
        std::uint8_t const* hashData,
        std::size_t hashSize);

    /** Create a hash-derived span with a remote parent.
        trace_id = hashData[0:16], parent span_id from protobuf context
        propagation. Produces a child span of the sender's span while
        sharing the deterministic trace_id.
        @param cat             Trace subsystem category.
        @param name            Full span name.
        @param hashData        Pointer to at least 16 bytes of hash data.
        @param hashSize        Size of the hash buffer (must be >= 16).
        @param parentSpanId    Pointer to 8 bytes of parent span ID.
        @param parentSpanSize  Size of parent span ID buffer (must be 8).
        @param traceFlags      Trace flags from remote context.
    */
    static SpanGuard
    hashSpan(
        TraceCategory cat,
        std::string_view name,
        std::uint8_t const* hashData,
        std::size_t hashSize,
        std::uint8_t const* parentSpanId,
        std::size_t parentSpanSize,
        std::uint8_t traceFlags);

    // --- Context capture -----------------------------------------------

    /** Snapshot the current thread's OTel context for cross-thread use.
        @return An opaque SpanContext, or an invalid one if null guard.
    */
    [[nodiscard]] SpanContext
    captureContext() const;

    // --- Attribute setters (explicit overloads, no OTel types) ---------

    /** Set a string attribute. No-op on a null guard. */
    void
    setAttribute(std::string_view key, std::string_view value);

    /** Set a string attribute (C-string overload). No-op on a null guard. */
    void
    setAttribute(std::string_view key, char const* value);

    /** Set an integer attribute. No-op on a null guard. */
    void
    setAttribute(std::string_view key, std::int64_t value);

    /** Set a floating-point attribute. No-op on a null guard. */
    void
    setAttribute(std::string_view key, double value);

    /** Set a boolean attribute. No-op on a null guard. */
    void
    setAttribute(std::string_view key, bool value);

    // --- Status / events -----------------------------------------------

    /** Mark the span status as OK. No-op on a null guard. */
    void
    setOk();

    /** Mark the span status as error. No-op on a null guard.
        @param description  Optional human-readable error description.
    */
    void
    setError(std::string_view description = "");

    /** Add a named event to the span's timeline. No-op on a null guard.
        @param name  Event name.
    */
    void
    addEvent(std::string_view name);

    /** Add a named event with key-value attributes to the span's timeline.
        No-op on a null guard.
        @param name   Event name.
        @param attrs  Attribute pairs (all string_view for simplicity).
    */
    void
    addEvent(std::string_view name, std::initializer_list<EventAttribute> attrs);

    /** Record an exception as a span event following OTel semantic
        conventions, and mark the span status as error.
        No-op on a null guard.
        @param e  The exception to record.
    */
    void
    recordException(std::exception const& e);

    /** Mark this span for discard and end it immediately.
        The FilteringSpanProcessor drops the span before it enters the
        batch export queue. After discard(), the guard is inert.
    */
    void
    discard();

    /** @return true if this guard holds an active span. */
    explicit
    operator bool() const;
};

// ---------------------------------------------------------------------------
// No-op stub (all inline, zero overhead, no OTel dependency)
// ---------------------------------------------------------------------------
#else  // XRPL_ENABLE_TELEMETRY not defined

class SpanGuard
{
public:
    SpanGuard() = default;
    ~SpanGuard() = default;
    SpanGuard(SpanGuard&&) noexcept = default;
    SpanGuard&
    operator=(SpanGuard&&) = delete;
    SpanGuard(SpanGuard const&) = delete;
    SpanGuard&
    operator=(SpanGuard const&) = delete;

    [[nodiscard]] static SpanGuard
    span(TraceCategory, std::string_view, std::string_view)
    {
        return {};
    }

    // NOLINTBEGIN(readability-convert-member-functions-to-static)
    [[nodiscard]] SpanGuard
    childSpan(std::string_view) const
    {
        return {};
    }
    [[nodiscard]] static SpanGuard
    childSpan(std::string_view, SpanContext const&)
    {
        return {};
    }
    [[nodiscard]] SpanGuard
    linkedSpan(std::string_view) const
    {
        return {};
    }
    [[nodiscard]] static SpanGuard
    linkedSpan(std::string_view, SpanContext const&)
    {
        return {};
    }

    [[nodiscard]] static SpanGuard
    hashSpan(TraceCategory, std::string_view, std::uint8_t const*, std::size_t)
    {
        return {};
    }
    [[nodiscard]] static SpanGuard
    hashSpan(
        TraceCategory,
        std::string_view,
        std::uint8_t const*,
        std::size_t,
        std::uint8_t const*,
        std::size_t,
        std::uint8_t)
    {
        return {};
    }

    [[nodiscard]] SpanContext
    captureContext() const
    {
        return {};
    }
    // NOLINTEND(readability-convert-member-functions-to-static)

    void
    setAttribute(std::string_view, std::string_view)
    {
    }
    void
    setAttribute(std::string_view, char const*)
    {
    }
    void
    setAttribute(std::string_view, std::int64_t)
    {
    }
    void
    setAttribute(std::string_view, double)
    {
    }
    void
    setAttribute(std::string_view, bool)
    {
    }

    void
    setOk()
    {
    }
    void
    setError(std::string_view = "")
    {
    }
    void
    addEvent(std::string_view)
    {
    }
    void
    addEvent(std::string_view, std::initializer_list<EventAttribute>)
    {
    }
    void
    recordException(std::exception const&)
    {
    }
    void
    discard()
    {
    }

    explicit
    operator bool() const
    {
        return false;
    }
};

#endif  // XRPL_ENABLE_TELEMETRY

}  // namespace xrpl::telemetry
