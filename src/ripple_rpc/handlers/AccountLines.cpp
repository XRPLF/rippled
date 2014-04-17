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
//   account: <account>|<nickname>|<account_public_key>
//   account_index: <number>        // optional, defaults to 0.
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
// }
Json::Value RPCHandler::doAccountLines (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();

    Ledger::pointer     lpLedger;
    Json::Value         jvResult    = RPC::lookupLedger (params, lpLedger, *mNetOps);

    if (!lpLedger)
        return jvResult;

    if (!params.isMember ("account"))
        return RPC::missing_field_error ("account");

    std::string     strIdent    = params["account"].asString ();
    bool            bIndex      = params.isMember ("account_index");
    int             iIndex      = bIndex ? params["account_index"].asUInt () : 0;

    RippleAddress   raAccount;

    jvResult    = RPC::accountFromString (lpLedger, raAccount, bIndex, strIdent, iIndex, false, *mNetOps);

    if (!jvResult.empty ())
        return jvResult;

    std::string     strPeer     = params.isMember ("peer") ? params["peer"].asString () : "";
    bool            bPeerIndex      = params.isMember ("peer_index");
    int             iPeerIndex      = bIndex ? params["peer_index"].asUInt () : 0;

    RippleAddress   raPeer;

    if (!strPeer.empty ())
    {
        jvResult["peer"]    = raAccount.humanAccountID ();

        if (bPeerIndex)
            jvResult["peer_index"]  = iPeerIndex;

        jvResult    = RPC::accountFromString (lpLedger, raPeer, bPeerIndex, strPeer, iPeerIndex, false, *mNetOps);

        if (!jvResult.empty ())
            return jvResult;
    }

    if (lpLedger->hasAccount (raAccount))
    {
        AccountItems rippleLines (raAccount.getAccountID (), lpLedger, AccountItem::pointer (new RippleState ()));

        jvResult["account"] = raAccount.humanAccountID ();
        Json::Value& jsonLines = (jvResult["lines"] = Json::arrayValue);


        BOOST_FOREACH (AccountItem::ref item, rippleLines.getItems ())
        {
            RippleState* line = (RippleState*)item.get ();

            if (!raPeer.isValid () || raPeer.getAccountID () == line->getAccountIDPeer ())
            {
                const STAmount&     saBalance   = line->getBalance ();
                const STAmount&     saLimit     = line->getLimit ();
                const STAmount&     saLimitPeer = line->getLimitPeer ();

                Json::Value&    jPeer   = jsonLines.append (Json::objectValue);

                jPeer["account"]        = RippleAddress::createHumanAccountID (line->getAccountIDPeer ());
                // Amount reported is positive if current account holds other account's IOUs.
                // Amount reported is negative if other account holds current account's IOUs.
                jPeer["balance"]        = saBalance.getText ();
                jPeer["currency"]       = saBalance.getHumanCurrency ();
                jPeer["limit"]          = saLimit.getText ();
                jPeer["limit_peer"]     = saLimitPeer.getText ();
                jPeer["quality_in"]     = static_cast<Json::UInt> (line->getQualityIn ());
                jPeer["quality_out"]    = static_cast<Json::UInt> (line->getQualityOut ());
                if (line->getAuth())
                    jPeer["authorized"] = true;
                if (line->getAuthPeer())
                    jPeer["peer_authorized"] = true;
                if (line->getNoRipple())
                    jPeer["no_ripple"]  = true;
                if (line->getNoRipplePeer())
                    jPeer["no_ripple_peer"] = true;
            }
        }

        loadType = Resource::feeMediumBurdenRPC;
    }
    else
    {
        jvResult    = rpcError (rpcACT_NOT_FOUND);
    }

    return jvResult;
}

} // ripple
