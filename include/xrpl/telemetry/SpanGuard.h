#pragma once

/**
 * RAII guards for OpenTelemetry trace spans.
 *
 * This header declares two complementary span guards, split by
 * responsibility:
 *
 * - SpanGuard   — owns a span only. Thread-free: it never touches the
 *                 OTel thread-local context stack, so it may be moved
 *                 to and destroyed on any thread. Movable AND
 *                 move-assignable.
 * - ScopedSpanGuard — owns a SpanGuard plus an active OTel Scope that
 *                 pushes the span onto the constructing thread's
 *                 context stack. Thread-bound: it must be created and
 *                 destroyed on the same thread. Non-copyable and
 *                 non-movable.
 *
 * Both wrap all OpenTelemetry types behind the pimpl idiom so no
 * opentelemetry headers are exposed here. When XRPL_ENABLE_TELEMETRY
 * is not defined, both are empty classes with all-inline no-op methods
 * — zero overhead, zero dependencies.
 *
 * SpanGuard dependency diagram:
 *
 *     +--------------------------------------------+
 *     |                 SpanGuard                  |
 *     |          (unscoped, thread-free)           |
 *     +--------------------------------------------+
 *     | - impl_ : unique_ptr<Impl>  (pimpl)        |
 *     +--------------------------------------------+
 *     | + span(cat, prefix, name)         [static] |
 *     | + freshRoot(cat, prefix, name)    [static] |
 *     | + childSpan(name) : SpanGuard              |
 *     | + linkedSpan(name) : SpanGuard             |
 *     | + spanContext() : SpanContext              |
 *     | + threadLocalContext()            [static] |
 *     | + setAttribute(key, value)                 |
 *     | + setOk() / setError(desc)                 |
 *     | + addEvent(name)                           |
 *     | + recordException(e)                       |
 *     | + discard()                                |
 *     | + operator bool()                          |
 *     +--------------------------------------------+
 *                     |  hides (pimpl)
 *                +--------+
 *                |  Span  |
 *                | (OTel) |
 *                +--------+
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
 *     // span ended automatically when the guard is destroyed
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
 *     // Thread A: create span and capture its own context as parent
 *     auto span = SpanGuard::span(
 *         TraceCategory::Rpc, rpc_span::prefix::rpc, rpc_span::op::process);
 *     auto ctx = span.spanContext();
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
 * 6. Fresh trace root at an inbound entry point (primary freshRoot use):
 * @code
 *     #include <xrpld/overlay/detail/PeerSpanNames.h>
 *     using namespace xrpl::telemetry;
 *
 *     // A peer message handled on a shared worker thread that may
 *     // already have unrelated spans active — start a clean root so
 *     // those do not become parents of this trace. Names come from a
 *     // *SpanNames.h header, never raw literals.
 *     auto span = SpanGuard::freshRoot(
 *         TraceCategory::Peer, seg::peer, peer_span::op::validationReceive);
 *     span.setAttribute(peer_span::attr::ledgerHash, hashStr);
 * @endcode
 *
 * 7. Hand a span to a job on another thread (thread-free — just move):
 * @code
 *     #include <xrpld/app/ledger/detail/LedgerSpanNames.h>
 *     using namespace xrpl::telemetry;
 *
 *     // SpanGuard owns no thread-local Scope, so it is safe to move
 *     // into a job and end on the worker thread. No detach step is
 *     // needed — just move the guard in.
 *     auto span = SpanGuard::span(
 *         TraceCategory::Ledger, seg::ledger, ledger_span::op::build);
 *     jobQueue.addJob(
 *         [g = std::move(span)]() mutable {
 *             doWork();
 *             // g's span ends when the job's lambda is destroyed,
 *             // on the worker thread.
 *         });
 * @endcode
 *
 * @note Thread safety: SpanGuard is thread-free. It holds only the
 * span (no Scope), so it never binds to a thread-local context stack
 * and may be moved to and destroyed on any thread. To make a span the
 * ambient parent for child spans on a thread, wrap it in a
 * ScopedSpanGuard. Use spanContext() to propagate this span's own
 * context across threads explicitly, or threadLocalContext() to
 * snapshot the thread's currently-active context.
 *
 * @note Move semantics: Move construction and move assignment both
 * transfer ownership of the pimpl pointer. There is no Scope to
 * re-bind, so move assignment is safe and enabled. Copy is deleted.
 *
 * @note Known limitations:
 * - Attributes cannot be removed per the OTel spec; use
 *   setAttribute with an empty value as a convention.
 * - The raw OTel Span is intentionally not exposed — all interaction
 *   goes through the public methods.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
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
 * Raw trace context bytes for cross-node propagation.
 *
 * Holds the binary trace_id, span_id, and trace_flags extracted from
 * an active span. Used by protocol-layer code to inject trace context
 * into outgoing protobuf messages without depending on OTel types.
 *
 * @see SpanGuard::getTraceBytes(), TraceContextPropagator.h
 */
