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
#include <ripple/app/main/Application.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/RPCHelpers.h>

namespace ripple {

// {
//   ledger_hash : <ledger>,
//   ledger_index : <ledger_index>
// }
//
// XXX In this case, not specify either ledger does not mean ledger current. It
// means any ledger.
Json::Value doTransactionEntry (RPC::Context& context)
{
    std::shared_ptr<ReadView const> lpLedger;
    Json::Value jvResult = RPC::lookupLedger (lpLedger, context);

    if(! lpLedger)
        return jvResult;

    if(! context.params.isMember (jss::tx_hash))
    {
        jvResult[jss::error] = "fieldNotFoundTransaction";
    }
    else if(jvResult.get(jss::ledger_hash, Json::nullValue).isNull())
    {
        // We don't work on ledger current.

        // XXX We don't support any transaction yet.
        jvResult[jss::error]   = "notYetImplemented";
    }
    else
    {
        uint256 uTransID;
        // XXX Relying on trusted WSS client. Would be better to have a strict
        // routine, returning success or failure.
        uTransID.SetHex (context.params[jss::tx_hash].asString ());

        auto tx = lpLedger->txRead (uTransID);
        if(! tx.first)
        {
            jvResult[jss::error]   = "transactionNotFound";
        }
        else
        {
            jvResult[jss::tx_json]     = tx.first->getJson (0);
            if (tx.second)
                jvResult[jss::metadata]    = tx.second->getJson (0);
            // 'accounts'
            // 'engine_...'
            // 'ledger_...'
        }
    }

    return jvResult;
}

} // ripple
