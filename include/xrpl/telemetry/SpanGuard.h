#pragma once

/**
 * RAII guard for OpenTelemetry trace spans.
 *
 * Wraps an OTel Span and Scope behind the pimpl idiom so that no
 * opentelemetry headers are exposed in this public header. When
 * XRPL_ENABLE_TELEMETRY is not defined, SpanGuard is an empty class
 * with all-inline no-op methods — zero overhead, zero dependencies.
 *
 * Dependency diagram:
 *
 *     +--------------------------------------------+
 *     |                 SpanGuard                  |
 *     +--------------------------------------------+
 *     | - impl_ : unique_ptr<Impl>  (pimpl)        |
 *     +--------------------------------------------+
 *     | + span(cat, prefix, name)         [static] |
 *     | + rootSpan(cat, prefix, name)     [static] |
 *     | + childSpan(name) : SpanGuard              |
 *     | + linkedSpan(name) : SpanGuard             |
 *     | + detached() : SpanGuard                   |
 *     | + captureContext() : SpanContext           |
 *     | + setAttribute(key, value)                 |
 *     | + setOk() / setError(desc)                 |
 *     | + addEvent(name)                           |
 *     | + recordException(e)                       |
 *     | + discard()                                |
 *     | + operator bool()                          |
 *     +--------------------------------------------+
 *                     |  hides (pimpl)
 *             +-------+-------------+
 *             |                     |
 *        +--------+   +---------------------------+
 *        |  Span  |   |     optional<Scope>       |
 *        | (OTel) |   | (OTel, non-movable)       |
 *        |        |   | present : scoped guard    |
 *        |        |   | nullopt : detached guard  |
 *        +--------+   +---------------------------+
 *
 * Static factory methods access the global Telemetry instance
 * internally (via Telemetry::getInstance()), check whether tracing
 * is enabled for the requested subsystem, and return either an
 * active guard or a null (no-op) guard. Callers never need a
 * Telemetry reference.
 *
 * Usage examples:
 *
 * Span names and attribute keys come from per-module `*SpanNames.h`
 * headers (e.g. RpcSpanNames.h, TxSpanNames.h) as typed compile-time
 * constants — never raw string literals — so the naming spec is
 * enforced at the call site and dashboards stay in sync.
 *
 * 1. Basic RPC tracing (factory method with category):
 * @code
 *     #include <xrpld/rpc/detail/RpcSpanNames.h>
 *     using namespace xrpl::telemetry;
 *
 *     auto span = SpanGuard::span(
 *         TraceCategory::Rpc, rpc_span::prefix::command, commandName);
 *     span.setAttribute(rpc_span::attr::command, commandName);
 *     span.setAttribute(rpc_span::attr::rpcStatus, rpc_span::val::success);
 *     // span ended automatically on scope exit
 * @endcode
 *
 * 2. Error recording:
 * @code
 *     auto span = SpanGuard::span(
 *         TraceCategory::Rpc, rpc_span::prefix::command, commandName);
 *     try {
 *         doWork();
 *         span.setOk();
 *     } catch (std::exception const& e) {
 *         span.recordException(e);
 *     }
 * @endcode
 *
 * 3. Cross-thread context propagation:
 * @code
 *     #include <xrpld/rpc/detail/RpcSpanNames.h>
 *     using namespace xrpl::telemetry;
 *
 *     // Thread A: create span and capture context
 *     auto span = SpanGuard::span(
 *         TraceCategory::Rpc, rpc_span::prefix::rpc, rpc_span::op::process);
 *     auto ctx = span.captureContext();
 *
 *     // Thread B: create child with captured context
 *     auto child = SpanGuard::childSpan(rpc_span::op::process, ctx);
 * @endcode
 *
 * 4. Conditional check (rarely needed — methods are no-ops on null):
 * @code
 *     auto span = SpanGuard::span(
 *         TraceCategory::Rpc, rpc_span::prefix::rpc, rpc_span::op::httpRequest);
 *     if (span) {
 *         // expensive attribute computation only when active
 *         span.setAttribute(rpc_span::attr::requestPayloadSize, computeSize());
 *     }
 * @endcode
 *
 * 5. Tail-based filtering via discard():
 * @code
 *     auto span = SpanGuard::span(
 *         TraceCategory::Transactions, tx_span::prefix::tx, tx_span::op::process);
 *     auto result = preflight(tx);
 *     if (result != tesSUCCESS) {
 *         span.discard();  // drop span, never exported
 *         return result;
 *     }
 * @endcode
 *
 * 6. Fresh trace root at an inbound entry point (primary rootSpan use):
 * @code
 *     #include <xrpld/overlay/detail/PeerSpanNames.h>
 *     using namespace xrpl::telemetry;
 *
 *     // A peer message handled on a shared worker thread that may
 *     // already have unrelated spans active — start a clean root so
 *     // those do not become parents of this trace. Names come from a
 *     // *SpanNames.h header, never raw literals.
 *     auto span = SpanGuard::rootSpan(
 *         TraceCategory::Peer, seg::peer, peer_span::op::validationReceive);
 *     span.setAttribute(peer_span::attr::ledgerHash, hashStr);
 * @endcode
 *
 * 7. Hand a span to a job on another thread (edge case, detached):
 * @code
 *     #include <xrpld/app/ledger/detail/LedgerSpanNames.h>
 *     using namespace xrpl::telemetry;
 *
 *     // Build the guard on THIS thread, then strip its thread-local
 *     // Scope so it can be safely moved into a job and ended there.
 *     auto span = SpanGuard::span(
 *         TraceCategory::Ledger, seg::ledger, ledger_span::op::build);
 *     jobQueue.addJob(
 *         [g = std::move(span).detached()]() mutable {
 *             doWork();
 *             // g's span ends when the job's lambda is destroyed,
 *             // on the worker thread — no origin-stack corruption.
 *         });
 * @endcode
 *
 * @note Thread safety: A SCOPED guard (from span(), rootSpan(),
 * childSpan(), linkedSpan()) must only be used on the thread where it
 * was constructed — its internal Scope binds to that thread's
 * thread-local context stack, and destroying it elsewhere would pop
 * the wrong stack. A guard returned by detached() holds no Scope, so
 * it may be moved to and destroyed on another thread; detached()
 * itself must be called on the origin (constructing) thread. Use
 * captureContext() to propagate the trace context to other threads.
 * Violating this rule is enforced (not just documented): a scoped
 * guard destroyed on a foreign thread, or detached() called from one,
 * trips an XRPL_ASSERT in debug/test/fuzzing builds instead of
 * silently corrupting the other thread's context stack.
 *
 * @note Move semantics: Move construction transfers ownership of
 * the pimpl pointer — no double-Scope issues. Move assignment is
 * deleted to prevent re-scoping mid-flight.
 *
 * @note Known limitations:
 * - Attributes cannot be removed per the OTel spec; use
 *   setAttribute with an empty value as a convention.
 * - SpanGuard::span() (raw Span access) is intentionally not
 *   exposed — all interaction goes through the public methods.
 */

