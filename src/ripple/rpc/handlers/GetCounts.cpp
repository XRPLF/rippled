//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <BeastConfig.h>
#include <ripple/app/data/DatabaseCon.h>
#include <ripple/app/ledger/AcceptedLedger.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/basics/UptimeTimer.h>
#include <ripple/nodestore/Database.h>

namespace ripple {

// {
//   min_count: <number>  // optional, defaults to 10
// }
Json::Value doGetCounts (RPC::Context& context)
{
    int minCount = 10;

    if (context.params.isMember (jss::min_count))
        minCount = context.params[jss::min_count].asUInt ();

    auto objectCounts = CountedObjects::getInstance ().getCounts (minCount);

    Json::Value ret (Json::objectValue);

    for (auto const& it : objectCounts)
    {
        ret [it.first] = it.second;
    }

    Application& app = getApp();

    int dbKB = getKBUsedAll (app.getLedgerDB ().getSession ());

    if (dbKB > 0)
        ret[jss::dbKBTotal] = dbKB;

    dbKB = getKBUsedDB (app.getLedgerDB ().getSession ());

    if (dbKB > 0)
        ret[jss::dbKBLedger] = dbKB;

    dbKB = getKBUsedDB (app.getTxnDB ().getSession ());

    if (dbKB > 0)
        ret[jss::dbKBTransaction] = dbKB;

    {
        std::size_t c = app.getOPs().getLocalTxCount ();
        if (c > 0)
            ret[jss::local_txs] = static_cast<Json::UInt> (c);
    }

    ret[jss::write_load] = app.getNodeStore ().getWriteLoad ();

    ret[jss::historical_perminute] = static_cast<int>(
        app.getInboundLedgers().fetchRate());
    ret[jss::SLE_hit_rate] = app.getSLECache ().getHitRate ();
    ret[jss::node_hit_rate] = app.getNodeStore ().getCacheHitRate ();
    ret[jss::ledger_hit_rate] = app.getLedgerMaster ().getCacheHitRate ();
    ret[jss::AL_hit_rate] = AcceptedLedger::getCacheHitRate ();

    ret[jss::fullbelow_size] = static_cast<int>(app.family().fullbelow().size());
    ret[jss::treenode_cache_size] = app.family().treecache().getCacheSize();
    ret[jss::treenode_track_size] = app.family().treecache().getTrackSize();

    std::string uptime;
    int s = UptimeTimer::getInstance ().getElapsedSeconds ();
    textTime (uptime, s, "year", 365 * 24 * 60 * 60);
    textTime (uptime, s, "day", 24 * 60 * 60);
    textTime (uptime, s, "hour", 60 * 60);
    textTime (uptime, s, "minute", 60);
    textTime (uptime, s, "second", 1);
    ret[jss::uptime] = uptime;

    ret[jss::node_writes] = app.getNodeStore().getStoreCount();
    ret[jss::node_reads_total] = app.getNodeStore().getFetchTotalCount();
    ret[jss::node_reads_hit] = app.getNodeStore().getFetchHitCount();
    ret[jss::node_written_bytes] = app.getNodeStore().getStoreSize();
    ret[jss::node_read_bytes] = app.getNodeStore().getFetchSize();

    return ret;
}

} // ripple
