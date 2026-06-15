#pragma once

/** Thread-local discard signaling between SpanGuard and the span processor.

    SpanGuard::discard() wants to drop a span without sending it to the
    exporter. The OTel SDK calls SpanProcessor::OnEnd() synchronously on the
    same thread that calls Span::End(), so a thread-local flag set just before
    End() and read inside OnEnd() lets FilteringSpanProcessor drop the span
    before it enters the batch export queue.

    This side-channel avoids inspecting the Recordable's internals (which vary
    by exporter type — SpanData vs OtlpRecordable).

    The raw flag lives in `detail` and is mutated only through DiscardScope, a
    RAII guard that sets it on construction and clears it on destruction. This
    keeps the set/clear lifetime bound to a scope (so the flag cannot leak onto
    the next span even if End() were to throw) and prevents any class which includes this header
    from flipping the flag directly. FilteringSpanProcessor reads it through
    isDiscardingCurrentSpan().

    Kept in a separate header to avoid transitive include bloat: SpanGuard.h
    only needs this signaling, not the full Telemetry.h with BasicConfig/Journal.

    Usage:
    @code
        // In SpanGuard::discard():
        {
            DiscardScope discardScope;  // flag set for this scope only
            span->End();                // OnEnd() runs synchronously, sees flag
        }                               // flag cleared here, unconditionally
    @endcode

    @note Thread safety: the flag is thread-local, so each thread observes only
    its own discard signal — no synchronization is required.

    @see SpanGuard::discard(), FilteringSpanProcessor (Telemetry.cpp)
*/

namespace xrpl::telemetry {

namespace detail {

/** Internal thread-local discard flag. Mutate only via DiscardScope; read
    only via isDiscardingCurrentSpan(). Not intended for direct use. */
inline thread_local bool gTlDiscardCurrentSpan = false;

}  // namespace detail

/** RAII guard that marks the current thread's span for discard.

    Sets the thread-local discard flag on construction and clears it on
    destruction, so a span ended within the guard's scope is dropped by
    FilteringSpanProcessor::OnEnd() while the flag stays confined to that scope.
    Non-copyable and non-movable — its sole purpose is the scoped flag lifetime.
*/
class DiscardScope
{
public:
    DiscardScope() noexcept
    {
        detail::gTlDiscardCurrentSpan = true;
    }

    ~DiscardScope()
    {
        detail::gTlDiscardCurrentSpan = false;
    }

    DiscardScope(DiscardScope const&) = delete;
    DiscardScope&
    operator=(DiscardScope const&) = delete;
    DiscardScope(DiscardScope&&) = delete;
    DiscardScope&
    operator=(DiscardScope&&) = delete;
};

/** @return true if the current thread is inside a DiscardScope, i.e. the span
    ending now should be dropped rather than exported. Read by
    FilteringSpanProcessor::OnEnd(). */
[[nodiscard]] inline bool
isDiscardingCurrentSpan() noexcept
{
    return detail::gTlDiscardCurrentSpan;
}

}  // namespace xrpl::telemetry
