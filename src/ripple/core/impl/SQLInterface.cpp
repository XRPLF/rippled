//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#include <ripple/core/ConfigSections.h>
#include <ripple/core/SQLInterface.h>
#include <ripple/nodestore/DatabaseShard.h>

namespace ripple {

LedgerIndex SQLInterface::ledgersPerShard_;

std::unordered_map<SQLInterface::DatabaseType, SQLInterface*>
    SQLInterface::type2iface_;
std::unordered_map<SQLDatabase_*, SQLInterface::DatabaseIndex>
    SQLInterface::db2ind_;
std::map<LedgerIndex, SQLDatabase_*> SQLInterface::txInd2db_;
std::map<LedgerIndex, SQLDatabase_*> SQLInterface::lgrInd2db_;
std::mutex SQLInterface::maps_mutex_;

LedgerIndex
SQLInterface::seqToShardIndex(LedgerIndex seq)
{
    return NodeStore::seqToShardIndex(seq, ledgersPerShard_);
}

LedgerIndex
SQLInterface::firstLedgerSeq(LedgerIndex shardIndex)
{
    return shardIndex * ledgersPerShard_ + 1;
}

LedgerIndex
SQLInterface::lastLedgerSeq(LedgerIndex shardIndex)
{
    return shardIndex * (ledgersPerShard_ + 1);
}

bool
SQLInterface::init(Config const& config)
{
    extern SQLInterface* SQLInterfaceSqlite;

    SQLInterface* default_backend = SQLInterfaceSqlite;

    type2iface_[LEDGER] = default_backend;
    type2iface_[TRANSACTION] = default_backend;
    type2iface_[WALLET] = default_backend;
    type2iface_[LEDGER_SHARD] = default_backend;
    type2iface_[TRANSACTION_SHARD] = default_backend;
    type2iface_[ACQUIRE_SHARD] = default_backend;
    type2iface_[ARCHIVE] = default_backend;
    type2iface_[STATE] = default_backend;
    type2iface_[DOWNLOAD] = default_backend;
    type2iface_[PEER_FINDER] = default_backend;
    type2iface_[VACUUM] = default_backend;

    ledgersPerShard_ = NodeStore::DatabaseShard::ledgersPerShardDefault;

    const Section& node_section{config.section(ConfigSection::nodeDatabase())};
    if (!node_section.empty())
    {
        if (boost::iequals(
                get<std::string>(node_section, "sql_backend"), "sqlite"))
        {
            type2iface_[LEDGER] = SQLInterfaceSqlite;
            type2iface_[TRANSACTION] = SQLInterfaceSqlite;
        }
    }

    const Section& shard_section{
        config.section(ConfigSection::shardDatabase())};
    if (!shard_section.empty())
    {
        if (boost::iequals(
                get<std::string>(shard_section, "sql_backend"), "sqlite"))
        {
            type2iface_[LEDGER_SHARD] = SQLInterfaceSqlite;
            type2iface_[TRANSACTION_SHARD] = SQLInterfaceSqlite;
            type2iface_[ACQUIRE_SHARD] = SQLInterfaceSqlite;
        }

        if (shard_section.exists("ledgers_per_shard"))
        {
            ledgersPerShard_ =
                get<std::uint32_t>(shard_section, "ledgers_per_shard");
        }
    }

    return true;
}

SQLInterface*
SQLInterface::getInterface(SQLInterface::DatabaseType type)
{
    return type2iface_[type];
}

void
SQLInterface::addDatabase(
    SQLDatabase_* db,
    DatabaseType type,
    LedgerIndex shardIndex)
{
    std::lock_guard lock{maps_mutex_};
    DatabaseIndex ind = std::make_pair(shardIndex, type);
    db2ind_[db] = ind;
    if (shardIndex != -1u)
    {
        switch (type)
        {
            case LEDGER:
                lgrInd2db_[shardIndex] = db;
                break;
            case TRANSACTION:
                txInd2db_[shardIndex] = db;
                break;
            default:
                break;
        }
    }
}

void
SQLInterface::removeDatabase(SQLDatabase_* db)
{
    std::lock_guard lock{maps_mutex_};
    if (db2ind_.count(db))
    {
        auto ind = db2ind_[db];
        db2ind_.erase(db);
        switch (ind.second)
        {
            case LEDGER:
                lgrInd2db_.erase(ind.first);
                break;
            case TRANSACTION:
                txInd2db_.erase(ind.first);
                break;
            default:
                break;
        }
    }
}

SQLDatabase_*
SQLInterface::findShardDatabase(SQLDatabase_* db, LedgerIndex ledgerIndex)
{
    std::lock_guard lock{maps_mutex_};
    if (db2ind_.count(db))
    {
        auto [index, type] = db2ind_[db];
        if (index == ledgerIndex)
            return db;
        switch (type)
        {
            case LEDGER:
                if (lgrInd2db_.count(ledgerIndex))
                    return lgrInd2db_[ledgerIndex];
                break;
            case TRANSACTION:
                if (txInd2db_.count(ledgerIndex))
                    return txInd2db_[ledgerIndex];
                break;
            default:
                break;
        }
    }
    return nullptr;
}

bool
SQLInterface::iterate_forward(
    SQLDatabase_* db,
    LedgerIndex firstIndex,
    std::function<bool(SQLDatabase_* db, LedgerIndex index)> const& onShardDB)
{
    std::lock_guard lock{maps_mutex_};
    if (!db2ind_.count(db))
        return false;
    [[maybe_unused]] auto [index, type] = db2ind_[db];
    std::map<LedgerIndex, SQLDatabase_*>::iterator it, eit;
    switch (type)
    {
        case LEDGER:
            if (firstIndex == (LedgerIndex)-1u)
                it = lgrInd2db_.begin();
            else
                it = lgrInd2db_.lower_bound(firstIndex);
            eit = lgrInd2db_.end();
            break;
        case TRANSACTION:
            if (firstIndex == (LedgerIndex)-1u)
                it = txInd2db_.begin();
            else
                it = txInd2db_.lower_bound(firstIndex);
            eit = txInd2db_.end();
            break;
        default:
            return false;
    }

    for (; it != eit; it++)
    {
        if (!onShardDB(it->second, it->first))
            return false;
    }
    return true;
}

bool
SQLInterface::iterate_back(
    SQLDatabase_* db,
    LedgerIndex lastIndex,
    std::function<bool(SQLDatabase_* db, LedgerIndex index)> const& onShardDB)
{
    std::lock_guard lock{maps_mutex_};
    if (!db2ind_.count(db))
        return false;
    [[maybe_unused]] auto [index, type] = db2ind_[db];
    std::map<LedgerIndex, SQLDatabase_*>::reverse_iterator it, eit;
    switch (type)
    {
        case LEDGER:
            if (lastIndex == (LedgerIndex)-1u)
                it = lgrInd2db_.rbegin();
            else
                it = std::make_reverse_iterator(
                    lgrInd2db_.upper_bound(lastIndex));
            eit = lgrInd2db_.rend();
            break;
        case TRANSACTION:
            if (lastIndex == (LedgerIndex)-1u)
                it = txInd2db_.rbegin();
            else
                it = std::make_reverse_iterator(
                    txInd2db_.upper_bound(lastIndex));
            eit = txInd2db_.rend();
            break;
        default:
            return false;
    }

    for (; it != eit; it++)
    {
        if (!onShardDB(it->second, it->first))
            return false;
    }
    return true;
}

}  // namespace ripple