#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string_view>

namespace xrpl::telemetry {

/**
 * Trace subsystem categories for conditional span creation.
 *
 * Each value maps to a runtime config flag (e.g. `trace_rpc=1`).
 * Used by SpanGuard::span(TraceCategory, prefix, name) to decide
 * whether to create a real span or return a null guard.
 */
enum class TraceCategory { Rpc, Transactions, Consensus, Peer, Ledger };

/**
 * Opaque wrapper for an OTel context snapshot.
 *
 * Used to propagate trace context across threads. Created by
 * SpanGuard::captureContext(), consumed by SpanGuard::childSpan()
 * or SpanGuard::linkedSpan() with an explicit parent/link context.
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

    /**
     * @return true if this context holds a valid trace context.
     */
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

/**
 * RAII wrapper that activates a span on construction and ends it on
 * destruction. All OTel types are hidden behind the Impl pointer.
 * Non-copyable, move-constructible.
 */
class SpanGuard
{
    struct Impl;
    std::unique_ptr<Impl> impl_;

    explicit SpanGuard(std::unique_ptr<Impl> impl);

public:
    /**
     * Construct a null (no-op) guard. All methods are safe to call.
     */
    SpanGuard();
    ~SpanGuard();

    SpanGuard(SpanGuard&& other) noexcept;
    SpanGuard&
    operator=(SpanGuard&&) = delete;
    SpanGuard(SpanGuard const&) = delete;
    SpanGuard&
    operator=(SpanGuard const&) = delete;

