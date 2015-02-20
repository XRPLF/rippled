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
//   account: <indent>,
//   account_index : <index> // optional
//   strict: <bool>
//           if true, only allow public keys and addresses. false, default.
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
// }

// TODO(tom): what is that "default"?
Json::Value doAccountInfo (RPC::Context& context)
{
    auto& params = context.params;

    Ledger::pointer ledger;
    Json::Value result = RPC::lookupLedger (params, ledger, context.netOps);

    if (!ledger)
        return result;

    if (!params.isMember (jss::account) && !params.isMember (jss::ident))
        return RPC::missing_field_error (jss::account);

    std::string strIdent = params.isMember (jss::account)
            ? params[jss::account].asString () : params[jss::ident].asString ();
    bool bIndex;
    int iIndex = params.isMember (jss::account_index)
            ? params[jss::account_index].asUInt () : 0;
    bool bStrict = params.isMember (jss::strict) && params[jss::strict].asBool ();
    RippleAddress naAccount;

    // Get info on account.

    Json::Value jvAccepted = RPC::accountFromString (
        ledger, naAccount, bIndex, strIdent, iIndex, bStrict, context.netOps);

    if (!jvAccepted.empty ())
        return jvAccepted;

    auto asAccepted = context.netOps.getAccountState (ledger, naAccount);

    if (asAccepted)
    {
        asAccepted->addJson (jvAccepted);
        result[jss::account_data]    = jvAccepted;
    }
    else
    {
        result[jss::account] = naAccount.humanAccountID ();
        RPC::inject_error (rpcACT_NOT_FOUND, result);
    }

    return result;
}

} // ripple
