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
//   'ident' : <indent>,
//   'account_index' : <index> // optional
// }
// XXX This would be better if it took the ledger.
Json::Value doOwnerInfo (RPC::Context& context)
{
    if (!context.params_.isMember ("account") && !context.params_.isMember ("ident"))
        return RPC::missing_field_error ("account");

    std::string     strIdent    = context.params_.isMember ("account") ? context.params_["account"].asString () : context.params_["ident"].asString ();
    bool            bIndex;
    int             iIndex      = context.params_.isMember ("account_index") ? context.params_["account_index"].asUInt () : 0;
    RippleAddress   raAccount;

    Json::Value     ret;

    // Get info on account.

    Json::Value     jAccepted   = RPC::accountFromString (context.netOps_.getClosedLedger (), raAccount, bIndex, strIdent, iIndex, false, context.netOps_);

    ret["accepted"] = jAccepted.empty () ? context.netOps_.getOwnerInfo (context.netOps_.getClosedLedger (), raAccount) : jAccepted;

    Json::Value     jCurrent    = RPC::accountFromString (context.netOps_.getCurrentLedger (), raAccount, bIndex, strIdent, iIndex, false, context.netOps_);

    ret["current"]  = jCurrent.empty () ? context.netOps_.getOwnerInfo (context.netOps_.getCurrentLedger (), raAccount) : jCurrent;

    return ret;
}

} // ripple
