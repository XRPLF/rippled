#pragma once

/** Helper functions for creating consensus receive trace spans.
 *
 *  Encapsulates the logic for creating SpanGuard instances for incoming
 *  proposal and validation messages with optional protobuf parent
 *  extraction. When the incoming message carries a TraceContext with a
 *  valid span_id, the receive span is created as a child of the
 *  sender's span, enabling cross-node trace correlation.
 *
 *  Dependency diagram:
 *
 *      protocol::TMProposeSet / TMValidation
 *               |
 *               v
 *      proposalReceiveSpan() / validationReceiveSpan()
 *               |
 *               +--- has trace_context? ----+
 *               |          yes              |  no
 *               v                           v
 *      SpanGuard::span() with          SpanGuard::span()
 *      extracted parent context       (standalone span)
 *
 *  When XRPL_ENABLE_TELEMETRY is not defined, the functions return
 *  no-op SpanGuard instances (zero overhead, zero dependencies).
 *
 *  Usage:
 *  @code
 *      // In PeerImp::onMessage(TMProposeSet):
 *      auto span = telemetry::proposalReceiveSpan(*m);
 *      span.setAttribute(...);
 *  @endcode
 *
 *  @note These span names use inline string_view literals. When
 *  ConsensusSpanNames.h (from Phase 4) is available, callers should
 *  migrate to using the constexpr constants defined there.
 */

#include <xrpl/proto/xrpl.pb.h>
#include <xrpl/telemetry/SpanGuard.h>

namespace xrpl::telemetry {

// Inline span name constants for consensus receive spans.
// Phase 4 will provide these via ConsensusSpanNames.h; these are
// temporary definitions for the propagation infrastructure.
namespace detail {
inline constexpr std::string_view proposalReceiveName = "consensus.proposal.receive";
inline constexpr std::string_view validationReceiveName = "consensus.validation.receive";
}  // namespace detail

/** Create a "consensus.proposal.receive" span for an incoming proposal.
 *
 *  If the message carries a TraceContext with a valid span_id, the
 *  receive span is created with the sender's context as parent.
 *  Otherwise a standalone span is created.
 *
 *  @param msg The incoming TMProposeSet protobuf message.
 *  @return An active SpanGuard, or a null guard if tracing is disabled.
 */
inline SpanGuard
proposalReceiveSpan([[maybe_unused]] protocol::TMProposeSet const& msg)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (msg.has_trace_context())
    {
        auto const& tc = msg.trace_context();
        if (tc.has_span_id() && tc.span_id().size() == 8 && tc.has_trace_id() &&
            tc.trace_id().size() == 16)
        {
            // Create a child span using the sender's trace_id and
            // span_id as parent. Use hashSpan with the sender's
            // trace_id so the receiving span shares the same trace.
            return SpanGuard::hashSpan(
                TraceCategory::Consensus,
                detail::proposalReceiveName,
                reinterpret_cast<std::uint8_t const*>(tc.trace_id().data()),
                tc.trace_id().size(),
                reinterpret_cast<std::uint8_t const*>(tc.span_id().data()),
                tc.span_id().size(),
                tc.has_trace_flags() ? static_cast<std::uint8_t>(tc.trace_flags())
                                     : std::uint8_t{0});
        }
    }
#endif
    // No propagated context — create a standalone span.
    return SpanGuard::span(TraceCategory::Consensus, "consensus", "proposal.receive");
}

/** Create a "consensus.validation.receive" span for an incoming validation.
 *
 *  If the message carries a TraceContext with a valid span_id, the
 *  receive span is created with the sender's context as parent.
 *  Otherwise a standalone span is created.
 *
 *  @param msg The incoming TMValidation protobuf message.
 *  @return An active SpanGuard, or a null guard if tracing is disabled.
 */
inline SpanGuard
validationReceiveSpan([[maybe_unused]] protocol::TMValidation const& msg)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (msg.has_trace_context())
    {
        auto const& tc = msg.trace_context();
        if (tc.has_span_id() && tc.span_id().size() == 8 && tc.has_trace_id() &&
            tc.trace_id().size() == 16)
        {
            return SpanGuard::hashSpan(
                TraceCategory::Consensus,
                detail::validationReceiveName,
                reinterpret_cast<std::uint8_t const*>(tc.trace_id().data()),
                tc.trace_id().size(),
                reinterpret_cast<std::uint8_t const*>(tc.span_id().data()),
                tc.span_id().size(),
                tc.has_trace_flags() ? static_cast<std::uint8_t>(tc.trace_flags())
                                     : std::uint8_t{0});
        }
    }
#endif
    // No propagated context — create a standalone span.
    return SpanGuard::span(TraceCategory::Consensus, "consensus", "validation.receive");
}

}  // namespace xrpl::telemetry