    // --- Static factory methods ----------------------------------------

    /**
     * Create a span guarded by a TraceCategory flag.
     * The span name is built as "prefix.name". Returns a null guard
     * if the category is disabled in config.
     * @param cat     Trace subsystem category.
     * @param prefix  Span name prefix (e.g. "rpc.command").
     * @param name    Span name suffix (e.g. "submit").
     */
    [[nodiscard]] static SpanGuard
    span(TraceCategory cat, std::string_view prefix, std::string_view name);

    /**
     * Create a span that always starts a fresh trace root.
     *
     * Like span(), but ignores this thread's active span so the new
     * span never inherits an ambient parent. Use at an inbound entry
     * point (e.g. a peer message received on a shared worker thread)
     * so unrelated work already on the stack does not pollute the
     * trace. The span name is built as "prefix.name". Returns a null
     * guard if the category is disabled in config.
     *
     * @param cat     Trace subsystem category.
     * @param prefix  Span name prefix (e.g. "peer").
     * @param name    Span name suffix (e.g. "validation.receive").
     * @return An active root-span guard, or a null guard if disabled.
     * @note Must be called on the thread that will own the span, like
     * span(); the returned guard is scoped to that thread.
     */
    [[nodiscard]] static SpanGuard
    rootSpan(TraceCategory cat, std::string_view prefix, std::string_view name);

    // --- Child / linked span creation ----------------------------------

    /**
     * Create a child span parented to this guard's active context.
     * @param name  Span name for the child.
     * @return A new guard, or null if this guard is inactive.
     */
    [[nodiscard]] SpanGuard
    childSpan(std::string_view name) const;

    /**
     * Create a child span parented to an explicit captured context.
     * @param name       Span name for the child.
     * @param parentCtx  Context captured via captureContext().
     * @return A new guard, or null if parentCtx is invalid.
     */
    [[nodiscard]] static SpanGuard
    childSpan(std::string_view name, SpanContext const& parentCtx);

    /**
     * Create a span linked (follows-from) to this guard's span.
     * The new span is NOT a child — it starts a new sub-tree but
     * carries a causal link to this span.
     * @param name  Span name for the linked span.
     * @return A new guard, or null if this guard is inactive.
     */
    [[nodiscard]] SpanGuard
    linkedSpan(std::string_view name) const;

    /**
     * Create a span linked to an explicit captured context.
     * @param name     Span name for the linked span.
     * @param linkCtx  Context to link from.
     * @return A new guard, or null if linkCtx is invalid.
     */
    [[nodiscard]] static SpanGuard
    linkedSpan(std::string_view name, SpanContext const& linkCtx);

    /**
     * Detach this guard's span from the current thread's context stack.
     *
     * A scoped guard holds an OTel Scope bound to the constructing
     * thread's context stack. Moving such a guard to another thread
     * (e.g. into a job queue) and destroying it there would pop the
     * wrong stack, leaving the origin thread's stack corrupted so
     * later spans inherit a stale parent. detached() pops the Scope
     * now, on the origin thread, and returns a new guard that holds
     * the same span with no thread-local binding.
     *
     * Consumes this guard (rvalue-qualified): after the call this
     * guard is null and the returned guard owns the span.
     *
     * @return A scope-less guard safe to move to and destroy on
     * another thread, or a null guard if this guard was null.
     * @note Must be called on the origin (constructing) thread; this is
     * checked by an XRPL_ASSERT in debug/test/fuzzing builds. The
     * returned guard may be freely moved across threads; only
     * its final destruction ends the span.
     */
    [[nodiscard]] SpanGuard
    detached() &&;

