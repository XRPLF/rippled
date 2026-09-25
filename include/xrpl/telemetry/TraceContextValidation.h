#pragma once

/**
 * Validation and clean-up of peer-supplied trace context.
 *
 * A protobuf TraceContext arrives inside untrusted peer messages
 * (TMTransaction, TMProposeSet, TMValidation). A peer can send a malformed
 * or all-zero trace_id/span_id, or trace_flags that do not fit 8 bits,
 * either by accident or to pollute traces. This header is the single place
 * those checks live, so every receive site agrees on what "valid" means.
 *
 * Validity follows the OpenTelemetry spec and W3C Trace Context: a trace_id
 * is 16 bytes, a span_id is 8 bytes, an all-zero id is invalid, and
 * trace_flags is an 8-bit field in which only the sampled and random bits
 * are defined.
 *
 * Dependency diagram:
 *
 *     P2P message parsed (ProtocolMessage.h)
 *              |
 *              v
 *     sanitizeTraceContext() --- invalid ---> trace_context cleared
 *              |
 *            valid (undefined flag bits zeroed)
 *              |
 *              v
 *     receive sites: isValidTraceContext() / isValidSpanId()
 *              |
 *              +--- valid ----> build child span from peer context
 *              |
 *              +--- invalid --> ignore peer context, start fresh span
 *
 * This header depends only on the generated protobuf type. It pulls in no
 * OpenTelemetry headers, so the parse-time check also runs in builds
 * without telemetry, and receive sites that use SpanGuard keep SpanGuard's
 * encapsulation of OTel types.
 *
 * @note Thread-safe: the predicates are pure, and sanitizeTraceContext()
 * changes only the message it is given.
 *
 * Usage:
 * @code
 *     // Once per parsed message, before any handler sees it:
 *     sanitizeTraceContext(*msg);
 *
 *     // Full parent context (both ids come from the peer):
 *     if (isValidTraceContext(msg->trace_context()))
 *         buildChildSpan(...);
 *
 *     // Span-only (trace_id is derived locally, not from the peer):
 *     if (tc.has_span_id() && isValidSpanId(tc.span_id()))
 *         buildChildSpan(..., traceFlagsByte(tc));
 * @endcode
 */

#include <xrpl/proto/xrpl.pb.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace xrpl::telemetry {

/**
 * Size in bytes of a trace_id.
 */
inline constexpr std::size_t kTraceIdSize = 16;

/**
 * Size in bytes of a span_id.
 */
inline constexpr std::size_t kSpanIdSize = 8;

/**
 * Largest valid trace_flags value. W3C trace-flags is an 8-bit field.
 */
inline constexpr std::uint32_t kMaxTraceFlags = std::numeric_limits<std::uint8_t>::max();

/**
 * The trace_flags bits kept from a peer: sampled (bit 0) and random
 * (bit 1). W3C Trace Context says the other bits must be zero.
 */
inline constexpr std::uint32_t kKnownTraceFlags = 0x03;

/**
 * True if the bytes are a valid OTel trace_id: kTraceIdSize bytes, not all
 * zero.
 *
 * @param traceId The raw trace_id bytes from a protobuf TraceContext.
 * @return true if usable as a trace identifier, false otherwise.
 */
[[nodiscard]] inline bool
isValidTraceId(std::string_view traceId)
{
    return traceId.size() == kTraceIdSize &&
        std::ranges::any_of(traceId, [](char c) { return c != 0; });
}

/**
 * True if the bytes are a valid OTel span_id: kSpanIdSize bytes, not all
 * zero.
 *
 * @param spanId The raw span_id bytes from a protobuf TraceContext.
 * @return true if usable as a span identifier, false otherwise.
 */
[[nodiscard]] inline bool
isValidSpanId(std::string_view spanId)
{
    return spanId.size() == kSpanIdSize &&
        std::ranges::any_of(spanId, [](char c) { return c != 0; });
}

/**
 * True if trace_flags is absent or fits the 8-bit W3C field.
 *
 * @param tc The protobuf TraceContext received from a peer.
 * @return false only when trace_flags is set above kMaxTraceFlags.
 */
[[nodiscard]] inline bool
isValidTraceFlags(protocol::TraceContext const& tc)
{
    return !tc.has_trace_flags() || tc.trace_flags() <= kMaxTraceFlags;
}

/**
 * True if the context carries a usable parent: a valid trace_id, a valid
 * span_id, and trace_flags that fit 8 bits.
 *
 * Use this where both ids are taken from the peer (consensus receive,
 * generic extraction). The transaction path derives its trace_id locally
 * from the txID, so it checks isValidSpanId() alone instead.
 *
 * @param tc The protobuf TraceContext received from a peer.
 * @return true if both ids are present and valid and the flags fit, false
 * otherwise.
 */
[[nodiscard]] inline bool
isValidTraceContext(protocol::TraceContext const& tc)
{
    return tc.has_trace_id() && isValidTraceId(tc.trace_id()) && tc.has_span_id() &&
        isValidSpanId(tc.span_id()) && isValidTraceFlags(tc);
}

/**
 * The trace_flags byte to pass to OpenTelemetry, with undefined bits
 * cleared.
 *
 * @param tc The protobuf TraceContext received from a peer.
 * @return trace_flags masked with kKnownTraceFlags, or 0 when absent.
 */
[[nodiscard]] inline std::uint8_t
traceFlagsByte(protocol::TraceContext const& tc)
{
    if (!tc.has_trace_flags())
        return 0;
    return static_cast<std::uint8_t>(tc.trace_flags() & kKnownTraceFlags);
}

/**
 * A protobuf message with an optional trace_context field.
 */
template <class Message>
concept HasTraceContext = requires(Message& msg) {
    msg.has_trace_context();
    msg.trace_context();
    msg.clear_trace_context();
    msg.mutable_trace_context();
};

/**
 * Clean the trace context of a message received from a peer.
 *
 * Drops the whole context when isValidTraceContext() rejects it, and
 * zeroes the undefined trace_flags bits otherwise. Call it once, when the
 * message is parsed, so every handler and every relay of the message sees
 * the cleaned context.
 *
 * @param msg A message just parsed from a peer. A message without a
 * trace_context is left without one.
 */
template <HasTraceContext Message>
void
sanitizeTraceContext(Message& msg)
{
    if (!msg.has_trace_context())
        return;

    auto const& tc = msg.trace_context();
    if (!isValidTraceContext(tc))
    {
        msg.clear_trace_context();
        return;
    }

    if (tc.has_trace_flags())
        msg.mutable_trace_context()->set_trace_flags(tc.trace_flags() & kKnownTraceFlags);
}

/**
 * Clean the trace context of every transaction in a batch.
 *
 * @param batch A TMTransactions message just parsed from a peer.
 */
inline void
sanitizeTraceContext(protocol::TMTransactions& batch)
{
    for (auto& tx : *batch.mutable_transactions())
        sanitizeTraceContext(tx);
}

}  // namespace xrpl::telemetry