struct TraceBytes
{
    std::array<std::uint8_t, 16> traceId{};  ///< 16-byte W3C trace identifier.
    std::array<std::uint8_t, 8> spanId{};    ///< 8-byte span id of current span.
    std::uint8_t traceFlags{0};              ///< W3C trace flags (bit 0 = sampled).
    bool valid{false};  ///< True if this struct holds data from an active span.
};

/**
 * Opaque wrapper for an OTel context snapshot.
 *
 * Used to propagate trace context across threads. Created by
 * SpanGuard::spanContext() (the guard's own span) or
 * SpanGuard::threadLocalContext() (the thread's current context),
 * consumed by SpanGuard::childSpan() or SpanGuard::linkedSpan()
 * with an explicit parent/link context.
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
 * RAII owner of an OTel span (unscoped, thread-free).
 *
 * Holds only the span behind the Impl pointer — it never pushes the
 * span onto the thread-local context stack, so it carries no
 * thread-affinity and may be moved to and destroyed on any thread.
 * The span is ended when the guard is destroyed (unless discard()
 * ended it earlier). Non-copyable; movable and move-assignable.
 *
 * To activate a span as the ambient parent for child spans on a
 * thread, wrap the guard in a ScopedSpanGuard.
 */
class SpanGuard
{
    struct Impl;
    std::unique_ptr<Impl> impl_;

    explicit SpanGuard(std::unique_ptr<Impl> impl);

    // ScopedSpanGuard wraps a SpanGuard and needs the raw span to build
    // its Scope; it reads impl_ directly so no OTel type leaks here.
    friend class ScopedSpanGuard;

public:
    /**
     * Construct a null (no-op) guard. All methods are safe to call.
     */
    SpanGuard();
    ~SpanGuard();

    SpanGuard(SpanGuard&& other) noexcept;
    SpanGuard&
    operator=(SpanGuard&&) noexcept;
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
     */
    [[nodiscard]] static SpanGuard
    freshRoot(TraceCategory cat, std::string_view prefix, std::string_view name);

    // --- Child / linked span creation ----------------------------------

    /**
     * Create a child span parented to the current thread's active
     * context. This is meaningful when this guard's span is active on
     * the thread (e.g. wrapped in a ScopedSpanGuard); otherwise the
     * child inherits whatever context is currently on the stack.
     * @param name  Span name for the child.
     * @return A new guard, or null if this guard is inactive.
     */
    [[nodiscard]] SpanGuard
    childSpan(std::string_view name) const;

    /**
     * Create a child span parented to an explicit captured context.
     * @param name       Span name for the child.
     * @param parentCtx  Context captured via spanContext().
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

    // --- Hash-derived span (category-gated) -----------------------------

    /**
     * Create a span whose trace_id is derived from arbitrary hash data.
     * trace_id = hashData[0:16], span_id = random. Gated by the given
     * TraceCategory. All nodes using the same hash independently produce
     * spans under the same trace_id, enabling cross-node correlation
     * without context propagation.
     * @param cat       Trace subsystem category.
     * @param name      Full span name (e.g. "tx.receive").
     * @param hashData  Pointer to at least 16 bytes of hash data.
     * @param hashSize  Size of the hash buffer (must be >= 16).
     */
    static SpanGuard
    hashSpan(
        TraceCategory const cat,
        std::string_view const name,
        std::uint8_t const* const hashData,
        std::size_t const hashSize);

