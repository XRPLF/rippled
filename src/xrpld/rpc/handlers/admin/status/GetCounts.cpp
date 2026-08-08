#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/rdb/backend/SQLiteDatabase.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/PathRequestManager.h>

#include <xrpl/basics/UptimeClock.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace xrpl {

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

    // Pathfinding AssetCache stats (shared across continuous path_find).
    // Use double so u64 counters are not truncated by json::UInt (32-bit).
    // When idle the cache is released: report zeros so charts reclaim and
    // operators can see memory drop after the last WS path_find closes.
    auto setU64 = [](json::Value& obj, char const* key, std::uint64_t v) {
        // double preserves integers exactly through 2^53; pathfind counters stay well below that.
        obj[key] = static_cast<double>(v);
    };
    auto const cacheStats = app.getPathRequestManager().getCacheStats();
    auto const hits = cacheStats.available ? cacheStats.hits : 0;
    auto const misses = cacheStats.available ? cacheStats.misses : 0;
    auto const loaded = cacheStats.available ? cacheStats.linesLoaded : 0;
    auto const advances = cacheStats.available ? cacheStats.ledgerAdvances : 0;
    auto const lines = cacheStats.available ? static_cast<std::uint64_t>(cacheStats.totalLines) : 0;
    setU64(ret, "pathfind_cache_hits", hits);
    setU64(ret, "pathfind_cache_misses", misses);
    setU64(ret, "pathfind_lines_loaded", loaded);
    // Correct name: counts advanceLedger calls (soft or force), not only rebuilds.
    setU64(ret, "pathfind_cache_advances", advances);
    // Alias for existing load-test / chart keys.
    setU64(ret, "pathfind_cache_rebuilds", advances);
    setU64(ret, "pathfind_cache_lines", lines);

    return ret;
}

// {
//   min_count: <number>  // optional, defaults to 10
// }
json::Value
doGetCounts(rpc::JsonContext& context)
{
    int minCount = 10;

    if (context.params.isMember(jss::min_count))
        minCount = context.params[jss::min_count].asUInt();

    return getCountsJson(context.app, minCount);
}

}  // namespace xrpl
