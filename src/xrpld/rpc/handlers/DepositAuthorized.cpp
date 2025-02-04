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

#include <xrpld/app/misc/CredentialHelpers.h>
#include <xrpld/ledger/ReadView.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

// {
//   source_account : <ident>
//   destination_account : <ident>
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   credentials : [<credentialID>,...]
// }

Json::Value
doDepositAuthorized(RPC::JsonContext& context)
{
    Json::Value const& params = context.params;

    // Validate source_account.
    if (!params.isMember(jss::source_account))
        return RPC::missing_field_error(jss::source_account);
    if (!params[jss::source_account].isString())
        return RPC::make_error(
            rpcINVALID_PARAMS,
            RPC::expected_field_message(jss::source_account, "a string"));

    auto srcID = parseBase58<AccountID>(params[jss::source_account].asString());
    if (!srcID)
        return rpcError(rpcACT_MALFORMED);
    auto const srcAcct{std::move(srcID.value())};

    // Validate destination_account.
    if (!params.isMember(jss::destination_account))
        return RPC::missing_field_error(jss::destination_account);
    if (!params[jss::destination_account].isString())
        return RPC::make_error(
            rpcINVALID_PARAMS,
            RPC::expected_field_message(jss::destination_account, "a string"));

    auto dstID =
        parseBase58<AccountID>(params[jss::destination_account].asString());
    if (!dstID)
        return rpcError(rpcACT_MALFORMED);
    auto const dstAcct{std::move(dstID.value())};

    // Validate ledger.
    std::shared_ptr<ReadView const> ledger;
    Json::Value result = RPC::lookupLedger(ledger, context);

    if (!ledger)
        return result;

    // If source account is not in the ledger it can't be authorized.
    if (!ledger->exists(keylet::account(srcAcct)))
    {
        RPC::inject_error(rpcSRC_ACT_NOT_FOUND, result);
        return result;
    }

    // If destination account is not in the ledger you can't deposit to it, eh?
    auto const sleDest = ledger->read(keylet::account(dstAcct));
    if (!sleDest)
    {
        RPC::inject_error(rpcDST_ACT_NOT_FOUND, result);
        return result;
    }

    bool const reqAuth =
        (sleDest->getFlags() & lsfDepositAuth) && (srcAcct != dstAcct);
    bool const credentialsPresent = params.isMember(jss::credentials);

    std::set<std::pair<AccountID, Slice>> sorted;
    std::vector<std::shared_ptr<SLE const>> lifeExtender;
    if (credentialsPresent)
    {
        auto const& creds(params[jss::credentials]);
        if (!creds.isArray() || !creds)
        {
            return RPC::make_error(
                rpcINVALID_PARAMS,
                RPC::expected_field_message(
                    jss::credentials,
                    "is non-empty array of CredentialID(hash256)"));
        }
        else if (creds.size() > maxCredentialsArraySize)
        {
            return RPC::make_error(
                rpcINVALID_PARAMS,
                RPC::expected_field_message(
                    jss::credentials, "array too long"));
        }

        lifeExtender.reserve(creds.size());
        for (auto const& jo : creds)
        {
            if (!jo.isString())
            {
                return RPC::make_error(
                    rpcINVALID_PARAMS,
                    RPC::expected_field_message(
                        jss::credentials, "an array of CredentialID(hash256)"));
            }

            uint256 credH;
            auto const credS = jo.asString();
            if (!credH.parseHex(credS))
            {
                return RPC::make_error(
                    rpcINVALID_PARAMS,
                    RPC::expected_field_message(
                        jss::credentials, "an array of CredentialID(hash256)"));
            }

            std::shared_ptr<SLE const> sleCred =
                ledger->read(keylet::credential(credH));
            if (!sleCred)
            {
                RPC::inject_error(
                    rpcBAD_CREDENTIALS, "credentials don't exist", result);
                return result;
            }

            if (!(sleCred->getFlags() & lsfAccepted))
            {
                RPC::inject_error(
                    rpcBAD_CREDENTIALS, "credentials aren't accepted", result);
                return result;
            }

            if (credentials::checkExpired(
                    sleCred, ledger->info().parentCloseTime))
            {
                RPC::inject_error(
                    rpcBAD_CREDENTIALS, "credentials are expired", result);
                return result;
            }

            if ((*sleCred)[sfSubject] != srcAcct)
            {
                RPC::inject_error(
                    rpcBAD_CREDENTIALS,
                    "credentials doesn't belong to the root account",
                    result);
                return result;
            }

            auto [it, ins] = sorted.emplace(
                (*sleCred)[sfIssuer], (*sleCred)[sfCredentialType]);
            if (!ins)
            {
                RPC::inject_error(
                    rpcBAD_CREDENTIALS, "duplicates in credentials", result);
                return result;
            }
            lifeExtender.push_back(std::move(sleCred));
        }
    }

    // If the two accounts are the same OR if that flag is
    // not set, then the deposit should be fine.
    bool depositAuthorized = true;
    if (reqAuth)
        depositAuthorized =
            ledger->exists(keylet::depositPreauth(dstAcct, srcAcct)) ||
            (credentialsPresent &&
             ledger->exists(keylet::depositPreauth(dstAcct, sorted)));

    result[jss::source_account] = params[jss::source_account].asString();
    result[jss::destination_account] =
        params[jss::destination_account].asString();
    if (credentialsPresent)
        result[jss::credentials] = params[jss::credentials];

    result[jss::deposit_authorized] = depositAuthorized;
    return result;
}

}  // namespace ripple
