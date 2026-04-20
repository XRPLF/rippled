#pragma once

/** Compile-time span name constants for transaction tracing.
 *
 *  Used by PeerImp (overlay) and NetworkOPs (app) for transaction
 *  lifecycle spans. Built on StaticStr/join() from SpanNames.h.
 *
 *  Span hierarchy:
 *
 *    Node A (sender)                 Node B (receiver)
 *    +------------------+            +------------------+
 *    | tx.process       |  protobuf  | tx.receive       |
 *    |   injectTo       | ---------> |   extractFrom    |
 *    |   Protobuf()     | trace_ctx  |   Protobuf()     |
 *    +------------------+            +------------------+
 */

#include <xrpl/telemetry/SpanNames.h>

namespace xrpl {
namespace telemetry {
namespace tx_span {

// ===== Span prefixes =======================================================

namespace prefix {
/// "tx" — root prefix for transaction lifecycle spans.
inline constexpr auto tx = seg::tx;
}  // namespace prefix

// ===== Span operation suffixes =============================================

namespace op {
inline constexpr auto receive = makeStr("receive");
inline constexpr auto process = makeStr("process");
}  // namespace op

// ===== Attribute keys ======================================================

namespace attr {
inline constexpr auto xrplTx = join(seg::xrpl, seg::tx);

/// "xrpl.tx.hash"
inline constexpr auto hash = join(xrplTx, makeStr("hash"));
/// "xrpl.tx.local"
inline constexpr auto local = join(xrplTx, makeStr("local"));
/// "xrpl.tx.path"
inline constexpr auto path = join(xrplTx, makeStr("path"));
/// "xrpl.tx.suppressed"
inline constexpr auto suppressed = join(xrplTx, makeStr("suppressed"));
/// "xrpl.tx.status"
inline constexpr auto status = join(xrplTx, makeStr("status"));

inline constexpr auto xrplPeer = join(seg::xrpl, seg::peer);

/// "xrpl.peer.id"
inline constexpr auto peerId = join(xrplPeer, makeStr("id"));
/// "xrpl.peer.version"
inline constexpr auto peerVersion = join(xrplPeer, makeStr("version"));
}  // namespace attr

// ===== Attribute values ====================================================

namespace val {
inline constexpr auto sync = makeStr("sync");
inline constexpr auto async = makeStr("async");
inline constexpr auto knownBad = makeStr("known_bad");
}  // namespace val

}  // namespace tx_span
}  // namespace telemetry
}  // namespace xrpl
