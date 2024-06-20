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
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/json/json_value.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/RPCErr.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/RPCHelpers.h>

namespace ripple {

// {
//   'ident' : <indent>,
// }
Json::Value
doOwnerInfo(RPC::JsonContext& context)
{
    if (!context.params.isMember(jss::account) &&
        !context.params.isMember(jss::ident))
    {
        return RPC::missing_field_error(jss::account);
    }

    std::string strIdent = context.params.isMember(jss::account)
        ? context.params[jss::account].asString()
        : context.params[jss::ident].asString();
    Json::Value ret;

    // Get info on account.
    auto const& closedLedger = context.ledgerMaster.getClosedLedger();
    std::optional<AccountID> const accountID = parseBase58<AccountID>(strIdent);
    ret[jss::accepted] = accountID.has_value()
        ? context.netOps.getOwnerInfo(closedLedger, accountID.value())
        : rpcError(rpcACT_MALFORMED);

    auto const& currentLedger = context.ledgerMaster.getCurrentLedger();
    ret[jss::current] = accountID.has_value()
        ? context.netOps.getOwnerInfo(currentLedger, *accountID)
        : rpcError(rpcACT_MALFORMED);
    return ret;
}

}  // namespace ripple
