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

#include <ripple/app/main/Application.h>
#include <ripple/json/json_value.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/types.h>
#include <ripple/rpc/impl/Utilities.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/AccountFromString.h>
#include <ripple/rpc/impl/LookupLedger.h>

namespace ripple {

// {
//   account: <indent>,
//   strict: <bool>
//           if true, only allow public keys and addresses. false, default.
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   signer_lists : <true | false>  // optional -- if true return SignerList(s)
// }

// TODO(tom): what is that "default"?
Json::Value doAccountInfo (RPC::Context& context)
{
    auto& params = context.params;

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger (ledger, context);

    if (!ledger)
        return result;

    if (!params.isMember (jss::account) && !params.isMember (jss::ident))
        return RPC::missing_field_error (jss::account);

    std::string strIdent = params.isMember (jss::account)
            ? params[jss::account].asString () : params[jss::ident].asString ();
    bool bStrict = params.isMember (jss::strict) && params[jss::strict].asBool ();
    AccountID accountID;

    // Get info on account.

    auto jvAccepted = RPC::accountFromString (accountID, strIdent, bStrict);

    if (jvAccepted)
        return jvAccepted;

    auto const sleAccepted = ledger->read(keylet::account(accountID));
    if (sleAccepted)
    {
        RPC::injectSLE(jvAccepted, *sleAccepted);
        result[jss::account_data] = jvAccepted;

        // Return SignerList(s) if that is requested.
        if (params.isMember (jss::signer_lists) &&
            params[jss::signer_lists].asBool ())
        {
            // We put the SignerList in an array because of an anticipated
            // future when we support multiple signer lists on one account.
            auto& jvSignerList = result[jss::account_data][jss::signer_lists];
            jvSignerList = Json::arrayValue;

            // This code will need to be revisited if in the future we support
            // multiple SignerLists on one account.
            auto const sleSigners = ledger->read (keylet::signers (accountID));
            if (sleSigners)
            {
                auto& listJson = jvSignerList.append (sleSigners->getJson (0));
            }
        }
    }
    else
    {
        result[jss::account] = context.app.accountIDCache().toBase58 (accountID);
        RPC::inject_error (rpcACT_NOT_FOUND, result);
    }

    return result;
}

} // ripple
