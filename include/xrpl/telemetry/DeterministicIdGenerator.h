#pragma once

#ifdef XRPL_ENABLE_TELEMETRY

#include <opentelemetry/sdk/trace/id_generator.h>

#include <array>
#include <cstdint>
#include <memory>

namespace xrpl::telemetry {

/**
 * OTel IdGenerator that can mint a deterministic (hash-derived) trace_id.
 *
 * By default the OTel SDK generates random trace_ids, so a span derived from
 * a stable hash (e.g. a transaction id) cannot become a real trace root: the
 * SDK either inherits the active span as parent or invents a random root id.
 * This generator lets a caller pin the trace_id of the next forced-root span
 * to a chosen 16-byte value, so hash-derived spans line up across nodes into
 * one trace. When no value is pinned it behaves exactly like the default
 * random generator.
 *
 * The pinned value lives in a thread-local slot set by PendingTraceId (below).
 * Only GenerateTraceId() consults that slot, and only on the SDK's no-parent
 * (root) branch; span_ids are always random. is_random_ is false so the W3C
 * random-trace-id flag is not set on these deterministic ids.
 *
 * Dependency / data-flow diagram:
 *
 *   +-----------------------------------------------------------+
 *   |               DeterministicIdGenerator                    |
 *   |                    (IdGenerator)                          |
 *   +-----------------------------------------------------------+
 *   | - random_ : unique_ptr<IdGenerator>   (random delegate)   |
 *   +-----------------------------------------------------------+
 *   | GenerateTraceId():                                        |
 *   |     thread-local pending id set? --yes--> return that id  |
 *   |                                  --no --> random_ ...     |
 *   | GenerateSpanId():  always ---------------> random_ ...    |
 *   +-----------------------------------------------------------+
 *             ^                                    |
 *   sets/clears|                          delegates|
 *              |                                    v
 *      +----------------+                +--------------------+
 *      | PendingTraceId |                | RandomIdGenerator  |
 *      |  (RAII guard)  |                |     (delegate)     |
 *      +----------------+                +--------------------+
 *
 * @note Thread safety: the pending trace_id is a file-local thread_local, so
 * each thread sees only its own pinned value and no synchronization is needed.
 * The pending id is consumed ONLY by GenerateTraceId(), which the SDK calls
 * solely when a span has no valid parent; GenerateSpanId() never reads it and
 * is always random.
 * @note Limitation: minting a deterministic root only works when the caller
 * forces the SDK's root branch (e.g. by starting the span with a
 * Context{kIsRootSpanKey, true}); otherwise the SDK reuses the parent's
 * trace_id and never calls GenerateTraceId(). The primary such caller is
 * SpanGuard::hashSpan(), which forces the root branch to mint deterministic
 * per-object trace roots.
 *
 * Example 1 - primary use, inside a forced-root hash span (shown as
 * pseudocode):
 * @code
 * // std::array<std::uint8_t, 16> id = deriveTraceIdFromHash(txHash);
 * // PendingTraceId const pending{id};           // pin id for this thread
 * // auto root = Context{kIsRootSpanKey, true};  // force the no-parent branch
 * // auto guard = telemetry.startSpan("tx.process", root);
 * // // GenerateTraceId() returns `id`; the guard's trace_id == id.
 * @endcode
 *
 * Example 2 - edge case, a normal child span never consults the pending id:
 * @code
 * // With an active parent span, startSpan() inherits the parent's trace_id
 * // and the SDK does NOT call GenerateTraceId(), so no PendingTraceId is used.
 * // auto child = parentGuard.childSpan("subtask");  // random/parent trace_id
 * @endcode
 */
class DeterministicIdGenerator final : public opentelemetry::sdk::trace::IdGenerator
{
    /**
     * Random generator the deterministic path falls back to. Used for every
     * span_id and for any trace_id when no PendingTraceId is active.
     */
    std::unique_ptr<opentelemetry::sdk::trace::IdGenerator> random_;

public:
    /**
     * Build a generator with is_random_ = false and a random delegate.
     */
    DeterministicIdGenerator();

    /**
     * @return the thread's pending trace_id if a PendingTraceId is active,
     * otherwise a fresh random trace_id from the delegate. Consuming the
     * pending id clears it so it applies to exactly one root span.
     */
    opentelemetry::trace::TraceId
    GenerateTraceId() noexcept override;

    /**
     * @return a fresh random span_id. Never uses the pending trace_id.
     */
    opentelemetry::trace::SpanId
    GenerateSpanId() noexcept override;
};

/**
 * RAII guard that pins a deterministic trace_id for the next forced-root span
 * started on this thread.
 *
 * Mirrors DiscardScope: it sets a thread-local pending trace_id on
 * construction and clears it on destruction, so the pinned id stays confined
 * to the guard's scope and cannot leak onto a later span. On destruction it
 * asserts that the id was actually consumed by GenerateTraceId() — if it was
 * not, the SDK took a branch other than the root branch the caller intended,
 * which is a bug worth catching in debug/test builds.
 *
 * Wrap ONLY the single forced-root startSpan() call that must receive the
 * deterministic id. Non-copyable and non-movable: its sole purpose is the
 * scoped lifetime of the pending id.
 *
 * @note Thread safety: the pending id is thread-local, so a guard on one
 * thread never affects another. Construct and destroy the guard on the same
 * thread as the startSpan() call it wraps.
 * @note Limitation: the consumed-assert only holds when the wrapped span is
 * started on the SDK root branch (Context{kIsRootSpanKey, true}); wrapping a
 * child span would leave the id unconsumed and trip the assert. Nesting two
 * guards on one thread is unsupported: the inner guard consumes/clears first,
 * so the outer id is silently dropped and its destructor trips the assert.
 *
 * Example 1 - primary use, wrapping a forced-root span start (pseudocode):
 * @code
 * // {
 * //   PendingTraceId const pending{id};            // pin for this scope
 * //   auto root = Context{kIsRootSpanKey, true};
 * //   auto guard = telemetry.startSpan(name, root);// consumes the pinned id
 * // }  // ~PendingTraceId asserts the id was consumed, then clears it
 * @endcode
 *
 * Example 2 - edge case, misuse detection: wrapping a non-root span leaves the
 * id unconsumed, so ~PendingTraceId trips XRPL_ASSERT in debug/test builds.
 */
class PendingTraceId
{
public:
    /**
     * Pin @p id as the pending trace_id for this thread.
     * @param id The 16-byte trace_id the next forced-root span should adopt.
     */
    explicit PendingTraceId(std::array<std::uint8_t, 16> const& id) noexcept;

    /**
     * Assert the pinned id was consumed by a root span, then clear it so it
     * never leaks onto the next span (even in release, where the assert is a
     * no-op).
     */
    ~PendingTraceId() noexcept;

    PendingTraceId(PendingTraceId const&) = delete;
    PendingTraceId&
    operator=(PendingTraceId const&) = delete;
    PendingTraceId(PendingTraceId&&) = delete;
    PendingTraceId&
    operator=(PendingTraceId&&) = delete;
};

}  // namespace xrpl::telemetry

#endif  // XRPL_ENABLE_TELEMETRY
