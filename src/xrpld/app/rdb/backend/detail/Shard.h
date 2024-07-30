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
    ANY  SPECIAL,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_APP_RDB_BACKEND_DETAIL_SHARD_H_INCLUDED
#define RIPPLE_APP_RDB_BACKEND_DETAIL_SHARD_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/rdb/RelationalDatabase.h>
#include <ripple/app/rdb/UnitaryShard.h>
#include <ripple/core/Config.h>
#include <ripple/protocol/RippleLedgerHash.h>
#include <boost/filesystem.hpp>

namespace ripple {
namespace detail {

/**
 * @brief makeMetaDBs Opens ledger and transaction 'meta' databases which
 *        map ledger hashes and transaction IDs to the index of the shard
 *        that holds the ledger or transaction.
 * @param config Config object.
 * @param setup Path to database and opening parameters.
 * @param checkpointerSetup Database checkpointer setup.
 * @return Struct DatabasePair which contains unique pointers to the ledger
 *         and transaction databases.
 */
DatabasePair
makeMetaDBs(
    Config const& config,
    DatabaseCon::Setup const& setup,
    DatabaseCon::CheckpointerSetup const& checkpointerSetup);

/**
 * @brief saveLedgerMeta Stores (transaction ID -> shard index) and
 *        (ledger hash -> shard index) mappings in the meta databases.
 * @param ledger The ledger.
 * @param app Application object.
 * @param lgrMetaSession Session to ledger meta database.
 * @param txnMetaSession Session to transaction meta database.
 * @param shardIndex The index of the shard that contains this ledger.
 * @return True on success.
 */
bool
saveLedgerMeta(
    std::shared_ptr<Ledger const> const& ledger,
    Application& app,
    soci::session& lgrMetaSession,
    soci::session& txnMetaSession,
    std::uint32_t shardIndex);

/**
 * @brief getShardIndexforLedger Queries the ledger meta database to
 *        retrieve the index of the shard that contains this ledger.
 * @param session Session to the database.
 * @param hash Hash of the ledger.
 * @return The index of the shard on success, otherwise an unseated value.
 */
std::optional<std::uint32_t>
getShardIndexforLedger(soci::session& session, LedgerHash const& hash);

/**
 * @brief getShardIndexforTransaction Queries the transaction meta database to
 *        retrieve the index of the shard that contains this transaction.
 * @param session Session to the database.
 * @param id ID of the transaction.
 * @return The index of the shard on success, otherwise an unseated value.
 */
std::optional<std::uint32_t>
getShardIndexforTransaction(soci::session& session, TxID const& id);

}  // namespace detail
}  // namespace ripple

#endif
