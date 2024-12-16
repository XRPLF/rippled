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

#include <xrpld/app/main/Application.h>
#include <xrpld/app/paths/TrustLine.h>
#include <xrpld/ledger/ReadView.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

Json::Value
doAccountCurrencies(RPC::JsonContext& context)
{
    auto& params = context.params;

    if (!(params.isMember(jss::account) || params.isMember(jss::ident)))
        return RPC::missing_field_error(jss::account);

    std::string strIdent;
    if (params.isMember(jss::account))
    {
        if (!params[jss::account].isString())
            return RPC::invalid_field_error(jss::account);
        strIdent = params[jss::account].asString();
    }
    else if (params.isMember(jss::ident))
    {
        if (!params[jss::ident].isString())
            return RPC::invalid_field_error(jss::ident);
        strIdent = params[jss::ident].asString();
    }

    // Get the current ledger
    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);
    if (!ledger)
        return result;

    // Get info on account.
    auto id = parseBase58<AccountID>(strIdent);
    if (!id)
    {
        RPC::inject_error(rpcACT_MALFORMED, result);
        return result;
    }
    auto const accountID{std::move(id.value())};

    if (!ledger->exists(keylet::account(accountID)))
        return rpcError(rpcACT_NOT_FOUND);

    std::set<Currency> send, receive;
    for (auto const& rspEntry : RPCTrustLine::getItems(accountID, *ledger))
    {
        STAmount const& saBalance = rspEntry.getBalance();

        if (saBalance < rspEntry.getLimit())
            receive.insert(saBalance.getCurrency());
        if ((-saBalance) < rspEntry.getLimitPeer())
            send.insert(saBalance.getCurrency());
    }

    send.erase(badCurrency());
    receive.erase(badCurrency());

    Json::Value& sendCurrencies =
        (result[jss::send_currencies] = Json::arrayValue);
    for (auto const& c : send)
        sendCurrencies.append(to_string(c));

    Json::Value& recvCurrencies =
        (result[jss::receive_currencies] = Json::arrayValue);
    for (auto const& c : receive)
        recvCurrencies.append(to_string(c));

    return result;
}

}  // namespace ripple
