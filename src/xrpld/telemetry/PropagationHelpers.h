#pragma once

/**
 * Helpers for injecting trace context into protobuf messages.
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
 *                      ^
 *                      |  delegates, once there is something to write
 *          injectSpanContext(span, message)   <-- preferred entry point
 *
 * @note Prefer the overload that takes the whole message. It is a true
 *  no-op when nothing is recorded, because it decides whether to create
 *  the TraceContext submessage at all. The overload taking a
 *  protocol::TraceContext& cannot be: its caller has already created the
 *  submessage and set its has-bit before this code runs.
 *
 *  Usage:
 *  @code
 *      // Send side — inject from a SpanGuard reference:
 *      protocol::TMTransaction tx;
 *      // ... populate tx fields ...
 *      injectSpanContext(mySpanGuard, tx);
 *      overlay.relay(txID, tx, toSkip);
 *  @endcode
 *
 * @see ConsensusReceiveTracing.h and TxTracing.h for receive-side
 * extraction helpers.
 * @see TraceContextPropagator.h for low-level OTel context serialization.
 */

#include <xrpl/proto/xrpl.pb.h>
#include <xrpl/telemetry/SpanGuard.h>

namespace xrpl::telemetry {

/**
 * Inject trace context from an active SpanGuard into a protobuf
 *  TraceContext message for cross-node propagation.
 *
 *  Reads the span's trace_id, span_id, and trace_flags via
 *  getTraceBytes() and writes them into the protobuf fields.
 *  Safe to call from any thread that holds a reference to the span.
 *  No-op if the span is null or inactive.
 *
 * @param span   The active SpanGuard whose context to propagate.
 * @param proto  The protobuf TraceContext to populate.
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

/**
 * Inject an active span's trace context into a message that carries an
 * optional TraceContext submessage.
 *
 *  Takes the parent message rather than the submessage so the decision to
 *  create the submessage stays here. `mutable_trace_context()` on a protobuf
 *  optional field allocates the submessage and sets its has-bit, so a caller
 *  that passes `*msg.mutable_trace_context()` puts an empty TraceContext on
 *  the wire whenever nothing is recorded, and makes receiving peers take
 *  their has_trace_context() branch for nothing.
 *
 * @param span  The span whose context to propagate; may be inactive.
 * @param msg   The message to populate. Untouched when nothing is recorded.
 */
template <class Message>
void
injectSpanContext(SpanGuard const& span, Message& msg)
{
    auto const bytes = span.getTraceBytes();
    if (!bytes.valid)
        return;

    injectSpanContext(span, *msg.mutable_trace_context());
}

}  // namespace xrpl::telemetry
