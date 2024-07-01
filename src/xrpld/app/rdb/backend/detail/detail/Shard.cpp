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

#include <ripple/app/ledger/AcceptedLedger.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/rdb/backend/detail/Shard.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/core/SociDB.h>
#include <ripple/json/to_string.h>

namespace ripple {
namespace detail {

DatabasePair
makeMetaDBs(
    Config const& config,
    DatabaseCon::Setup const& setup,
    DatabaseCon::CheckpointerSetup const& checkpointerSetup)
{
    // ledger meta database
    auto lgrMetaDB{std::make_unique<DatabaseCon>(
        setup,
        LgrMetaDBName,
        LgrMetaDBPragma,
        LgrMetaDBInit,
        checkpointerSetup)};

    if (!config.useTxTables())
        return {std::move(lgrMetaDB), nullptr};

    // transaction meta database
    auto txMetaDB{std::make_unique<DatabaseCon>(
        setup, TxMetaDBName, TxMetaDBPragma, TxMetaDBInit, checkpointerSetup)};

    return {std::move(lgrMetaDB), std::move(txMetaDB)};
}

bool
saveLedgerMeta(
    std::shared_ptr<Ledger const> const& ledger,
    Application& app,
    soci::session& lgrMetaSession,
    soci::session& txnMetaSession,
    std::uint32_t const shardIndex)
{
    std::string_view constexpr lgrSQL =
        R"sql(INSERT OR REPLACE INTO LedgerMeta VALUES
              (:ledgerHash,:shardIndex);)sql";

    auto const hash = to_string(ledger->info().hash);
    lgrMetaSession << lgrSQL, soci::use(hash), soci::use(shardIndex);

    if (!app.config().useTxTables())
        return true;

    auto const aLedger = [&app, ledger]() -> std::shared_ptr<AcceptedLedger> {
        try
        {
            auto aLedger =
                app.getAcceptedLedgerCache().fetch(ledger->info().hash);
            if (!aLedger)
            {
                aLedger = std::make_shared<AcceptedLedger>(ledger, app);
                app.getAcceptedLedgerCache().canonicalize_replace_client(
                    ledger->info().hash, aLedger);
            }

            return aLedger;
        }
        catch (std::exception const&)
        {
            JLOG(app.journal("Ledger").warn())
                << "An accepted ledger was missing nodes";
        }

        return {};
    }();

    if (!aLedger)
        return false;

    soci::transaction tr(txnMetaSession);

    for (auto const& acceptedLedgerTx : *aLedger)
    {
        std::string_view constexpr txnSQL =
            R"sql(INSERT OR REPLACE INTO TransactionMeta VALUES
                      (:transactionID,:shardIndex);)sql";

        auto const transactionID =
            to_string(acceptedLedgerTx->getTransactionID());

        txnMetaSession << txnSQL, soci::use(transactionID),
            soci::use(shardIndex);
    }

    tr.commit();
    return true;
}

std::optional<std::uint32_t>
getShardIndexforLedger(soci::session& session, LedgerHash const& hash)
{
    std::uint32_t shardIndex;
    session << "SELECT ShardIndex FROM LedgerMeta WHERE LedgerHash = '" << hash
            << "';",
        soci::into(shardIndex);

    if (!session.got_data())
        return std::nullopt;

    return shardIndex;
}

std::optional<std::uint32_t>
getShardIndexforTransaction(soci::session& session, TxID const& id)
{
    std::uint32_t shardIndex;
    session << "SELECT ShardIndex FROM TransactionMeta WHERE TransID = '" << id
            << "';",
        soci::into(shardIndex);

    if (!session.got_data())
        return std::nullopt;

    return shardIndex;
}

}  // namespace detail
}  // namespace ripple
