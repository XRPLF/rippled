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
// The span-id validator and std::uint8_t are named only by the
// telemetry-enabled branches below.
#include <xrpl/telemetry/TraceContextValidation.h>

#include <cstdint>
#endif

namespace xrpl::telemetry {

/**
 * Create a "tx.receive" span for a transaction received from a peer.
 *  trace_id is derived from txID[0:16]. If the incoming message carries
 *  a protobuf TraceContext with a valid span_id, it is used as the
 *  parent to preserve relay ordering.
 */
inline SpanGuard
txReceiveSpan(uint256 const& txID, [[maybe_unused]] protocol::TMTransaction const& msg)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (msg.has_trace_context())
    {
        auto const& tc = msg.trace_context();
        // Only the span_id is taken from the peer here; the trace_id is
        // derived locally from txID, so validate the span_id alone.
        if (tc.has_span_id() && isValidSpanId(tc.span_id()))
        {
            return SpanGuard::hashSpan(
                TraceCategory::Transactions,
                tx_span::receive,
                txID.data(),
                txID.kBytes,
                reinterpret_cast<std::uint8_t const*>(tc.span_id().data()),
                tc.span_id().size(),
                tc.has_trace_flags() ? static_cast<std::uint8_t>(tc.trace_flags())
                                     : std::uint8_t{0});
        }
    }
#endif
    return SpanGuard::hashSpan(
        TraceCategory::Transactions, tx_span::receive, txID.data(), txID.kBytes);
}

/**
 * Create a "tx.process" span for transaction processing in NetworkOPs.
 *  trace_id is derived from txID[0:16].
 */
inline SpanGuard
txProcessSpan(uint256 const& txID)
{
    return SpanGuard::hashSpan(
        TraceCategory::Transactions, tx_span::process, txID.data(), txID.kBytes);
}

}  // namespace xrpl::telemetry
