#pragma once

/**
 * Validation predicates for peer-supplied trace context.
 *
 *  A protobuf TraceContext arrives inside untrusted peer messages
 *  (TMTransaction, TMProposeSet, TMValidation). Before a receiving node
 *  builds a span from it, the context must be checked: a peer can send a
 *  malformed or all-zero trace_id/span_id, either by accident or to
 *  pollute traces. This header is the single place those checks live, so
 *  every receive site agrees on what "valid" means.
 *
 *  Validity follows the OpenTelemetry spec: a trace_id is 16 bytes, a
 *  span_id is 8 bytes, and an all-zero id is invalid and must not be
 *  used as a parent.
 *
 *  Dependency diagram:
 *
 *      protocol::TraceContext (untrusted, from peer)
 *               |
 *               v
 *      isValidTraceContext() / isValidSpanId() / isValidTraceId()
 *               |
 *               +--- valid ----> build child span from peer context
 *               |
 *               +--- invalid --> ignore peer context, start fresh span
 *
 *  This header depends only on the generated protobuf type. It pulls in
 *  no OpenTelemetry headers, so receive sites that use SpanGuard keep
 *  SpanGuard's encapsulation of OTel types.
 *
 * @note Thread-safe: all functions are pure and operate only on their
 *  arguments. No shared state.
 *
 *  Usage:
 *  @code
 *      // Full parent context (both ids come from the peer):
 *      if (isValidTraceContext(msg.trace_context()))
 *          buildChildSpan(...);
 *
 *      // Span-only (trace_id is derived locally, not from the peer):
 *      if (tc.has_span_id() && isValidSpanId(tc.span_id()))
 *          buildChildSpan(...);
 *  @endcode
 */

#include <xrpl/proto/xrpl.pb.h>

#include <algorithm>
#include <string>

namespace xrpl::telemetry {

/**
 * True if the bytes are a valid OTel trace_id: 16 bytes, not all-zero.
 *
 * @param traceId The raw trace_id bytes from a protobuf TraceContext.
 * @return true if usable as a trace identifier, false otherwise.
 */
[[nodiscard]] inline bool
isValidTraceId(std::string const& traceId)
{
    return traceId.size() == 16 && std::ranges::any_of(traceId, [](char c) { return c != 0; });
}

/**
 * True if the bytes are a valid OTel span_id: 8 bytes, not all-zero.
 *
 * @param spanId The raw span_id bytes from a protobuf TraceContext.
 * @return true if usable as a span identifier, false otherwise.
 */
[[nodiscard]] inline bool
isValidSpanId(std::string const& spanId)
{
    return spanId.size() == 8 && std::ranges::any_of(spanId, [](char c) { return c != 0; });
}

/**
 * True if the context carries a usable parent: a valid trace_id and a
 *  valid span_id together.
 *
 *  Use this where both ids are taken from the peer (consensus receive,
 *  generic extraction). The transaction path derives its trace_id
 *  locally from the txID, so it checks isValidSpanId() alone instead.
 *
 * @param tc The protobuf TraceContext received from a peer.
 * @return true if both ids are present and valid, false otherwise.
 */
[[nodiscard]] inline bool
isValidTraceContext(protocol::TraceContext const& tc)
{
    return tc.has_trace_id() && isValidTraceId(tc.trace_id()) && tc.has_span_id() &&
        isValidSpanId(tc.span_id());
}

}  // namespace xrpl::telemetry
