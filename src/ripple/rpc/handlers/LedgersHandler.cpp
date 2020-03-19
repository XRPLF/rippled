//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Dev Null Productions, LLC

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

#include <ripple/rpc/handlers/LedgersHandler.h>
#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/json/Object.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <ripple/rpc/Role.h>

namespace ripple {
namespace RPC {

LedgersHandler::LedgersHandler (JsonContext& context) : context_ (context)
{
}

Status LedgersHandler::check()
{
    auto const& params = context_.params;
    bool needsLedger =
            params.isMember (jss::ledger_hashes) ||
            params.isMember (jss::ledger_indexes) ||
            params.isMember (jss::ledger_index_range);
    if (! needsLedger)
        return Status::OK;

    if (auto s = lookupLedgers (ledgers_, context_))
        return s;

    bool const full = params[jss::full].asBool();
    bool const transactions = params[jss::transactions].asBool();
    bool const accounts = params[jss::accounts].asBool();
    bool const expand = params[jss::expand].asBool();
    bool const binary = params[jss::binary].asBool();
    bool const owner_funds = params[jss::owner_funds].asBool();

    options_ = (full ? LedgerFill::full : 0)
            | (expand ? LedgerFill::expand : 0)
            | (transactions ? LedgerFill::dumpTxrp : 0)
            | (accounts ? LedgerFill::dumpState : 0)
            | (binary ? LedgerFill::binary : 0)
            | (owner_funds ? LedgerFill::ownerFunds : 0);

    if (full || accounts)
    {
        // Until some sane way to get full ledgers has been implemented,
        // disallow retrieving all state nodes.
        if (! isUnlimited (context_.role))
            return rpcNO_PERMISSION;

        if (context_.app.getFeeTrack().isLoadedLocal() &&
            ! isUnlimited (context_.role))
        {
            return rpcTOO_BUSY;
        }
        context_.loadType = binary ? Resource::feeMediumBurdenRPC :
            Resource::feeHighBurdenRPC;
    }

    return Status::OK;
}

} // RPC
} // ripple
