#pragma once

/**
 * Helper functions for creating transaction trace spans.
 *
 *  Encapsulates the logic for creating SpanGuard instances with
 *  hash-derived trace IDs and optional protobuf parent extraction.
 *  Call sites in PeerImp and NetworkOPs stay simple one-liners.
 *
 *  When XRPL_ENABLE_TELEMETRY is not defined, the functions return
 *  no-op SpanGuard instances (zero overhead, zero dependencies).
 */

#include <xrpld/telemetry/TxSpanNames.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/proto/xrpl.pb.h>
#include <xrpl/telemetry/SpanGuard.h>

#ifdef XRPL_ENABLE_TELEMETRY
// The trace-context helpers, std::span and std::uint8_t are named only by
// the telemetry-enabled branches below.
#include <xrpl/telemetry/TraceContextValidation.h>

#include <cstdint>
#include <span>
#endif

namespace xrpl::telemetry {

/**
 * Create a "tx.receive" span for a transaction received from a peer.
 *
 * The trace_id is txID[0:16]. The sender's span becomes the parent when
 * the message's TraceContext carries a valid span_id and this same
 * trace_id. A parent and its child share one trace. A context that names
 * another trace leaves the span a root in the txID trace.
 *
 * @param txID  Transaction id; its first 16 bytes become the trace_id.
 * @param msg   The received message, read only for its trace context.
 * @return An active guard, or a null guard when the Transactions category
 * is disabled. Bind it: a discarded guard ends the span immediately.
 */
[[nodiscard]] inline SpanGuard
txReceiveSpan(uint256 const& txID, [[maybe_unused]] protocol::TMTransaction const& msg)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (msg.has_trace_context())
    {
        auto const& tc = msg.trace_context();
        // The parent comes from the peer only when the peer's trace_id is
        // txID[0:16]. A message from the parser is already clean (both ids
        // valid). The span_id check covers a message built elsewhere.
        auto const txTraceId = std::as_bytes(std::span(txID)).first<kTraceIdSize>();
        if (tc.has_span_id() && isValidSpanId(tc.span_id()) &&
            isSameTraceId(tc.trace_id(), txTraceId))
        {
            return SpanGuard::hashSpan(
                TraceCategory::Transactions,
                tx_span::receive,
                txID.data(),
                txID.kBytes,
                reinterpret_cast<std::uint8_t const*>(tc.span_id().data()),
                tc.span_id().size(),
                traceFlagsByte(tc));
        }
    }
#endif
    return SpanGuard::hashSpan(
        TraceCategory::Transactions, tx_span::receive, txID.data(), txID.kBytes);
}

/**
 * Create a "tx.process" span for transaction processing in NetworkOPs.
 *  trace_id is derived from txID[0:16].
 * @param txID  Transaction id; its first 16 bytes become the trace_id.
 * @return An active guard, or a null guard when the Transactions category
 * is disabled. Bind it: a discarded guard ends the span immediately.
 */
[[nodiscard]] inline SpanGuard
txProcessSpan(uint256 const& txID)
{
    return SpanGuard::hashSpan(
        TraceCategory::Transactions, tx_span::process, txID.data(), txID.kBytes);
}

}  // namespace xrpl::telemetry
