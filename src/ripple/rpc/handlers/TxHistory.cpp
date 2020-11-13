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

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/Transaction.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/core/Pg.h>
#include <ripple/core/SociDB.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/Role.h>
#include <ripple/rpc/Status.h>
#include <boost/format.hpp>

namespace ripple {

Json::Value
doTxHistoryReporting(RPC::JsonContext& context)
{
    Json::Value ret;
#ifdef RIPPLED_REPORTING
    if (!context.app.config().reporting())
    {
        assert(false);
        Throw<std::runtime_error>(
            "called doTxHistoryReporting but not in reporting mode");
    }
    context.loadType = Resource::feeMediumBurdenRPC;

    if (!context.params.isMember(jss::start))
        return rpcError(rpcINVALID_PARAMS);

    unsigned int startIndex = context.params[jss::start].asUInt();

    if ((startIndex > 10000) && (!isUnlimited(context.role)))
        return rpcError(rpcNO_PERMISSION);

    std::string sql = boost::str(
        boost::format("SELECT nodestore_hash, ledger_seq "
                      "  FROM transactions"
                      " ORDER BY ledger_seq DESC LIMIT 20 "
                      "OFFSET %u;") %
        startIndex);

    auto res = PgQuery(context.app.getPgPool())(sql.data());

    if (!res)
    {
        JLOG(context.j.error())
            << __func__ << " : Postgres response is null - sql = " << sql;
        assert(false);
        return {};
    }
    else if (res.status() != PGRES_TUPLES_OK)
    {
        JLOG(context.j.error())
            << __func__
            << " : Postgres response should have been "
               "PGRES_TUPLES_OK but instead was "
            << res.status() << " - msg  = " << res.msg() << " - sql = " << sql;
        assert(false);
        return {};
    }

    JLOG(context.j.trace())
        << __func__ << " Postgres result msg  : " << res.msg();

    if (res.isNull() || res.ntuples() == 0)
    {
        JLOG(context.j.debug()) << __func__ << " : Empty postgres response";
        assert(false);
        return {};
    }
    else if (res.ntuples() > 0)
    {
        if (res.nfields() != 2)
        {
            JLOG(context.j.error()) << __func__
                                    << " : Wrong number of fields in Postgres "
                                       "response. Expected 1, but got "
                                    << res.nfields() << " . sql = " << sql;
            assert(false);
            return {};
        }
    }

    JLOG(context.j.trace())
        << __func__ << " : Postgres result = " << res.c_str();

    Json::Value txs;

    std::vector<uint256> nodestoreHashes;
    std::vector<uint32_t> ledgerSequences;
    for (size_t i = 0; i < res.ntuples(); ++i)
    {
        uint256 hash;
        if (!hash.parseHex(res.c_str(i, 0) + 2))
            assert(false);
        nodestoreHashes.push_back(hash);
        ledgerSequences.push_back(res.asBigInt(i, 1));
    }

    auto txns = flatFetchTransactions(context.app, nodestoreHashes);
    for (size_t i = 0; i < txns.size(); ++i)
    {
        auto const& [sttx, meta] = txns[i];
        assert(sttx);

        std::string reason;
        auto txn = std::make_shared<Transaction>(sttx, reason, context.app);
        txn->setLedger(ledgerSequences[i]);
        txn->setStatus(COMMITTED);
        txs.append(txn->getJson(JsonOptions::none));
    }

    ret[jss::index] = startIndex;
    ret[jss::txs] = txs;
    ret["used_postgres"] = true;

#endif
    return ret;
}

// {
//   start: <index>
// }
Json::Value
doTxHistory(RPC::JsonContext& context)
{
    if (!context.app.config().useTxTables())
        return rpcError(rpcNOT_ENABLED);

    if (context.app.config().reporting())
        return doTxHistoryReporting(context);
    context.loadType = Resource::feeMediumBurdenRPC;

    if (!context.params.isMember(jss::start))
        return rpcError(rpcINVALID_PARAMS);

    unsigned int startIndex = context.params[jss::start].asUInt();

    if ((startIndex > 10000) && (!isUnlimited(context.role)))
        return rpcError(rpcNO_PERMISSION);

    Json::Value obj;
    Json::Value txs;

    obj[jss::index] = startIndex;

    std::string sql = boost::str(
        boost::format(
            "SELECT LedgerSeq, Status, RawTxn "
            "FROM Transactions ORDER BY LedgerSeq desc LIMIT %u,20;") %
        startIndex);

    {
        auto db = context.app.getTxnDB().checkoutDb();

        // SOCI requires boost::optional (not std::optional) as parameters.
        boost::optional<std::uint64_t> ledgerSeq;
        boost::optional<std::string> status;
        soci::blob sociRawTxnBlob(*db);
        soci::indicator rti;
        Blob rawTxn;

        soci::statement st =
            (db->prepare << sql,
             soci::into(ledgerSeq),
             soci::into(status),
             soci::into(sociRawTxnBlob, rti));

        st.execute();
        while (st.fetch())
        {
            if (soci::i_ok == rti)
                convert(sociRawTxnBlob, rawTxn);
            else
                rawTxn.clear();

            if (auto trans = Transaction::transactionFromSQL(
                    ledgerSeq, status, rawTxn, context.app))
                txs.append(trans->getJson(JsonOptions::none));
        }
    }

    obj[jss::txs] = txs;

    return obj;
}

}  // namespace ripple
