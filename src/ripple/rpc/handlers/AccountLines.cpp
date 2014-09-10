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

#include <ripple/rpc/impl/Tuning.h>

namespace ripple {

// {
//   account: <account>|<account_public_key>
//   account_index: <number>        // optional, defaults to 0.
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   limit: integer                 // optional
//   marker: opaque                 // optional, resume previous query
// }
Json::Value doAccountLines (RPC::Context& context)
{
    auto& params = context.params_;

    Ledger::pointer ledger;
    Json::Value result = RPC::lookupLedger (params, ledger, context.netOps_);

    if (!ledger)
        return result;

    if (!params.isMember (jss::account))
        return RPC::missing_field_error ("account");

    std::string strIdent = params[jss::account].asString ();
    bool bIndex = params.isMember (jss::account_index);
    int iIndex = bIndex ? params[jss::account_index].asUInt () : 0;

    RippleAddress   raAccount;

    result = RPC::accountFromString (
        ledger, raAccount, bIndex, strIdent, iIndex, false, context.netOps_);

    if (!result.empty ())
        return result;

    std::string strPeer = params.isMember (jss::peer)
            ? params[jss::peer].asString () : "";
    bool bPeerIndex = params.isMember (jss::peer_index);
    int iPeerIndex = bIndex ? params[jss::peer_index].asUInt () : 0;

    RippleAddress   raPeer;

    if (!strPeer.empty ())
    {
        result[jss::peer]    = raAccount.humanAccountID ();

        if (bPeerIndex)
            result[jss::peer_index]  = iPeerIndex;

        result = RPC::accountFromString (
            ledger, raPeer, bPeerIndex, strPeer, iPeerIndex, false,
            context.netOps_);

        if (!result.empty ())
            return result;
    }

    unsigned int limit;
    if (params.isMember (jss::limit))
    {
        limit = std::max (RPC::Tuning::minLinesPerRequest,
            std::min (params[jss::limit].asUInt (),
            RPC::Tuning::maxLinesPerRequest));
    }
    else
    {
        limit = RPC::Tuning::defaultLinesPerRequest;
    }

    RippleAddress resumeAddress;    
    if (params.isMember (jss::marker))
    {
        if (!resumeAddress.setAccountID (params[jss::marker].asString ()))
            return rpcError (rpcACT_MALFORMED);
    }

    if (ledger->hasAccount (raAccount))
    {
        result[jss::account] = raAccount.humanAccountID ();
        Json::Value& jsonLines = (result[jss::lines] = Json::arrayValue);

        bool resume (! resumeAddress.isValid ());
        unsigned int i (0);

        for (auto const& item : getRippleStateItems (raAccount.getAccountID (), ledger))
        {
            RippleState const& line (*item.get ());
            Account const& lineAccount (line.getAccountIDPeer ());
            
            if (! resume && resumeAddress.getAccountID () == lineAccount)
                resume = true;

            if (resume &&
                (!raPeer.isValid () || raPeer.getAccountID () == lineAccount))
            {
                if (i < limit)
                {
                    STAmount const& saBalance = line.getBalance ();
                    STAmount const& saLimit = line.getLimit ();
                    STAmount const& saLimitPeer = line.getLimitPeer ();

                    Json::Value& jPeer = jsonLines.append (Json::objectValue);

                    jPeer[jss::account] = to_string (lineAccount);
                    // Amount reported is positive if current account holds other
                    // account's IOUs.
                    //
                    // Amount reported is negative if other account holds current
                    // account's IOUs.
                    jPeer[jss::balance] = saBalance.getText ();
                    jPeer[jss::currency] = saBalance.getHumanCurrency ();
                    jPeer[jss::limit] = saLimit.getText ();
                    jPeer[jss::limit_peer] = saLimitPeer.getText ();
                    jPeer[jss::quality_in]
                        = static_cast<Json::UInt> (line.getQualityIn ());
                    jPeer[jss::quality_out]
                        = static_cast<Json::UInt> (line.getQualityOut ());
                    if (line.getAuth ())
                        jPeer[jss::authorized] = true;
                    if (line.getAuthPeer ())
                        jPeer[jss::peer_authorized] = true;
                    if (line.getNoRipple ())
                        jPeer[jss::no_ripple] = true;
                    if (line.getNoRipplePeer ())
                        jPeer[jss::no_ripple_peer] = true;
                    if (line.getFreeze ())
                        jPeer[jss::freeze] = true;
                    if (line.getFreezePeer ())
                        jPeer[jss::freeze_peer] = true;
                    
                    ++i;
                }
                else
                {
                    result[jss::limit] = limit;
                    result[jss::marker] = to_string (lineAccount);
                    break;
                }
            }
        }

        if (! resume)
            return rpcError (rpcACT_MALFORMED);

        context.loadType_ = Resource::feeMediumBurdenRPC;
    }
    else
    {
        result    = rpcError (rpcACT_NOT_FOUND);
    }

    return result;
}

} // ripple
