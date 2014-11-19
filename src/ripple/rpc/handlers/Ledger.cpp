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
#include <ripple/rpc/handlers/Ledger.h>
#include <ripple/rpc/impl/JsonObject.h>
#include <ripple/server/Role.h>

namespace ripple {
namespace RPC {

LedgerHandler::LedgerHandler (Context& context) : HandlerBase (context)
{
}

bool LedgerHandler::check (Json::Value& error)
{
    bool needsLedger = context_.params.isMember (jss::ledger) ||
            context_.params.isMember (jss::ledger_hash) ||
            context_.params.isMember (jss::ledger_index);
    if (!needsLedger)
        return true;

    lookupResult_ = RPC::lookupLedger (
        context_.params, ledger_, context_.netOps);

    if (!ledger_)
    {
        error = lookupResult_;
        return false;
    }

    bool bFull = context_.params.isMember (jss::full)
            && context_.params[jss::full].asBool ();
    bool bTransactions = context_.params.isMember (jss::transactions)
            && context_.params[jss::transactions].asBool ();
    bool bAccounts = context_.params.isMember (jss::accounts)
            && context_.params[jss::accounts].asBool ();
    bool bExpand = context_.params.isMember (jss::expand)
            && context_.params[jss::expand].asBool ();
    options_ = (bFull ? LEDGER_JSON_FULL : 0)
            | (bExpand ? LEDGER_JSON_EXPAND : 0)
            | (bTransactions ? LEDGER_JSON_DUMP_TXRP : 0)
            | (bAccounts ? LEDGER_JSON_DUMP_STATE : 0);

    if (bFull || bAccounts)
    {
        if (context_.role != Role::ADMIN)
        {
            // Until some sane way to get full ledgers has been implemented,
            // disallow retrieving all state nodes.
            error = rpcError (rpcNO_PERMISSION);
            return false;
        }

        if (getApp().getFeeTrack().isLoadedLocal() &&
            context_.role != Role::ADMIN)
        {
            WriteLog (lsDEBUG, Peer) << "Too busy to give full ledger";
            error = rpcError(rpcTOO_BUSY);
            return false;
        }
        context_.loadType = Resource::feeHighBurdenRPC;
    }

    return true;
}

template <class JsonValue>
void LedgerHandler::writeJson (JsonValue& value)
{
    if (ledger_)
    {
        RPC::copyFrom (value, lookupResult_);
        addJson (*ledger_, value, options_, context_.yield);
    }
    else
    {
        auto& master = getApp().getLedgerMaster ();
        auto& yield = context_.yield;
        auto&& open = RPC::addObject (value, jss::open);
        auto&& closed = RPC::addObject (value, jss::closed);
        addJson (*master.getCurrentLedger(), open, 0, yield);
        addJson (*master.getClosedLedger(), closed, 0, yield);
    }
}

void LedgerHandler::write (Object& value)
{
    writeJson (value);
}

template void LedgerHandler::writeJson<Object> (Object&);

} // RPC

// ledger [id|index|current|closed] [full]
// {
//    ledger: 'current' | 'closed' | <uint256> | <number>,  // optional
//    full: true | false    // optional, defaults to false.
// }
Json::Value doLedger (RPC::Context& context)
{
    RPC::LedgerHandler handler (context);
    Json::Value object;
    if (handler.check (object))
        handler.writeJson (object);
    return object;
}

} // ripple
