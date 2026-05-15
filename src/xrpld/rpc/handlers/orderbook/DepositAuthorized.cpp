#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>

#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace xrpl {

// {
//   source_account : <ident>
//   destination_account : <ident>
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   credentials : [<credentialID>,...]
// }

json::Value
doDepositAuthorized(RPC::JsonContext& context)
{
    json::Value const& params = context.params;

    // Validate source_account.
    if (!params.isMember(jss::source_account))
        return RPC::missingFieldError(jss::source_account);
    if (!params[jss::source_account].isString())
    {
        return RPC::makeError(
            RpcInvalidParams, RPC::expectedFieldMessage(jss::source_account, "a string"));
    }

    auto srcID = parseBase58<AccountID>(params[jss::source_account].asString());
    if (!srcID)
        return rpcError(RpcActMalformed);
    auto const srcAcct{srcID.value()};

    // Validate destination_account.
    if (!params.isMember(jss::destination_account))
        return RPC::missingFieldError(jss::destination_account);
    if (!params[jss::destination_account].isString())
    {
        return RPC::makeError(
            RpcInvalidParams, RPC::expectedFieldMessage(jss::destination_account, "a string"));
    }

    auto dstID = parseBase58<AccountID>(params[jss::destination_account].asString());
    if (!dstID)
        return rpcError(RpcActMalformed);
    auto const dstAcct{dstID.value()};

    // Validate ledger.
    std::shared_ptr<ReadView const> ledger;
    json::Value result = RPC::lookupLedger(ledger, context);

    if (!ledger)
        return result;

    // If source account is not in the ledger it can't be authorized.
    if (!ledger->exists(keylet::account(srcAcct)))
    {
        RPC::injectError(RpcSrcActNotFound, result);
        return result;
    }

    // If destination account is not in the ledger you can't deposit to it, eh?
    auto const sleDest = ledger->read(keylet::account(dstAcct));
    if (!sleDest)
    {
        RPC::injectError(RpcDstActNotFound, result);
        return result;
    }

    bool const reqAuth = sleDest->isFlag(lsfDepositAuth) && (srcAcct != dstAcct);
    bool const credentialsPresent = params.isMember(jss::credentials);

    std::set<std::pair<AccountID, Slice>> sorted;
    std::vector<std::shared_ptr<SLE const>> lifeExtender;
    if (credentialsPresent)
    {
        auto const& creds(params[jss::credentials]);
        if (!creds.isArray() || !creds)
        {
            return RPC::makeError(
                RpcInvalidParams,
                RPC::expectedFieldMessage(
                    jss::credentials, "is non-empty array of CredentialID(hash256)"));
        }
        if (creds.size() > kMAX_CREDENTIALS_ARRAY_SIZE)
        {
            return RPC::makeError(
                RpcInvalidParams, RPC::expectedFieldMessage(jss::credentials, "array too long"));
        }

        lifeExtender.reserve(creds.size());
        for (auto const& jo : creds)
        {
            if (!jo.isString())
            {
                return RPC::makeError(
                    RpcInvalidParams,
                    RPC::expectedFieldMessage(
                        jss::credentials, "an array of CredentialID(hash256)"));
            }

            uint256 credH;
            auto const credS = jo.asString();
            if (!credH.parseHex(credS))
            {
                return RPC::makeError(
                    RpcInvalidParams,
                    RPC::expectedFieldMessage(
                        jss::credentials, "an array of CredentialID(hash256)"));
            }

            std::shared_ptr<SLE const> sleCred = ledger->read(keylet::credential(credH));
            if (!sleCred)
            {
                RPC::injectError(RpcBadCredentials, "credentials don't exist", result);
                return result;
            }

            if (!sleCred->isFlag(lsfAccepted))
            {
                RPC::injectError(RpcBadCredentials, "credentials aren't accepted", result);
                return result;
            }

            if (credentials::checkExpired(*sleCred, ledger->header().parentCloseTime))
            {
                RPC::injectError(RpcBadCredentials, "credentials are expired", result);
                return result;
            }

            if ((*sleCred)[sfSubject] != srcAcct)
            {
                RPC::injectError(
                    RpcBadCredentials, "credentials doesn't belong to the root account", result);
                return result;
            }

            auto [it, ins] = sorted.emplace((*sleCred)[sfIssuer], (*sleCred)[sfCredentialType]);
            if (!ins)
            {
                RPC::injectError(RpcBadCredentials, "duplicates in credentials", result);
                return result;
            }
            lifeExtender.push_back(std::move(sleCred));
        }
    }

    // If the two accounts are the same OR if that flag is
    // not set, then the deposit should be fine.
    bool depositAuthorized = true;
    if (reqAuth)
    {
        depositAuthorized = ledger->exists(keylet::depositPreauth(dstAcct, srcAcct)) ||
            (credentialsPresent && ledger->exists(keylet::depositPreauth(dstAcct, sorted)));
    }

    result[jss::source_account] = params[jss::source_account].asString();
    result[jss::destination_account] = params[jss::destination_account].asString();
    if (credentialsPresent)
        result[jss::credentials] = params[jss::credentials];

    result[jss::deposit_authorized] = depositAuthorized;
    return result;
}

}  // namespace xrpl
