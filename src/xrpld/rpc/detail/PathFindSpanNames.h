#pragma once

/** Compile-time span name constants for PathFind tracing.
 *
 *  Covers the path_find and ripple_path_find RPC handlers, the
 *  PathRequest computation engine, and the Pathfinder graph exploration.
 *
 *  Span hierarchy:
 *
 *    RPC entry (one-shot or subscription):
 *
 *    +-------------------------------------------------------+
 *    | pathfind.request                                      |
 *    | doPathFind() / doRipplePathFind()                     |
 *    |   attrs: source_account, dest_account                 |
 *    |                                                       |
 *    |  +--------------------------------------------------+ |
 *    |  | pathfind.compute                                 | |
 *    |  | PathRequest::doUpdate()                          | |
 *    |  | attrs: fast, search_level                        | |
 *    |  |                                                  | |
 *    |  |  +---------------------+ +--------------------+  | |
 *    |  |  | pathfind.discover   | | pathfind.rank      |  | |
 *    |  |  | Pathfinder::find()  | | computePathRanks() |  | |
 *    |  |  +---------------------+ +--------------------+  | |
 *    |  +--------------------------------------------------+ |
 *    +-------------------------------------------------------+
 *
 *    Async recomputation (ledger close):
 *
 *    +-------------------------------------------------------+
 *    | pathfind.update_all                                   |
 *    | PathRequestManager::updateAll()                       |
 *    |   attrs: ledger_index, num_requests                   |
 *    |                                                       |
 *    |  +--------------------------------------------------+ |
 *    |  | pathfind.compute  (per active request)            | |
 *    |  +--------------------------------------------------+ |
 *    +-------------------------------------------------------+
 */

#include <xrpl/telemetry/SpanNames.h>

namespace xrpl {
namespace telemetry {
namespace pathfind_span {

// ===== Span prefixes =======================================================

namespace prefix {
/// "pathfind" — root prefix for path finding spans.
inline constexpr auto pathfind = makeStr("pathfind");
}  // namespace prefix

// ===== Span operation suffixes =============================================

namespace op {
inline constexpr auto request = makeStr("request");
inline constexpr auto compute = makeStr("compute");
inline constexpr auto updateAll = makeStr("update_all");
inline constexpr auto discover = makeStr("discover");
inline constexpr auto rank = makeStr("rank");
}  // namespace op

// ===== Attribute keys ======================================================

namespace attr {
inline constexpr auto xrplPathfind = join(seg::xrpl, makeStr("pathfind"));

/// "xrpl.pathfind.source_account"
inline constexpr auto sourceAccount = join(xrplPathfind, makeStr("source_account"));
/// "xrpl.pathfind.dest_account"
inline constexpr auto destAccount = join(xrplPathfind, makeStr("dest_account"));
/// "xrpl.pathfind.fast"
inline constexpr auto fast = join(xrplPathfind, makeStr("fast"));
/// "xrpl.pathfind.search_level"
inline constexpr auto searchLevel = join(xrplPathfind, makeStr("search_level"));
/// "xrpl.pathfind.num_complete_paths"
inline constexpr auto numCompletePaths = join(xrplPathfind, makeStr("num_complete_paths"));
/// "xrpl.pathfind.num_paths"
inline constexpr auto numPaths = join(xrplPathfind, makeStr("num_paths"));
/// "xrpl.pathfind.num_requests"
inline constexpr auto numRequests = join(xrplPathfind, makeStr("num_requests"));
/// "xrpl.pathfind.ledger_index"
inline constexpr auto ledgerIndex = join(xrplPathfind, makeStr("ledger_index"));
}  // namespace attr

}  // namespace pathfind_span
}  // namespace telemetry
}  // namespace xrpl
