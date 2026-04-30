#pragma once

/** Compile-time span name constants for peer overlay tracing.
 *
 *  Used by PeerImp for peer message handling spans (proposals,
 *  validations). Built on StaticStr/join() from SpanNames.h.
 *
 *  Span hierarchy:
 *
 *    peer.proposal.receive   (PeerImp — incoming proposal)
 *    peer.validation.receive (PeerImp — incoming validation)
 */

#include <xrpl/telemetry/SpanNames.h>

namespace xrpl::telemetry::peer_span {

// ===== Span operation suffixes ===============================================

namespace op {
inline constexpr auto proposalReceive = makeStr("proposal.receive");
inline constexpr auto validationReceive = makeStr("validation.receive");
}  // namespace op

// ===== Attribute keys ========================================================

namespace attr {
inline constexpr auto xrplPeer = join(seg::xrpl, seg::peer);

/// "xrpl.peer.id"
inline constexpr auto id = join(xrplPeer, makeStr("id"));
/// "xrpl.peer.proposal.trusted"
inline constexpr auto proposalTrusted =
    join(join(xrplPeer, makeStr("proposal")), makeStr("trusted"));

/// "xrpl.peer.validation.ledger_hash"
inline constexpr auto validationLedgerHash =
    join(join(xrplPeer, makeStr("validation")), makeStr("ledger_hash"));
/// "xrpl.peer.validation.full"
inline constexpr auto validationFull = join(join(xrplPeer, makeStr("validation")), makeStr("full"));
/// "xrpl.peer.validation.trusted"
inline constexpr auto validationTrusted =
    join(join(xrplPeer, makeStr("validation")), makeStr("trusted"));
}  // namespace attr

}  // namespace xrpl::telemetry::peer_span
