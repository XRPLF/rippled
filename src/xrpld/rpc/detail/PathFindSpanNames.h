#pragma once

/**
 * Compile-time span name constants for PathFind tracing.
 *
 *  Covers the path_find and ripple_path_find RPC handlers, the
 *  PathRequest computation engine, and the Pathfinder graph exploration.
 *
 *  Span hierarchy:
 *
 *    RPC entry (one-shot or subscription):
 *
 *    +----------------------------------------------------------------+
 *    | pathfind.request                                               |
 *    | doPathFind() / doRipplePathFind()                              |
 *    |   attrs: pathfind_source_account, pathfind_dest_account        |
 *    |          (set when present in request params)                  |
 *    |                                                                |
 *    |  +-----------------------------------------------------------+ |
 *    |  | pathfind.compute                                          | |
 *    |  | PathRequest::doUpdate()                                   | |
 *    |  | attrs: pathfind_fast                                      | |
 *    |  |                                                           | |
 *    |  |  +-----------------------------------------------------+  | |
 *    |  |  | pathfind.discover  (one per RPC call, hoisted above | |
 *    |  |  | the per-source-asset loop in PathRequest::findPaths)| |
 *    |  |  |   attrs: pathfind_search_level, pathfind_num_paths  | |
 *    |  |  +-----------------------------------------------------+ | |
 *    |  +-----------------------------------------------------------+ |
 *    +----------------------------------------------------------------+
 *
 *    Async recomputation (ledger close):
 *
 *    +----------------------------------------------------------------+
 *    | pathfind.update_all                                            |
 *    | PathRequestManager::updateAll()                                |
 *    |   attrs: pathfind_ledger_index, pathfind_num_requests          |
 *    |                                                                |
 *    |  +-----------------------------------------------------------+ |
 *    |  | pathfind.compute  (per active request)                    | |
 *    |  +-----------------------------------------------------------+ |
 *    +----------------------------------------------------------------+
 */

#include <xrpl/telemetry/SpanNames.h>

namespace xrpl::telemetry::pathfind_span {

// ===== Span prefixes =======================================================

namespace prefix {
/**
 * "pathfind" — root prefix for path finding spans.
 */
inline constexpr auto pathfind = makeStr("pathfind");
}  // namespace prefix

// ===== Span operation suffixes =============================================

namespace op {
inline constexpr auto request = makeStr("request");
inline constexpr auto compute = makeStr("compute");
inline constexpr auto updateAll = makeStr("update_all");
inline constexpr auto discover = makeStr("discover");
}  // namespace op

// ===== Attribute keys ======================================================

/**
 * All pathfind attributes are namespaced under `pathfind_*`, in underscore
 * form, per the span attribute naming convention in CONTRIBUTING.md. Avoids
 * collisions with bare keys like `fast` or `num_paths` that other subsystems
 * may introduce.
 */

namespace attr {
/**
 * "pathfind_source_account" — originating account for path search.
 */
inline constexpr auto sourceAccount = makeStr("pathfind_source_account");
/**
 * "pathfind_dest_account" — destination account.
 */
inline constexpr auto destAccount = makeStr("pathfind_dest_account");
/**
 * "pathfind_fast" — whether fast pathfinding mode enabled.
 */
inline constexpr auto fast = makeStr("pathfind_fast");
/**
 * "pathfind_search_level" — depth of graph exploration.
 */
inline constexpr auto searchLevel = makeStr("pathfind_search_level");
/**
 * "pathfind_num_paths" — total paths produced across the per-source-asset
 * loop in PathRequest::findPaths (sum of getBestPaths().size() per asset).
 */
inline constexpr auto numPaths = makeStr("pathfind_num_paths");
/**
 * "pathfind_num_requests" — snapshot size of requests_ at update_all start
 * (may include weak_ptrs that subsequently expire during processing).
 */
inline constexpr auto numRequests = makeStr("pathfind_num_requests");
/**
 * "pathfind_ledger_index" — pathfind target ledger index.
 */
inline constexpr auto ledgerIndex = makeStr("pathfind_ledger_index");
/**
 * "pathfind_dest_currency" — destination currency code.
 */
inline constexpr auto destCurrency = makeStr("pathfind_dest_currency");
/**
 * "pathfind_num_source_assets" — candidate source assets count.
 */
inline constexpr auto numSourceAssets = makeStr("pathfind_num_source_assets");
}  // namespace attr

}  // namespace xrpl::telemetry::pathfind_span
