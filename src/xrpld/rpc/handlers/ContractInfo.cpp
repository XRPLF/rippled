#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/GRPCHandlers.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STDataType.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

namespace xrpl {

// {
//   contract_account: <account>,
//   function : <string> // optional
//   user_account : <account>
//   ledger_index : <ledger_index>
// }

json::Value
doContractInfo(RPC::JsonContext& context)
{
    auto& params = context.params;

    std::string contractAccount;
    if (params.isMember(jss::contract_account))
    {
        if (!params[jss::contract_account].isString())
            return RPC::invalidFieldError(jss::contract_account);
        contractAccount = params[jss::contract_account].asString();
    }
    else
        return RPC::missingFieldError(jss::contract_account);

    std::string functionName;
    if (params.isMember(jss::function))
    {
        if (!params[jss::function].isString())
            return RPC::invalidFieldError(jss::function);
        functionName = params[jss::function].asString();
    }

    std::string account;
    if (params.isMember(jss::account))
    {
        if (!params[jss::account].isString())
            return RPC::invalidFieldError(jss::account);
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
        RPC::injectError(RpcActMalformed, result);
        return result;
    }
    auto const caID{caid.value()};
    auto const caSle = ledger->read(keylet::account(caID));
    if (!caSle)
    {
        result[jss::contract_account] = toBase58(caID);
        RPC::injectError(RpcActNotFound, result);
    }

    uint256 const contractID = caSle->getFieldH256(sfContractID);
    auto const contractSle = ledger->read(keylet::contract(contractID));
    if (!contractSle)
    {
        result[jss::contract_account] = toBase58(caID);
        RPC::injectError(RpcObjectNotFound, result);
    }

    // contract source
    if (!contractSle->at(sfContractHash))
    {
        result[jss::contract_account] = toBase58(caID);
        RPC::injectError(RpcUnknown, result);
    }

    auto const sourceSle = ledger->read(keylet::contractSource(contractSle->at(sfContractHash)));
    if (!sourceSle)
    {
        result[jss::contract_account] = toBase58(caID);
        RPC::injectError(RpcObjectNotFound, result);
    }

    result[jss::contract_account] = toBase58(caID);
    result[jss::code] = strHex(sourceSle->at(sfContractCode));
    result[jss::hash] = to_string(sourceSle->at(sfContractHash));

    // lambda to format the functions response:
    // name: <string>
    // params: [<flag>: <string>, <type>: <string>, <name>: <string>]
    auto formatFunctions = [](json::Value& jv, std::shared_ptr<SLE const> const& slePtr) {
        if (slePtr && slePtr->isFieldPresent(sfFunctions))
        {
            auto const& functions = slePtr->getFieldArray(sfFunctions);
            for (auto const& function : functions)
            {
                json::Value jvFunction(json::ValueType::Object);
                jvFunction[jss::name] = strHex(function.getFieldVL(sfFunctionName));
                json::Value jvParams(json::ValueType::Array);
                for (auto const& param : function.getFieldArray(sfParameters))
                {
                    json::Value jvParam(json::ValueType::Object);
                    jvParam[jss::flags] = param.getFieldU32(sfParameterFlag);
                    jvParam[jss::type] =
                        param.getFieldDataType(sfParameterType).getInnerTypeString();
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

    json::Value jvAccepted(json::ValueType::Object);
    RPC::injectSLE(jvAccepted, *caSle);
    result[jss::account_data] = jvAccepted;

    auto const dataSle = ledger->read(keylet::contractData(caID, caID));
    if (dataSle)
        result[jss::contract_data] =
            dataSle->getFieldJson(sfContractJson).getJson(JsonOptions::Values::None);

    if (!account.empty())
    {
        auto id = parseBase58<AccountID>(account);
        if (!id)
        {
            RPC::injectError(RpcActMalformed, result);
            return result;
        }
        auto const accountID = id.value();
        if (ledger->exists(keylet::account(accountID)))
        {
            if (auto dataSle = ledger->read(keylet::contractData(accountID, caID)))
                result[jss::user_data] =
                    dataSle->getFieldJson(sfContractJson).getJson(JsonOptions::Values::None);
        }
    }

    return result;
}

}  // namespace xrpl
