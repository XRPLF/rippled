#pragma once

/**
 * Helper functions for creating consensus receive trace spans.
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
 *      SpanGuard::hashSpan() with      SpanGuard::rootSpan()
 *      extracted parent context       (fresh trace root)
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
 * @note Span names come from the canonical constants in
 *  ConsensusSpanNames.h (consensus::span::proposalReceive /
 *  validationReceive) so they stay in sync with the rest of Phase 4.
 */

#include <xrpld/consensus/ConsensusSpanNames.h>

#include <xrpl/proto/xrpl.pb.h>
#include <xrpl/telemetry/SpanGuard.h>
#include <xrpl/telemetry/SpanNames.h>
#include <xrpl/telemetry/TraceContextValidation.h>

#include <cstdint>

namespace xrpl::telemetry {

/**
 * Create a "consensus.proposal.receive" span for an incoming proposal.
 *
 *  If the message carries a TraceContext with a valid span_id, the
 *  receive span is created with the sender's context as parent.
 *  Otherwise a standalone span is created.
 *
 * @param msg The incoming TMProposeSet protobuf message.
 * @return An active SpanGuard, or a null guard if tracing is disabled.
 */
inline SpanGuard
proposalReceiveSpan([[maybe_unused]] protocol::TMProposeSet const& msg)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (msg.has_trace_context())
    {
        auto const& tc = msg.trace_context();
        // Reject malformed or all-zero ids from the peer before trusting
        // them as a parent. See TraceContextValidation.h.
        if (isValidTraceContext(tc))
        {
            // Create a child span using the sender's trace_id and
            // span_id as parent. Use hashSpan with the sender's
            // trace_id so the receiving span shares the same trace.
            return SpanGuard::hashSpan(
                TraceCategory::Consensus,
                consensus::span::proposalReceive,
                reinterpret_cast<std::uint8_t const*>(tc.trace_id().data()),
                tc.trace_id().size(),
                reinterpret_cast<std::uint8_t const*>(tc.span_id().data()),
                tc.span_id().size(),
                tc.has_trace_flags() ? static_cast<std::uint8_t>(tc.trace_flags())
                                     : std::uint8_t{0});
        }
    }
#endif
    // No propagated context — start a fresh trace root (not an ambient child).
    return SpanGuard::rootSpan(
        TraceCategory::Consensus, seg::consensus, consensus::span::op::proposalReceive);
}

/**
 * Create a "consensus.validation.receive" span for an incoming validation.
 *
 *  If the message carries a TraceContext with a valid span_id, the
 *  receive span is created with the sender's context as parent.
 *  Otherwise a standalone span is created.
 *
 * @param msg The incoming TMValidation protobuf message.
 * @return An active SpanGuard, or a null guard if tracing is disabled.
 */
inline SpanGuard
validationReceiveSpan([[maybe_unused]] protocol::TMValidation const& msg)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (msg.has_trace_context())
    {
        auto const& tc = msg.trace_context();
        // Reject malformed or all-zero ids from the peer before trusting
        // them as a parent. See TraceContextValidation.h.
        if (isValidTraceContext(tc))
        {
            return SpanGuard::hashSpan(
                TraceCategory::Consensus,
                consensus::span::validationReceive,
                reinterpret_cast<std::uint8_t const*>(tc.trace_id().data()),
                tc.trace_id().size(),
                reinterpret_cast<std::uint8_t const*>(tc.span_id().data()),
                tc.span_id().size(),
                tc.has_trace_flags() ? static_cast<std::uint8_t>(tc.trace_flags())
                                     : std::uint8_t{0});
        }
    }
#endif
    // No propagated context — start a fresh trace root (not an ambient child).
    return SpanGuard::rootSpan(
        TraceCategory::Consensus, seg::consensus, consensus::span::op::validationReceive);
}

}  // namespace xrpl::telemetry