    /**
     * Create a hash-derived span with a remote parent.
     * trace_id = hashData[0:16], parent span_id from protobuf context
     * propagation. Produces a child span of the sender's span while
     * sharing the deterministic trace_id.
     * @param cat             Trace subsystem category.
     * @param name            Full span name.
     * @param hashData        Pointer to at least 16 bytes of hash data.
     * @param hashSize        Size of the hash buffer (must be >= 16).
     * @param parentSpanId    Pointer to 8 bytes of parent span ID.
     * @param parentSpanSize  Size of parent span ID buffer (must be 8).
     * @param traceFlags      Trace flags from remote context.
     */
    static SpanGuard
    hashSpan(
        TraceCategory const cat,
        std::string_view const name,
        std::uint8_t const* const hashData,
        std::size_t const hashSize,
        std::uint8_t const* const parentSpanId,
        std::size_t const parentSpanSize,
        std::uint8_t const traceFlags);

    // --- Context capture -----------------------------------------------

    /**
     * Return a SpanContext referring to THIS guard's own span, for use
     * as an explicit parent in childSpan(name, ctx) across threads.
     * Independent of the thread's active span (a SpanGuard is unscoped).
     * Returns an invalid context if this guard is null.
     * @return A SpanContext holding this guard's own span.
     * @note This is NOT the thread's ambient context — use
     * threadLocalContext() for that.
     */
    [[nodiscard]] SpanContext
    spanContext() const;

    /**
     * Snapshot the calling thread's CURRENTLY-ACTIVE OTel context (the
     * ambient span on this thread's context stack), for cross-thread
     * propagation. This is the OpenTelemetry "capture current context"
     * operation: capture -> pass to another thread -> childSpan(name,
     * ctx). Unlike spanContext() (which refers to a specific guard's own
     * span), this reflects whatever span is active on THIS thread right
     * now, or an invalid context if none is active.
     * @return A SpanContext holding the thread's current context.
     */
    [[nodiscard]] static SpanContext
    threadLocalContext();

    /**
     * Extract raw trace context bytes from this span for propagation.
     *
     * Unlike threadLocalContext() which captures the thread-local runtime
     * context, this method reads the span's own SpanContext directly.
     * Safe to call from any thread that holds a reference to this guard.
     *
     * @return A TraceBytes struct with valid=true if the span is active
     *         and has a valid context, or valid=false otherwise.
     */
    [[nodiscard]] TraceBytes
    getTraceBytes() const;

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

/**
 * RAII guard that activates a span on the current thread (scoped).
 *
 * Wraps a SpanGuard (which owns the span) plus an OTel Scope that
 * pushes the span onto this thread's thread-local context stack for
 * the guard's lifetime, so child spans created on the thread inherit
 * it as parent. On destruction the Scope pops BEFORE the span ends
 * (member order: guard first, scope second). Non-copyable and
 * non-movable — factories return unnamed temporaries, so guaranteed
 * copy elision (C++17) covers `auto s = ScopedSpanGuard::freshRoot(...)`.
 *
 * When the span must outlive the current thread (e.g. handed to a job
 * queue), convert to a plain SpanGuard with `operator SpanGuard() &&`:
 * that pops the Scope eagerly on the origin thread and yields a
 * thread-free guard.
 *
 * ScopedSpanGuard dependency diagram:
 *
 *     +--------------------------------------------+
 *     |              ScopedSpanGuard               |
 *     |            (scoped, thread-bound)          |
 *     +--------------------------------------------+
 *     | - impl_ : unique_ptr<ScopedImpl>  (pimpl)  |
 *     +--------------------------------------------+
 *     | + (cat, prefix, name)             [ctor]   |
 *     | + freshRoot(cat, prefix, name)    [static] |
 *     | + childSpan(name) : ScopedSpanGuard        |
 *     | + linkedSpan(name) : ScopedSpanGuard       |
 *     | + operator SpanGuard() &&    (handoff)     |
 *     | + setAttribute / setOk / setError / ...    |
 *     | + spanContext() / discard()                |
 *     | + operator bool()                          |
 *     +--------------------------------------------+
 *                     |  hides (pimpl)
 *              +------+--------------------+
 *              |                           |
 *         +----------+     +-----------------------------+
 *         | SpanGuard|     |     optional<Scope>         |
 *         |  (span)  |     | (OTel, thread-bound;        |
 *         |          |     |  present : span active)     |
 *         +----------+     +-----------------------------+
 *
 * Usage examples:
 *
 * 1. Same-thread scoped tracing (child inherits parent automatically):
 * @code
 *     using namespace xrpl::telemetry;
 *
 *     ScopedSpanGuard span(
 *         TraceCategory::Rpc, rpc_span::prefix::command, commandName);
 *     span.setAttribute(rpc_span::attr::command, commandName);
 *     // childSpan parents to `span` because it is active on this thread
 *     auto child = span.childSpan(rpc_span::op::dispatch);
 * @endcode
 *
 * 2. Capture on this thread, hand off to another (edge case):
 * @code
 *     using namespace xrpl::telemetry;
 *
 *     auto scoped = ScopedSpanGuard::freshRoot(
 *         TraceCategory::Ledger, seg::ledger, ledger_span::op::build);
 *     // Pop the scope now, on this thread, and move a thread-free
 *     // SpanGuard into the job.
 *     SpanGuard span = std::move(scoped);
 *     jobQueue.addJob([g = std::move(span)]() mutable { doWork(); });
 * @endcode
 *
 * @note Thread safety: A ScopedSpanGuard must be constructed AND
 * destroyed on the same thread — its Scope binds to that thread's
 * context stack. `operator SpanGuard() &&` and the destructor must
 * both run on the owning thread; both check this with an XRPL_ASSERT
 * in debug/test/fuzzing builds. `operator SpanGuard() &&` pops the
 * scope eagerly, so the resulting SpanGuard is thread-free.
 *
 * @note Known limitations:
 * - Non-movable: cannot be stored in a container that relocates, and
 *   cannot be returned then move-assigned into an existing variable.
 *   Store the thread-free SpanGuard (via `operator SpanGuard() &&`)
 *   instead when a movable handle is needed.
 * - Same attribute-removal limitation as SpanGuard.
 */
class ScopedSpanGuard
{
    struct ScopedImpl;
    std::unique_ptr<ScopedImpl> impl_;

