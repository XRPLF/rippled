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
#include <ripple/rpc/impl/AccountFromString.h>
#include <ripple/rpc/impl/LookupLedger.h>
#include <ripple/app/paths/RippleState.h>

namespace ripple {

Json::Value doAccountCurrencies (RPC::Context& context)
{
    auto& params = context.params;

    // Get the current ledger
    Ledger::pointer ledger;
    Json::Value result (RPC::lookupLedger (params, ledger, context.netOps));

    if (!ledger)
        return result;

    if (!(params.isMember ("account") || params.isMember ("ident")))
        return RPC::missing_field_error ("account");

    std::string const strIdent (params.isMember ("account")
        ? params["account"].asString ()
        : params["ident"].asString ());

    int const iIndex (params.isMember ("account_index")
        ? params["account_index"].asUInt ()
        : 0);
    bool const bStrict = params.isMember ("strict") &&
            params["strict"].asBool ();

    // Get info on account.
    bool bIndex; // out param
    RippleAddress naAccount; // out param
    Json::Value jvAccepted (RPC::accountFromString (
        ledger, naAccount, bIndex, strIdent, iIndex, bStrict, context.netOps));

    if (!jvAccepted.empty ())
        return jvAccepted;

    std::set<Currency> send, receive;
    for (auto const& item : getRippleStateItems (naAccount.getAccountID (), ledger))
    {
        RippleState* rspEntry = (RippleState*) item.get ();
        STAmount const& saBalance = rspEntry->getBalance ();

        if (saBalance < rspEntry->getLimit ())
            receive.insert (saBalance.getCurrency ());
        if ((-saBalance) < rspEntry->getLimitPeer ())
            send.insert (saBalance.getCurrency ());
    }

    send.erase (badCurrency());
    receive.erase (badCurrency());

    Json::Value& sendCurrencies =
            (result["send_currencies"] = Json::arrayValue);
    for (auto const& c: send)
        sendCurrencies.append (to_string (c));

    Json::Value& recvCurrencies =
            (result["receive_currencies"] = Json::arrayValue);
    for (auto const& c: receive)
        recvCurrencies.append (to_string (c));

    return result;
}

} // ripple
