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

Json::Value RPCHandler::doAccountCurrencies (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();
    // Get the current ledger
    Ledger::pointer lpLedger;
    Json::Value jvResult (RPC::lookupLedger (params, lpLedger, *mNetOps));
    if (!lpLedger)
        return jvResult;

    if (! params.isMember ("account") && ! params.isMember ("ident"))
        return RPC::missing_field_error ("account");

    std::string const strIdent (params.isMember ("account")
        ? params["account"].asString ()
        : params["ident"].asString ());

    int const iIndex (params.isMember ("account_index")
        ? params["account_index"].asUInt ()
        : 0);
    bool const bStrict (params.isMember ("strict") && params["strict"].asBool ());

    // Get info on account.
    bool bIndex; // out param
    RippleAddress naAccount; // out param
    Json::Value jvAccepted (RPC::accountFromString (
        lpLedger, naAccount, bIndex, strIdent, iIndex, bStrict, *mNetOps));

    if (!jvAccepted.empty ())
        return jvAccepted;

    std::set<uint160> send, receive;
    AccountItems rippleLines (naAccount.getAccountID (), lpLedger, AccountItem::pointer (new RippleState ()));
    BOOST_FOREACH(AccountItem::ref item, rippleLines.getItems ())
    {
        RippleState* rspEntry = (RippleState*) item.get ();
        const STAmount& saBalance = rspEntry->getBalance ();

        if (saBalance < rspEntry->getLimit ())
            receive.insert (saBalance.getCurrency ());
        if ((-saBalance) < rspEntry->getLimitPeer ())
            send.insert (saBalance.getCurrency ());
    }


    send.erase (CURRENCY_BAD);
    receive.erase (CURRENCY_BAD);

    Json::Value& sendCurrencies = (jvResult["send_currencies"] = Json::arrayValue);
    BOOST_FOREACH(uint160 const& c, send)
    {
        sendCurrencies.append (STAmount::createHumanCurrency (c));
    }

    Json::Value& recvCurrencies = (jvResult["receive_currencies"] = Json::arrayValue);
    BOOST_FOREACH(uint160 const& c, receive)
    {
        recvCurrencies.append (STAmount::createHumanCurrency (c));
    }


    return jvResult;
}

} // ripple
