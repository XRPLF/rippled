#pragma once

/** Thread-local flag for span discard signaling.

    SpanGuard::discard() sets tl_discardCurrentSpan to true before calling
    Span::End(). The OTel SDK calls SpanProcessor::OnEnd() synchronously on
    the same thread, so FilteringSpanProcessor checks and clears this flag
    in OnEnd() to drop the span before it enters the batch export queue.

    This side-channel avoids inspecting the Recordable's internals (which
    vary by exporter type — SpanData vs OtlpRecordable).

    Kept in a separate header to avoid transitive include bloat: SpanGuard.h
    only needs this flag, not the full Telemetry.h with BasicConfig/Journal.

    @see SpanGuard::discard(), FilteringSpanProcessor (Telemetry.cpp)
*/

namespace xrpl {
namespace telemetry {

/** When true, the FilteringSpanProcessor drops the current span in
    OnEnd(). Set by SpanGuard::discard(), cleared by OnEnd(). */
inline thread_local bool tl_discardCurrentSpan = false;

}  // namespace telemetry
}  // namespace xrpl
