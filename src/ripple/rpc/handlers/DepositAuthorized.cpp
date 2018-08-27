//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

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


#include <ripple/ledger/ReadView.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/RPCHelpers.h>

namespace ripple {

// {
//   source_account : <ident>
//   destination_account : <ident>
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
// }

Json::Value doDepositAuthorized (RPC::Context& context)
{
    Json::Value const& params = context.params;

    // Validate source_account.
    if (! params.isMember (jss::source_account))
        return RPC::missing_field_error (jss::source_account);
    if (! params[jss::source_account].isString())
        return RPC::make_error (rpcINVALID_PARAMS,
            RPC::expected_field_message (jss::source_account,
                "a string"));

    AccountID srcAcct;
    {
        Json::Value const jvAccepted = RPC::accountFromString (
            srcAcct, params[jss::source_account].asString(), true);
        if (jvAccepted)
            return jvAccepted;
    }

    // Validate destination_account.
    if (! params.isMember (jss::destination_account))
        return RPC::missing_field_error (jss::destination_account);
    if (! params[jss::destination_account].isString())
        return RPC::make_error (rpcINVALID_PARAMS,
            RPC::expected_field_message (jss::destination_account,
                "a string"));

    AccountID dstAcct;
    {
        Json::Value const jvAccepted = RPC::accountFromString (
            dstAcct, params[jss::destination_account].asString(), true);
        if (jvAccepted)
            return jvAccepted;
    }

    // Validate ledger.
    std::shared_ptr<ReadView const> ledger;
    Json::Value result = RPC::lookupLedger (ledger, context);

    if (!ledger)
        return result;

    // If source account is not in the ledger it can't be authorized.
    if (! ledger->exists (keylet::account(srcAcct)))
    {
        RPC::inject_error (rpcSRC_ACT_NOT_FOUND, result);
        return result;
    }

    // If destination account is not in the ledger you can't deposit to it, eh?
    auto const sleDest = ledger->read (keylet::account(dstAcct));
    if (! sleDest)
    {
        RPC::inject_error (rpcDST_ACT_NOT_FOUND, result);
        return result;
    }

    // If the two accounts are the same, then the deposit should be fine.
    bool depositAuthorized {true};
    if (srcAcct != dstAcct)
    {
        // Check destination for the DepositAuth flag.  If that flag is
        // not set then a deposit should be just fine.
        if (sleDest->getFlags() & lsfDepositAuth)
        {
            // See if a preauthorization entry is in the ledger.
            auto const sleDepositAuth =
                ledger->read(keylet::depositPreauth (dstAcct, srcAcct));
            depositAuthorized = static_cast<bool>(sleDepositAuth);
        }
    }
    result[jss::source_account] = params[jss::source_account].asString();
    result[jss::destination_account] =
        params[jss::destination_account].asString();

    result[jss::deposit_authorized] = depositAuthorized;
    return result;
}

} // ripple
