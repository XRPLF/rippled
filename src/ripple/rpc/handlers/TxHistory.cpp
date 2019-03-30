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

#include <ripple/app/main/Application.h>
#include <ripple/app/misc/Transaction.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/core/SociDB.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/Role.h>
#include <boost/format.hpp>

namespace ripple {

// {
//   start: <index>
// }
Json::Value doTxHistory (RPC::Context& context)
{
    context.loadType = Resource::feeMediumBurdenRPC;

    if (!context.params.isMember (jss::start))
        return rpcError (rpcINVALID_PARAMS);

    unsigned int startIndex = context.params[jss::start].asUInt ();

    if ((startIndex > 10000) &&  (! isUnlimited (context.role)))
        return rpcError (rpcNO_PERMISSION);

    Json::Value obj;
    Json::Value txs;

    obj[jss::index] = startIndex;

    std::string sql =
        boost::str (boost::format (
            "SELECT LedgerSeq, Status, RawTxn "
            "FROM Transactions ORDER BY LedgerSeq desc LIMIT %u,20;")
                    % startIndex);

    {
        auto db = context.app.getTxnDB ().checkoutDb ();

        boost::optional<std::uint64_t> ledgerSeq;
        boost::optional<std::string> status;
        soci::blob sociRawTxnBlob (*db);
        soci::indicator rti;
        Blob rawTxn;

        soci::statement st = (db->prepare << sql,
                              soci::into (ledgerSeq),
                              soci::into (status),
                              soci::into (sociRawTxnBlob, rti));

        st.execute ();
        while (st.fetch ())
        {
            if (soci::i_ok == rti)
                convert(sociRawTxnBlob, rawTxn);
            else
                rawTxn.clear ();

            if (auto trans = Transaction::transactionFromSQL (
                    ledgerSeq, status, rawTxn, context.app))
                txs.append (trans->getJson (0));
        }
    }

    obj[jss::txs] = txs;

    return obj;
}

} // ripple
