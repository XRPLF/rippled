/** @file
 *  Implements the `get_counts` admin RPC command.
 *
 *  Provides a single-call snapshot of runtime health: live object reference
 *  counts, relational database disk usage, multiple cache hit rates, node
 *  store write pressure, and formatted uptime.
 */

#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/rdb/backend/SQLiteDatabase.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/basics/UptimeClock.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

#include <chrono>
#include <cstddef>
#include <string>

namespace xrpl {

/** Append one time unit to an uptime string and consume it from the remainder.
 *
 *  Divides `seconds` by `unitVal`, appends the count and unit name to `text`
 *  (with a comma separator if `text` is non-empty and an "s" plural suffix
 *  when count > 1), then subtracts the consumed duration from `seconds`.
 *  Does nothing when the unit count is zero, so units with no contribution
 *  are silently omitted from the output.
 *
 *  @param text     Accumulator string for the formatted uptime result.
 *  @param seconds  Remaining uptime; modified in place by subtracting the
 *      portion consumed by this unit.
 *  @param unitName Singular name of the time unit (e.g. "hour").
 *  @param unitVal  Duration value of one unit (e.g. `std::chrono::hours{1}`).
 *
 *  @note This function is intended to be called in descending unit order
 *      (years → days → hours → minutes → seconds). Each call strips the
 *      largest remaining unit, so later calls never double-count earlier ones.
 */
static void
textTime(
    std::string& text,
    UptimeClock::time_point& seconds,
    char const* unitName,
    std::chrono::seconds unitVal)
{
    auto i = seconds.time_since_epoch() / unitVal;

    if (i == 0)
        return;

    seconds -= unitVal * i;

    if (!text.empty())
        text += ", ";

    text += std::to_string(i);
    text += " ";
    text += unitName;

    if (i > 1)
        text += "s";
}

/** Aggregate runtime diagnostics and cache statistics into a JSON object.
 *
 *  Collects and returns a snapshot of the node's internal state, including:
 *  - Live object reference counts from `CountedObjects` (filtered by
 *    `minObjectCount` to suppress low-population types);
 *  - Relational database disk usage (`dbKBTotal`, `dbKBLedger`,
 *    `dbKBTransaction`) and local transaction queue depth, when
 *    `config().useTxTables()` is enabled;
 *  - Node store write pressure (`write_load`) and historical ledger fetch
 *    rate (`historical_perminute`);
 *  - Cache hit rates for SLEs, the ledger cache, and the `AcceptedLedger`
 *    cache, plus `NodeFamily` SHAMap tree-node cache sizes;
 *  - Formatted human-readable uptime string;
 *  - Additional node store counters appended by
 *    `NodeStore::Database::getCountsJson`.
 *
 *  This function is factored out of the RPC handler so it can be called by
 *  non-RPC paths (e.g. `OverlayImpl::getServerCounts` for the `/crawl`
 *  endpoint) without constructing a fake `RPC::JsonContext`.
 *
 *  @param app            The running application instance.
 *  @param minObjectCount Minimum live-instance threshold for including an
 *      object type in the output. Types with fewer live instances are omitted.
 *  @return JSON object containing the aggregated diagnostic fields.
 */
json::Value
getCountsJson(Application& app, int minObjectCount)
{
    auto objectCounts = CountedObjects::getInstance().getCounts(minObjectCount);

    json::Value ret(json::ValueType::Object);

    for (auto const& [k, v] : objectCounts)
    {
        ret[k] = v;
    }

    if (app.config().useTxTables())
    {
        auto& db = app.getRelationalDatabase();

        auto dbKB = db.getKBUsedAll();

        if (dbKB > 0)
            ret[jss::dbKBTotal] = dbKB;

        dbKB = db.getKBUsedLedger();

        if (dbKB > 0)
            ret[jss::dbKBLedger] = dbKB;

        dbKB = db.getKBUsedTransaction();

        if (dbKB > 0)
            ret[jss::dbKBTransaction] = dbKB;

        {
            std::size_t const c = app.getOPs().getLocalTxCount();
            if (c > 0)
                ret[jss::local_txs] = static_cast<json::UInt>(c);
        }
    }

    ret[jss::write_load] = app.getNodeStore().getWriteLoad();

    ret[jss::historical_perminute] = static_cast<int>(app.getInboundLedgers().fetchRate());
    ret[jss::SLE_hit_rate] = app.getCachedSLEs().rate();
    ret[jss::ledger_hit_rate] = app.getLedgerMaster().getCacheHitRate();
    ret[jss::AL_size] = json::UInt(app.getAcceptedLedgerCache().size());
    ret[jss::AL_hit_rate] = app.getAcceptedLedgerCache().getHitRate();

    ret[jss::fullbelow_size] = static_cast<int>(app.getNodeFamily().getFullBelowCache()->size());
    ret[jss::treenode_cache_size] = app.getNodeFamily().getTreeNodeCache()->getCacheSize();
    ret[jss::treenode_track_size] = app.getNodeFamily().getTreeNodeCache()->getTrackSize();

    std::string uptime;
    auto s = UptimeClock::now();
    using namespace std::chrono_literals;
    textTime(uptime, s, "year", 365 * 24h);
    textTime(uptime, s, "day", 24h);
    textTime(uptime, s, "hour", 1h);
    textTime(uptime, s, "minute", 1min);
    textTime(uptime, s, "second", 1s);
    ret[jss::uptime] = uptime;

    app.getNodeStore().getCountsJson(ret);

    return ret;
}

/** Handle the `get_counts` admin RPC request.
 *
 *  Reads the optional `min_count` parameter (default: 10) and delegates to
 *  `getCountsJson`. The default of 10 acts as a noise filter, suppressing
 *  object types with very few live instances that are rarely relevant in
 *  production diagnostics.
 *
 *  @param context RPC dispatch context; `context.params` may contain
 *      `min_count` (unsigned integer). A missing, non-numeric, or negative
 *      value silently coerces to 0, causing all object types to be reported.
 *  @return JSON object of aggregated runtime diagnostics.
 *  @see getCountsJson
 */
json::Value
doGetCounts(RPC::JsonContext& context)
{
    int minCount = 10;

    if (context.params.isMember(jss::min_count))
        minCount = context.params[jss::min_count].asUInt();

    return getCountsJson(context.app, minCount);
}

}  // namespace xrpl
