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

#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/rpc/handlers/Ledger.h>
#include <ripple/rpc/impl/JsonObject.h>
#include <ripple/server/Role.h>

namespace ripple {
namespace RPC {

LedgerHandler::LedgerHandler (Context& context) : context_ (context)
{
}

Status LedgerHandler::check ()
{
    auto const& params = context_.params;
    bool needsLedger = params.isMember (jss::ledger) ||
            params.isMember (jss::ledger_hash) ||
            params.isMember (jss::ledger_index);
    if (!needsLedger)
        return Status::OK;

    if (auto s = RPC::lookupLedger (params, ledger_, context_.netOps, result_))
        return s;

    bool bFull = params[jss::full].asBool();
    bool bTransactions = params[jss::transactions].asBool();
    bool bAccounts = params[jss::accounts].asBool();
    bool bExpand = params[jss::expand].asBool();

    options_ = (bFull ? LEDGER_JSON_FULL : 0)
            | (bExpand ? LEDGER_JSON_EXPAND : 0)
            | (bTransactions ? LEDGER_JSON_DUMP_TXRP : 0)
            | (bAccounts ? LEDGER_JSON_DUMP_STATE : 0);

    if (bFull || bAccounts)
    {
        // Until some sane way to get full ledgers has been implemented,
        // disallow retrieving all state nodes.
        if (context_.role != Role::ADMIN)
            return rpcNO_PERMISSION;

        if (getApp().getFeeTrack().isLoadedLocal() &&
            context_.role != Role::ADMIN)
        {
            return rpcTOO_BUSY;
        }
        context_.loadType = Resource::feeHighBurdenRPC;
    }

    return Status::OK;
}

} // RPC
} // ripple
