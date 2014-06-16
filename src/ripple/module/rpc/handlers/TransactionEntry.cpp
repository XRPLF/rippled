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
//   ledger_hash : <ledger>,
//   ledger_index : <ledger_index>
// }
// XXX In this case, not specify either ledger does not mean ledger current. It means any ledger.
Json::Value doTransactionEntry (RPC::Context& context)
{
    context.lock_.unlock ();

    Ledger::pointer     lpLedger;
    Json::Value         jvResult    = RPC::lookupLedger (context.params_, lpLedger, context.netOps_);

    if (!lpLedger)
        return jvResult;

    if (!context.params_.isMember ("tx_hash"))
    {
        jvResult["error"]   = "fieldNotFoundTransaction";
    }
    else if (!context.params_.isMember ("ledger_hash") && !context.params_.isMember ("ledger_index"))
    {
        // We don't work on ledger current.

        jvResult["error"]   = "notYetImplemented";  // XXX We don't support any transaction yet.
    }
    else
    {
        uint256                     uTransID;
        // XXX Relying on trusted WSS client. Would be better to have a strict routine, returning success or failure.
        uTransID.SetHex (context.params_["tx_hash"].asString ());

        if (!lpLedger)
        {
            jvResult["error"]   = "ledgerNotFound";
        }
        else
        {
            Transaction::pointer        tpTrans;
            TransactionMetaSet::pointer tmTrans;

            if (!lpLedger->getTransaction (uTransID, tpTrans, tmTrans))
            {
                jvResult["error"]   = "transactionNotFound";
            }
            else
            {
                jvResult["tx_json"]     = tpTrans->getJson (0);
                if (tmTrans)
                    jvResult["metadata"]    = tmTrans->getJson (0);
                // 'accounts'
                // 'engine_...'
                // 'ledger_...'
            }
        }
    }

    return jvResult;
}

} // ripple
