/** @file
 *  Declares `getCountsJson`, the shared diagnostic snapshot function for the
 *  `get_counts` admin RPC command and the `/crawl` overlay endpoint.
 *
 *  The function is factored out of `GetCounts.cpp` so that non-RPC subsystems
 *  (specifically `OverlayImpl::getServerCounts`) can obtain the same runtime
 *  health payload without depending on `RPC::JsonContext` or the full RPC
 *  dispatch machinery — only on `Application&`.
 */

#pragma once

#include <xrpld/app/main/Application.h>

namespace xrpl {

/** Aggregate runtime diagnostics and cache statistics into a JSON object.
 *
 *  Returns a broad snapshot of the node's internal health, including:
 *  - Live object reference counts from `CountedObjects` (types with fewer
 *    than `minObjectCount` live instances are omitted to reduce noise);
 *  - Relational database disk usage (`dbKBTotal`, `dbKBLedger`,
 *    `dbKBTransaction`) and local transaction queue depth (`local_txs`) — only
 *    present when `config().useTxTables()` is enabled;
 *  - Node store write pressure (`write_load`) and historical ledger fetch
 *    rate (`historical_perminute`);
 *  - Cache hit rates for SLEs, the ledger cache, and `AcceptedLedger`, plus
 *    `NodeFamily` SHAMap tree-node cache sizes;
 *  - Human-readable formatted uptime string;
 *  - Additional counters appended by `NodeStore::Database::getCountsJson`.
 *
 *  This function is the shared implementation used by both the `doGetCounts`
 *  RPC handler (default `minObjectCount` of 10, overridable via `min_count`)
 *  and `OverlayImpl::getServerCounts` (hard-coded threshold of 10) for the
 *  `/crawl` endpoint, without requiring either caller to construct a fake
 *  `RPC::JsonContext`.
 *
 *  @param app            The running application instance.
 *  @param minObjectCount Minimum live-instance threshold for including a
 *      tracked object type in the output. Pass 0 to report all types.
 *  @return JSON object containing the aggregated diagnostic fields.
 *
 *  @note `local_txs` and the `dbKB*` fields are absent when
 *      `config().useTxTables()` is false (e.g. reporting nodes that do not
 *      maintain a transaction database).
 */
json::Value
getCountsJson(Application& app, int minObjectCount);

}  // namespace xrpl
