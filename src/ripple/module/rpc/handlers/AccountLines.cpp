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
Json::Value doAccountLines (RPC::Context& context)
{
    context.lock_.unlock ();

    Ledger::pointer     lpLedger;
    Json::Value         jvResult    = RPC::lookupLedger (context.params_, lpLedger, context.netOps_);

    if (!lpLedger)
        return jvResult;

    if (!context.params_.isMember (jss::account))
        return RPC::missing_field_error ("account");

    std::string     strIdent    = context.params_[jss::account].asString ();
    bool            bIndex      = context.params_.isMember (jss::account_index);
    int             iIndex      = bIndex ? context.params_[jss::account_index].asUInt () : 0;

    RippleAddress   raAccount;

    jvResult    = RPC::accountFromString (lpLedger, raAccount, bIndex, strIdent, iIndex, false, context.netOps_);

    if (!jvResult.empty ())
        return jvResult;

    std::string     strPeer     = context.params_.isMember (jss::peer) ? context.params_[jss::peer].asString () : "";
    bool            bPeerIndex      = context.params_.isMember (jss::peer_index);
    int             iPeerIndex      = bIndex ? context.params_[jss::peer_index].asUInt () : 0;

    RippleAddress   raPeer;

    if (!strPeer.empty ())
    {
        jvResult[jss::peer]    = raAccount.humanAccountID ();

        if (bPeerIndex)
            jvResult[jss::peer_index]  = iPeerIndex;

        jvResult    = RPC::accountFromString (lpLedger, raPeer, bPeerIndex, strPeer, iPeerIndex, false, context.netOps_);

        if (!jvResult.empty ())
            return jvResult;
    }

    if (lpLedger->hasAccount (raAccount))
    {
        AccountItems rippleLines (raAccount.getAccountID (), lpLedger, AccountItem::pointer (new RippleState ()));

        jvResult[jss::account] = raAccount.humanAccountID ();
        Json::Value& jsonLines = (jvResult[jss::lines] = Json::arrayValue);


        BOOST_FOREACH (AccountItem::ref item, rippleLines.getItems ())
        {
            RippleState* line = (RippleState*)item.get ();

            if (!raPeer.isValid () || raPeer.getAccountID () == line->getAccountIDPeer ())
            {
                const STAmount&     saBalance   = line->getBalance ();
                const STAmount&     saLimit     = line->getLimit ();
                const STAmount&     saLimitPeer = line->getLimitPeer ();

                Json::Value&    jPeer   = jsonLines.append (Json::objectValue);

                jPeer[jss::account]       = RippleAddress::createHumanAccountID (line->getAccountIDPeer ());
                // Amount reported is positive if current account holds other account's IOUs.
                // Amount reported is negative if other account holds current account's IOUs.
                jPeer[jss::balance]       = saBalance.getText ();
                jPeer[jss::currency]      = saBalance.getHumanCurrency ();
                jPeer[jss::limit]         = saLimit.getText ();
                jPeer[jss::limit_peer]     = saLimitPeer.getText ();
                jPeer[jss::quality_in]     = static_cast<Json::UInt> (line->getQualityIn ());
                jPeer[jss::quality_out]    = static_cast<Json::UInt> (line->getQualityOut ());
                if (line->getAuth())
                    jPeer[jss::authorized] = true;
                if (line->getAuthPeer())
                    jPeer[jss::peer_authorized] = true;
                if (line->getNoRipple())
                    jPeer[jss::no_ripple]  = true;
                if (line->getNoRipplePeer())
                    jPeer[jss::no_ripple_peer] = true;
            }
        }

        context.loadType_ = Resource::feeMediumBurdenRPC;
    }
    else
    {
        jvResult    = rpcError (rpcACT_NOT_FOUND);
    }

    return jvResult;
}

} // ripple
