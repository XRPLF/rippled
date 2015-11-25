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
#include <ripple/rpc/handlers/LedgerHandler.h>
#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/app/main/Application.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/json/Object.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/impl/LookupLedger.h>
#include <ripple/server/Role.h>

namespace ripple {
namespace RPC {

LedgerHandler::LedgerHandler (Context& context) : context_ (context)
{
}

Status LedgerHandler::check()
{
    auto const& params = context_.params;
    bool needsLedger = params.isMember (jss::ledger) ||
            params.isMember (jss::ledger_hash) ||
            params.isMember (jss::ledger_index);
    if (!needsLedger)
        return Status::OK;

    if (auto s = lookupLedger (ledger_, context_, result_))
        return s;

    bool bFull = params[jss::full].asBool();
    bool bTransactions = params[jss::transactions].asBool();
    bool bAccounts = params[jss::accounts].asBool();
    bool bExpand = params[jss::expand].asBool();
    bool bBinary = params[jss::binary].asBool();

    options_ = (bFull ? LedgerFill::full : 0)
            | (bExpand ? LedgerFill::expand : 0)
            | (bTransactions ? LedgerFill::dumpTxrp : 0)
            | (bAccounts ? LedgerFill::dumpState : 0)
            | (bBinary ? LedgerFill::binary : 0);

    if (bFull || bAccounts)
    {
        // Until some sane way to get full ledgers has been implemented,
        // disallow retrieving all state nodes.
        if (! isUnlimited (context_.role))
            return rpcNO_PERMISSION;

        if (context_.app.getFeeTrack().isLoadedLocal() &&
            !isUnlimited (context_.role))
        {
            return rpcTOO_BUSY;
        }
        context_.loadType = bBinary ? Resource::feeMediumBurdenRPC :
            Resource::feeHighBurdenRPC;
    }

    return Status::OK;
}

} // RPC
} // ripple
