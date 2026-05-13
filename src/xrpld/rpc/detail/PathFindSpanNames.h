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

namespace xrpl::telemetry::pathfind_span {

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
/// "source_account" — originating account for path search.
inline constexpr auto sourceAccount = makeStr("source_account");
/// "dest_account" — destination account.
inline constexpr auto destAccount = makeStr("dest_account");
/// "fast" — whether fast pathfinding mode enabled.
inline constexpr auto fast = makeStr("fast");
/// "search_level" — depth of graph exploration.
inline constexpr auto searchLevel = makeStr("search_level");
/// "num_complete_paths" — complete paths found.
inline constexpr auto numCompletePaths = makeStr("num_complete_paths");
/// "num_paths" — total paths returned.
inline constexpr auto numPaths = makeStr("num_paths");
/// "num_requests" — active path requests.
inline constexpr auto numRequests = makeStr("num_requests");
/// "xrpl.pathfind.ledger_index" — kept qualified (rule 5): pathfind target
/// ledger is distinct from xrpl.ledger.seq.
inline constexpr auto ledgerIndex =
    join(join(seg::xrpl, makeStr("pathfind")), makeStr("ledger_index"));
}  // namespace attr

}  // namespace xrpl::telemetry::pathfind_span
