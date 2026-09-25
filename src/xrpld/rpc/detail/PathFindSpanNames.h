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
 *    pathfind.request ends with status error whenever the handler's reply
 *    carries an rpc error. The description is that error's registry token,
 *    never request text.
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
 *
 * Emitted as the raw r-address, not hashed. An account address is a public
 * ledger identifier drawn from an enumerable set, so an unsalted hash of it
 * is reversible by lookup and protects nothing; it only breaks the join
 * against explorers, RPC responses and logs that show the same address.
 * Only a value that parses as an r-address is emitted, so a malformed or
 * mistaken request value never reaches the span. Do not add redaction here
 * or in a collector processor.
 */
inline constexpr auto sourceAccount = makeStr("pathfind_source_account");
/**
 * "pathfind_dest_account" — destination account.
 *
 * Raw r-address, for the same reason as pathfind_source_account.
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
 * "pathfind_dest_currency" — destination asset as rendered by to_string(Asset):
 * "XRP", "<issuer r-address>/<currency>" for an IOU, or the 48-hex-char
 * issuance id for an MPT (its last 20 bytes are the issuer's account id).
 * The issuer is a public identifier and is not hashed.
 */
inline constexpr auto destCurrency = makeStr("pathfind_dest_currency");
/**
 * "pathfind_num_source_assets" — candidate source assets count.
 */
inline constexpr auto numSourceAssets = makeStr("pathfind_num_source_assets");
}  // namespace attr

}  // namespace xrpl::telemetry::pathfind_span
