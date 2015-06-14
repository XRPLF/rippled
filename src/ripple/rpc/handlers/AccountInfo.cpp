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
#include <ripple/protocol/Indexes.h>

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

    auto jvAccepted = RPC::accountFromString (
        naAccount, bIndex, strIdent, iIndex, bStrict);

    if (!jvAccepted.empty ())
        return jvAccepted;

    AccountState::pointer asAccepted =
        getAccountState(*ledger, naAccount,
            getApp().getSLECache());

    if (asAccepted)
    {
        asAccepted->addJson (jvAccepted);

        // See if there's a SignerEntries for this account.
        AccountID const account = naAccount.getAccountID ();
        uint256 const signerListIndex = getSignerListIndex (account);
        auto const signerList = fetch(*ledger, signerListIndex,
            getApp().getSLECache());

        if (signerList)
        {
            // Return multi-signing information if there are multi-signers.
            static const Json::StaticString multiSignersName("multisigners");
            jvAccepted[multiSignersName] = signerList->getJson (0);
            Json::Value& multiSignerJson = jvAccepted[multiSignersName];

            // Remove unwanted fields.
            multiSignerJson.removeMember (sfFlags.getName ());
            multiSignerJson.removeMember (sfLedgerEntryType.getName ());
            multiSignerJson.removeMember (sfOwnerNode.getName ());
            multiSignerJson.removeMember ("index");
        }
        result[jss::account_data] = jvAccepted;
    }
    else
    {
        result[jss::account] = naAccount.humanAccountID ();
        RPC::inject_error (rpcACT_NOT_FOUND, result);
    }

    return result;
}

} // ripple
