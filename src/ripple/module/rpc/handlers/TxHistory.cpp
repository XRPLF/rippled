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


namespace ripple {

// {
//   start: <index>
// }
Json::Value doTxHistory (RPC::Context& context)
{
    context.lock_.unlock ();
    context.loadType_ = Resource::feeMediumBurdenRPC;

    if (!context.params_.isMember ("start"))
        return rpcError (rpcINVALID_PARAMS);

    unsigned int startIndex = context.params_["start"].asUInt ();

    if ((startIndex > 10000) &&  (context.role_ != Config::ADMIN))
        return rpcError (rpcNO_PERMISSION);

    Json::Value obj;
    Json::Value txs;

    obj["index"] = startIndex;

    std::string sql =
        boost::str (boost::format ("SELECT * FROM Transactions ORDER BY LedgerSeq desc LIMIT %u,20")
                    % startIndex);

    {
        Database* db = getApp().getTxnDB ()->getDB ();
        DeprecatedScopedLock sl (getApp().getTxnDB ()->getDBLock ());

        SQL_FOREACH (db, sql)
        {
            Transaction::pointer trans = Transaction::transactionFromSQL (db, false);

            if (trans) txs.append (trans->getJson (0));
        }
    }

    obj["txs"] = txs;

    return obj;
}

} // ripple
