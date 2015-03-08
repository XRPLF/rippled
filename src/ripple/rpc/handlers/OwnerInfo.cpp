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

namespace ripple {

// {
//   'ident' : <indent>,
//   'account_index' : <index> // optional
// }
Json::Value doOwnerInfo (RPC::Context& context)
{
    if (!context.params.isMember (jss::account) &&
        !context.params.isMember (jss::ident))
    {
        return RPC::missing_field_error (jss::account);
    }

    std::string strIdent = context.params.isMember (jss::account)
            ? context.params[jss::account].asString ()
            : context.params[jss::ident].asString ();
    bool bIndex;
    int iIndex = context.params.isMember (jss::account_index)
            ? context.params[jss::account_index].asUInt () : 0;
    RippleAddress raAccount;
    Json::Value ret;

    // Get info on account.

    auto const& closedLedger = context.netOps.getClosedLedger ();
    Json::Value jAccepted = RPC::accountFromString (
        closedLedger,
        raAccount,
        bIndex,
        strIdent,
        iIndex,
        false,
        context.netOps);

    ret[jss::accepted] = jAccepted.empty () ? context.netOps.getOwnerInfo (
        closedLedger, raAccount) : jAccepted;

    auto const& currentLedger = context.netOps.getCurrentLedger ();
    Json::Value jCurrent = RPC::accountFromString (
        currentLedger,
        raAccount,
        bIndex,
        strIdent,
        iIndex,
        false,
        context.netOps);

    ret[jss::current] = jCurrent.empty () ? context.netOps.getOwnerInfo (
        currentLedger, raAccount) : jCurrent;

    return ret;
}

} // ripple