    // --- Context capture -----------------------------------------------

    /**
     * Snapshot the current thread's OTel context for cross-thread use.
     * @return An opaque SpanContext, or an invalid one if null guard.
     */
    [[nodiscard]] SpanContext
    captureContext() const;

    // --- Attribute setters (explicit overloads, no OTel types) ---------

    /**
     * Set a string attribute. No-op on a null guard.
     */
    void
    setAttribute(std::string_view key, std::string_view value);

    /**
     * Set a string attribute (C-string overload). No-op on a null guard.
     */
    void
    setAttribute(std::string_view key, char const* value);

    /**
     * Set an integer attribute. No-op on a null guard.
     */
    void
    setAttribute(std::string_view key, std::int64_t value);

    /**
     * Set a floating-point attribute. No-op on a null guard.
     */
    void
    setAttribute(std::string_view key, double value);

    /**
     * Set a boolean attribute. No-op on a null guard.
     */
    void
    setAttribute(std::string_view key, bool value);

    // --- Status / events -----------------------------------------------

    /**
     * Mark the span status as OK. No-op on a null guard.
     */
    void
    setOk();

    /**
     * Mark the span status as error. No-op on a null guard.
     * @param description  Optional human-readable error description.
     */
    void
    setError(std::string_view description = "");

    /**
     * Add a named event to the span's timeline. No-op on a null guard.
     * @param name  Event name.
     */
    void
    addEvent(std::string_view name);

    /**
     * Record an exception as a span event following OTel semantic
     * conventions, and mark the span status as error.
     * No-op on a null guard.
     * @param e  The exception to record.
     */
    void
    recordException(std::exception const& e);

    /**
     * Mark this span for discard and end it immediately.
     * The FilteringSpanProcessor drops the span before it enters the
     * batch export queue. After discard(), the guard is inert.
     */
    void
    discard();

    /**
     * @return true if this guard holds an active span.
     */
    explicit
    operator bool() const;
};

// --- Detach-in-place helpers -----------------------------------------------

/**
 * Detach an active `std::optional<SpanGuard>` member in place.
 *
 * Equivalent to `guard.emplace(std::move(*guard).detached())`, which is the
 * required idiom for detaching a live guard stored in an `optional` (move
 * assignment is deleted because the underlying Scope cannot be re-scoped in
 * place). No-op if `guard` is empty or already null.
 *
 * @param guard  The optional guard to detach. Must be called on the thread
 *               that constructed the active guard inside it (same rule as
 *               `SpanGuard::detached()`).
 * @note Must run BEFORE any `captureContext()` snapshot is taken if the
 *       caller also needs the pre-detach context — capture first, then call
 *       this helper (same ordering rule `detached()` itself has).
 */
void
detachInPlace(std::optional<SpanGuard>& guard);

/**
 * Detach an active `std::shared_ptr<SpanGuard>`, returning the detached
 * guard as a new `shared_ptr`.
 *
 * Equivalent to
 * `guard = std::make_shared<SpanGuard>(std::move(*guard).detached())`.
 * No-op (returns the input unchanged) if `guard` is null or points at a
 * null guard.
 *
 * @param guard  The guard to detach, taken by value (the caller's pointer
 *               is consumed; assign the return value back).
 * @return A `shared_ptr` to the detached guard (new allocation), or the
 *         input pointer unchanged if it was null/inactive.
 */
[[nodiscard]] std::shared_ptr<SpanGuard>
detachInPlace(std::shared_ptr<SpanGuard> guard);

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

    [[nodiscard]] static SpanGuard
    rootSpan(TraceCategory, std::string_view, std::string_view)
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

    [[nodiscard]] SpanGuard
    detached() &&
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

// --- Detach-in-place helpers (no-op stubs) ---------------------------------

inline void
detachInPlace(std::optional<SpanGuard>&)
{
}

[[nodiscard]] inline std::shared_ptr<SpanGuard>
detachInPlace(std::shared_ptr<SpanGuard> guard)
{
    return guard;
}

#endif  // XRPL_ENABLE_TELEMETRY

}  // namespace xrpl::telemetry
