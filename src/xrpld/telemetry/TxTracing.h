#pragma once

/** Helper functions for creating transaction trace spans.
 *
 *  Encapsulates the logic for creating SpanGuard instances with
 *  hash-derived trace IDs and optional protobuf parent extraction.
 *  Call sites in PeerImp and NetworkOPs stay simple one-liners.
 *
 *  When XRPL_ENABLE_TELEMETRY is not defined, the functions return
 *  no-op SpanGuard instances (zero overhead, zero dependencies).
 */

#include <xrpld/app/misc/TxSpanNames.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/proto/xrpl.pb.h>
#include <xrpl/telemetry/SpanGuard.h>

namespace xrpl {
namespace telemetry {

/** Create a "tx.receive" span for a transaction received from a peer.
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
        if (tc.has_span_id() && tc.span_id().size() == 8)
        {
            return SpanGuard::txSpan(
                tx_span::prefix::tx,
                tx_span::op::receive,
                txID.data(),
                txID.bytes,
                reinterpret_cast<std::uint8_t const*>(tc.span_id().data()),
                tc.span_id().size(),
                tc.has_trace_flags() ? static_cast<std::uint8_t>(tc.trace_flags())
                                     : std::uint8_t{0});
        }
    }
#endif
    return SpanGuard::txSpan(tx_span::prefix::tx, tx_span::op::receive, txID.data(), txID.bytes);
}

/** Create a "tx.process" span for transaction processing in NetworkOPs.
 *  trace_id is derived from txID[0:16].
 */
inline SpanGuard
txProcessSpan(uint256 const& txID)
{
    return SpanGuard::txSpan(tx_span::prefix::tx, tx_span::op::process, txID.data(), txID.bytes);
}

}  // namespace telemetry
}  // namespace xrpl
