#pragma once

/** Helpers for injecting trace context into protobuf messages.
 *
 *  Bridges the gap between SpanGuard (which hides OTel types) and the
 *  protobuf TraceContext message used for cross-node propagation.
 *
 *  Dependency diagram:
 *
 *      SpanGuard::getTraceBytes()    protocol::TraceContext (proto)
 *               \                      /
 *                +--- TraceBytes -----+
 *                |                    |
 *          injectSpanContext(span, proto)
 *
 *  @note When XRPL_ENABLE_TELEMETRY is disabled, getTraceBytes() returns
 *  {.valid=false}, so injectSpanContext becomes a no-op with zero overhead.
 *
 *  Usage:
 *  @code
 *      // Send side — inject from a SpanGuard reference:
 *      protocol::TMTransaction tx;
 *      // ... populate tx fields ...
 *      injectSpanContext(mySpanGuard, *tx.mutable_trace_context());
 *      overlay.relay(txID, tx, toSkip);
 *  @endcode
 *
 *  @see ConsensusReceiveTracing.h for receive-side extraction helpers.
 *  @see TraceContextPropagator.h for low-level OTel context serialization.
 */

#include <xrpl/proto/xrpl.pb.h>
#include <xrpl/telemetry/SpanGuard.h>

namespace xrpl {
namespace telemetry {

/** Inject trace context from an active SpanGuard into a protobuf
 *  TraceContext message for cross-node propagation.
 *
 *  Reads the span's trace_id, span_id, and trace_flags via
 *  getTraceBytes() and writes them into the protobuf fields.
 *  Safe to call from any thread that holds a reference to the span.
 *  No-op if the span is null or inactive.
 *
 *  @param span   The active SpanGuard whose context to propagate.
 *  @param proto  The protobuf TraceContext to populate.
 */
inline void
injectSpanContext(SpanGuard const& span, protocol::TraceContext& proto)
{
    auto const bytes = span.getTraceBytes();
    if (!bytes.valid)
        return;

    proto.set_trace_id(bytes.traceId.data(), bytes.traceId.size());
    proto.set_span_id(bytes.spanId.data(), bytes.spanId.size());
    proto.set_trace_flags(bytes.traceFlags);
}

}  // namespace telemetry
}  // namespace xrpl