    explicit ScopedSpanGuard(SpanGuard&& guard);

public:
    /**
     * Create a scoped span guarded by a TraceCategory flag and activate
     * it on this thread. Equivalent to SpanGuard::span() wrapped in an
     * active Scope. The span name is built as "prefix.name".
     * @param cat     Trace subsystem category.
     * @param prefix  Span name prefix (e.g. "rpc.command").
     * @param name    Span name suffix (e.g. "submit").
     */
    ScopedSpanGuard(TraceCategory cat, std::string_view prefix, std::string_view name);

    ~ScopedSpanGuard();

    ScopedSpanGuard(ScopedSpanGuard&&) = delete;
    ScopedSpanGuard&
    operator=(ScopedSpanGuard&&) = delete;
    ScopedSpanGuard(ScopedSpanGuard const&) = delete;
    ScopedSpanGuard&
    operator=(ScopedSpanGuard const&) = delete;

    // --- Static factory methods ----------------------------------------

    /**
     * Create a scoped span that always starts a fresh trace root and
     * activate it on this thread. See SpanGuard::freshRoot().
     * @param cat     Trace subsystem category.
     * @param prefix  Span name prefix.
     * @param name    Span name suffix.
     * @return An active scoped root-span guard, or a null one if disabled.
     */
    [[nodiscard]] static ScopedSpanGuard
    freshRoot(TraceCategory cat, std::string_view prefix, std::string_view name);

    // --- Child / linked span creation ----------------------------------

    /**
     * Create a scoped child span parented to this guard's active span
     * and activate it on this thread.
     * @param name  Span name for the child.
     * @return A new scoped guard, or a null one if this guard is inactive.
     */
    [[nodiscard]] ScopedSpanGuard
    childSpan(std::string_view name) const;

    /**
     * Create a scoped child span parented to an explicit captured
     * context and activate it on this thread.
     * @param name       Span name for the child.
     * @param parentCtx  Context captured via spanContext().
     * @return A new scoped guard, or a null one if parentCtx is invalid.
     */
    [[nodiscard]] static ScopedSpanGuard
    childSpan(std::string_view name, SpanContext const& parentCtx);

    /**
     * Create a scoped span linked (follows-from) to this guard's span
     * and activate it on this thread.
     * @param name  Span name for the linked span.
     * @return A new scoped guard, or a null one if this guard is inactive.
     */
    [[nodiscard]] ScopedSpanGuard
    linkedSpan(std::string_view name) const;

