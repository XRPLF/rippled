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

#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/GRPCHandlers.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STDataType.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

// {
//   contract_account: <account>,
//   function : <string> // optional
//   user_account : <account>
//   ledger_index : <ledger_index>
// }

Json::Value
doContractInfo(RPC::JsonContext& context)
{
    auto& params = context.params;

    std::string contractAccount;
    if (params.isMember(jss::contract_account))
    {
        if (!params[jss::contract_account].isString())
            return RPC::invalid_field_error(jss::contract_account);
        contractAccount = params[jss::contract_account].asString();
    }
    else
        return RPC::missing_field_error(jss::contract_account);

    std::string functionName;
    if (params.isMember(jss::function))
    {
        if (!params[jss::function].isString())
            return RPC::invalid_field_error(jss::function);
        functionName = params[jss::function].asString();
    }

    std::string account;
    if (params.isMember(jss::account))
    {
        if (!params[jss::account].isString())
            return RPC::invalid_field_error(jss::account);
        account = params[jss::account].asString();
    }

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);

    if (!ledger)
        return result;

    // contract account
    auto caid = parseBase58<AccountID>(contractAccount);
    if (!caid)
    {
        RPC::inject_error(rpcACT_MALFORMED, result);
        return result;
    }
    auto const caID{std::move(caid.value())};
    auto const caSle = ledger->read(keylet::account(caID));
    if (!caSle)
    {
        result[jss::contract_account] = toBase58(caID);
        RPC::inject_error(rpcACT_NOT_FOUND, result);
    }

    uint256 const contractID = caSle->getFieldH256(sfContractID);
    auto const contractSle = ledger->read(keylet::contract(contractID));
    if (!contractSle)
    {
        result[jss::contract_account] = toBase58(caID);
        RPC::inject_error(rpcOBJECT_NOT_FOUND, result);
    }

    // contract source
    if (!contractSle->at(sfContractHash))
    {
        result[jss::contract_account] = toBase58(caID);
        RPC::inject_error(rpcUNKNOWN, result);
    }

    auto const sourceSle =
        ledger->read(keylet::contractSource(contractSle->at(sfContractHash)));
    if (!sourceSle)
    {
        result[jss::contract_account] = toBase58(caID);
        RPC::inject_error(rpcOBJECT_NOT_FOUND, result);
    }

    result[jss::contract_account] = toBase58(caID);
    result[jss::code] = strHex(sourceSle->at(sfContractCode));
    result[jss::hash] = to_string(sourceSle->at(sfContractHash));

    // lambda to format the functions response:
    // name: <string>
    // params: [<flag>: <string>, <type>: <string>, <name>: <string>]
    auto formatFunctions = [](Json::Value& jv,
                              std::shared_ptr<SLE const> const& slePtr) {
        if (slePtr && slePtr->isFieldPresent(sfFunctions))
        {
            auto const& functions = slePtr->getFieldArray(sfFunctions);
            for (auto const& function : functions)
            {
                Json::Value jvFunction(Json::objectValue);
                jvFunction[jss::name] =
                    strHex(function.getFieldVL(sfFunctionName));
                Json::Value jvParams(Json::arrayValue);
                for (auto const& param : function.getFieldArray(sfParameters))
                {
                    Json::Value jvParam(Json::objectValue);
                    jvParam[jss::flags] = param.getFieldU32(sfParameterFlag);
                    jvParam[jss::type] = param.getFieldDataType(sfParameterType)
                                             .getInnerTypeString();
                    jvParam[jss::name] =
                        strHex(param.getFieldVL(sfParameterName));
                    jvParams.append(jvParam);
                }
                jvFunction[jss::params] = std::move(jvParams);
                jv.append(std::move(jvFunction));
            }
        }
    };
    if (sourceSle->isFieldPresent(sfFunctions))
        formatFunctions(result[jss::functions], sourceSle);
    if (contractSle->isFieldPresent(sfURI))
        result[jss::source_code_uri] = strHex(contractSle->at(sfURI));

    Json::Value jvAccepted(Json::objectValue);
    RPC::injectSLE(jvAccepted, *caSle);
    result[jss::account_data] = jvAccepted;

    auto const dataSle = ledger->read(keylet::contractData(caID, caID));
    if (dataSle)
        result[jss::contract_data] =
            dataSle->getFieldJson(sfContractJson).getJson(JsonOptions::none);

    if (!account.empty())
    {
        auto id = parseBase58<AccountID>(account);
        if (!id)
        {
            RPC::inject_error(rpcACT_MALFORMED, result);
            return result;
        }
        auto const accountID = id.value();
        if (ledger->exists(keylet::account(accountID)))
        {
            if (auto dataSle =
                    ledger->read(keylet::contractData(accountID, caID)))
                result[jss::user_data] = dataSle->getFieldJson(sfContractJson)
                                             .getJson(JsonOptions::none);
        }
    }

    return result;
}

}  // namespace ripple
