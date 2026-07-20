#pragma once

/**
 * Thread-local discard signaling between SpanGuard and the span processor.
 *
 * SpanGuard::discard() wants to drop a span without sending it to the
 * exporter. The OTel SDK calls SpanProcessor::OnEnd() synchronously on the
 * same thread that calls Span::End(), so a thread-local flag set just before
 * End() and read inside OnEnd() lets FilteringSpanProcessor drop the span
 * before it enters the batch export queue.
 *
 * This side-channel avoids inspecting the Recordable's internals (which vary
 * by exporter type — SpanData vs OtlpRecordable).
 *
 * The flag is a *private* thread-local member of DiscardScope, mutated only by
 * its constructor and destructor. This gives real access control rather than a
 * naming convention: no code that includes this header can flip the flag
 * directly — it can only enter a DiscardScope (which sets and clears the flag
 * over its own lifetime) and observe the state via DiscardScope::isActive().
 * Binding set/clear to a scope also means the flag cannot leak onto the next
 * span even if End() were to throw.
 *
 * Kept in a separate header to avoid transitive include bloat: SpanGuard.h
 * only needs this signaling, not the full Telemetry.h with BasicConfig/Journal.
 *
 * Usage:
 * @code
 * // In SpanGuard::discard():
 * {
 * DiscardScope discardScope;  // flag set for this scope only
 * span->End();                // OnEnd() runs synchronously, sees flag
 * }                               // flag cleared here, unconditionally
 *
 * // In FilteringSpanProcessor::OnEnd():
 * if (DiscardScope::isActive())
 * return;  // drop the span
 * @endcode
 *
 * @note Thread safety: the flag is thread-local, so each thread observes only
 * its own discard signal — no synchronization is required.
 *
 * @see SpanGuard::discard(), FilteringSpanProcessor (Telemetry.cpp)
 */

namespace xrpl::telemetry {

/**
 * RAII guard that marks the current thread's span for discard.
 *
 * Sets the thread-local discard flag on construction and clears it on
 * destruction, so a span ended within the guard's scope is dropped by
 * FilteringSpanProcessor::OnEnd() while the flag stays confined to that scope.
 * The flag is private and mutated only here, so no other code can set it.
 * Non-copyable and non-movable — its sole purpose is the scoped flag lifetime.
 */
class DiscardScope
{
public:
    DiscardScope() noexcept
    {
        discarding = true;
    }

    ~DiscardScope()
    {
        discarding = false;
    }

    DiscardScope(DiscardScope const&) = delete;
    DiscardScope&
    operator=(DiscardScope const&) = delete;
    DiscardScope(DiscardScope&&) = delete;
    DiscardScope&
    operator=(DiscardScope&&) = delete;

    /**
     * @return true if the current thread is inside a DiscardScope, i.e. the
     * span ending now should be dropped rather than exported. Read by
     * FilteringSpanProcessor::OnEnd().
     */
    [[nodiscard]] static bool
    isActive() noexcept
    {
        return discarding;
    }

private:
    /**
     * Thread-local discard flag. Private, so only this class's ctor/dtor can
     * mutate it; observers use isActive(). One instance per thread.
     */
    inline static thread_local bool discarding = false;
};

}  // namespace xrpl::telemetry