    /**
     * Create a scoped span linked to an explicit captured context and
     * activate it on this thread.
     * @param name     Span name for the linked span.
     * @param linkCtx  Context to link from.
     * @return A new scoped guard, or a null one if linkCtx is invalid.
     */
    [[nodiscard]] static ScopedSpanGuard
    linkedSpan(std::string_view name, SpanContext const& linkCtx);

    // --- Handoff bridge -------------------------------------------------

    /**
     * Pop the active Scope on the origin thread and yield the span as a
     * thread-free SpanGuard. Use to move a span into a job or onto
     * another thread. Must be called on the constructing thread.
     * @return The unscoped SpanGuard that now owns the span.
     */
    operator SpanGuard() &&;

    // --- Context capture -----------------------------------------------

    /**
     * Return a SpanContext referring to the owned guard's own span, for
     * use as an explicit parent in childSpan(name, ctx) across threads.
     * Forwards to the owned SpanGuard's spanContext(). Returns an invalid
     * context if this guard is null.
     * @return A SpanContext holding the owned guard's own span.
     * @note This is NOT the thread's ambient context — use
     * SpanGuard::threadLocalContext() for that.
     */
    [[nodiscard]] SpanContext
    spanContext() const;

    // --- Forwarding methods (delegate to the owned SpanGuard) ----------

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
     * Record an exception as a span event and mark status as error.
     * No-op on a null guard.
     * @param e  The exception to record.
     */
    void
    recordException(std::exception const& e);

    /**
     * Mark this span for discard and end it immediately. discard() pops the
     * thread-local scope right away (on the owning thread) and discards the
     * span. After discard() the guard is inert and its destructor is a no-op.
     */
    void
    discard();

    /**
     * @return true if this guard holds an active span.
     */
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
    operator=(SpanGuard&&) noexcept = default;
    SpanGuard(SpanGuard const&) = delete;
    SpanGuard&
    operator=(SpanGuard const&) = delete;

    [[nodiscard]] static SpanGuard
    span(TraceCategory, std::string_view, std::string_view)
    {
        return {};
    }

    [[nodiscard]] static SpanGuard
    freshRoot(TraceCategory, std::string_view, std::string_view)
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
    spanContext() const
    {
        return {};
    }
    [[nodiscard]] TraceBytes
    getTraceBytes() const
    {
        return {};
    }
    // NOLINTEND(readability-convert-member-functions-to-static)

    [[nodiscard]] static SpanContext
    threadLocalContext()
    {
        return {};
    }

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

class ScopedSpanGuard
{
    // Private default ctor lets the inline factories/child methods build
    // a no-op instance without exposing a public default constructor
    // (mirrors the real class, which has no public default ctor).
    ScopedSpanGuard() = default;

public:
    ScopedSpanGuard(TraceCategory, std::string_view, std::string_view)
    {
    }
    ~ScopedSpanGuard() = default;

    ScopedSpanGuard(ScopedSpanGuard&&) = delete;
    ScopedSpanGuard&
    operator=(ScopedSpanGuard&&) = delete;
    ScopedSpanGuard(ScopedSpanGuard const&) = delete;
    ScopedSpanGuard&
    operator=(ScopedSpanGuard const&) = delete;

    [[nodiscard]] static ScopedSpanGuard
    freshRoot(TraceCategory, std::string_view, std::string_view)
    {
        return {};
    }

    // NOLINTBEGIN(readability-convert-member-functions-to-static)
    [[nodiscard]] ScopedSpanGuard
    childSpan(std::string_view) const
    {
        return {};
    }
    [[nodiscard]] static ScopedSpanGuard
    childSpan(std::string_view, SpanContext const&)
    {
        return {};
    }
    [[nodiscard]] ScopedSpanGuard
    linkedSpan(std::string_view) const
    {
        return {};
    }
    [[nodiscard]] static ScopedSpanGuard
    linkedSpan(std::string_view, SpanContext const&)
    {
        return {};
    }

    [[nodiscard]] SpanContext
    spanContext() const
    {
        return {};
    }
    // NOLINTEND(readability-convert-member-functions-to-static)

    operator SpanGuard() &&
    {
        return {};
    }

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

#endif  // XRPL_ENABLE_TELEMETRY

}  // namespace xrpl::telemetry
