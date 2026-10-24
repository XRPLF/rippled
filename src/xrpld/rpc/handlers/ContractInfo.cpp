#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>  // IWYU pragma: keep
#include <xrpl/protocol/STDataType.h>
#include <xrpl/protocol/jss.h>

#include <memory>
#include <string>
#include <utility>

namespace xrpl {

// {
//   contract_account: <account>,
//   account : <account> // optional, whose data under the contract to return
//   ledger_index : <ledger_index>
// }

json::Value
doContractInfo(rpc::JsonContext& context)
{
    auto& params = context.params;

    std::string contractAccount;
    if (params.isMember(jss::contract_account))
    {
        if (!params[jss::contract_account].isString())
            return rpc::invalidFieldError(jss::contract_account);
        contractAccount = params[jss::contract_account].asString();
    }
    else
    {
        {
            return rpc::missingFieldError(jss::contract_account);
        }
    }

    std::string account;
    if (params.isMember(jss::account))
    {
        if (!params[jss::account].isString())
            return rpc::invalidFieldError(jss::account);
        account = params[jss::account].asString();
    }

    std::shared_ptr<ReadView const> ledger;
    auto result = rpc::lookupLedger(ledger, context);

    if (!ledger)
        return result;

    // contract account
    auto caid = parseBase58<AccountID>(contractAccount);
    if (!caid)
    {
        rpc::injectError(RpcActMalformed, result);
        return result;
    }
    auto const caID{caid.value()};
    auto const caSle = ledger->read(keylet::account(caID));
    if (!caSle)
    {
        result[jss::contract_account] = toBase58(caID);
        rpc::injectError(RpcActNotFound, result);
        return result;
    }

    // An account that is not a contract's pseudo-account has no contract to describe.
    auto const contractSle = caSle->isFieldPresent(sfContractID)
        ? ledger->read(keylet::contract(caSle->getFieldH256(sfContractID)))
        : nullptr;
    if (!contractSle)
    {
        result[jss::contract_account] = toBase58(caID);
        rpc::injectError(RpcObjectNotFound, result);
        return result;
    }

    // Every Contract carries the hash of a ContractSource it holds a reference to.
    auto const sourceSle = ledger->read(keylet::contractSource(contractSle->at(sfContractHash)));
    if (!sourceSle)
    {
        // LCOV_EXCL_START
        result[jss::contract_account] = toBase58(caID);
        rpc::injectError(RpcObjectNotFound, result);
        return result;
        // LCOV_EXCL_STOP
    }

    result[jss::contract_account] = toBase58(caID);
    result[jss::code] = strHex(sourceSle->at(sfContractCode));
    result[jss::hash] = to_string(sourceSle->at(sfContractHash));

    // Each function of the ABI: its name, and each parameter's flag and type.
    for (auto const& function : sourceSle->getFieldArray(sfFunctions))
    {
        json::Value jvFunction(json::ValueType::Object);
        jvFunction[jss::name] = strHex(function.getFieldVL(sfFunctionName));
        json::Value jvParams(json::ValueType::Array);
        for (auto const& param : function.getFieldArray(sfParameters))
        {
            json::Value jvParam(json::ValueType::Object);
            jvParam[jss::flags] = param.getFieldU32(sfParameterFlag);
            jvParam[jss::type] = param.getFieldDataType(sfParameterType).getInnerTypeString();
            jvParams.append(jvParam);
        }
        jvFunction[jss::params] = std::move(jvParams);
        result[jss::functions].append(std::move(jvFunction));
    }
    if (contractSle->isFieldPresent(sfURI))
        result[jss::source_code_uri] = strHex(contractSle->at(sfURI));

    json::Value jvAccepted(json::ValueType::Object);
    rpc::injectSLE(jvAccepted, *caSle);
    result[jss::account_data] = jvAccepted;

    auto const dataSle = ledger->read(keylet::contractData(caID, caID));
    if (dataSle)
    {
        result[jss::contract_data] =
            dataSle->getFieldJson(sfContractJson).getJson(JsonOptions::Values::None);
    }

    if (!account.empty())
    {
        auto id = parseBase58<AccountID>(account);
        if (!id)
        {
            rpc::injectError(RpcActMalformed, result);
            return result;
        }
        auto const accountID = id.value();
        if (ledger->exists(keylet::account(accountID)))
        {
            if (auto const dataSle = ledger->read(keylet::contractData(accountID, caID)))
            {
                result[jss::user_data] =
                    dataSle->getFieldJson(sfContractJson).getJson(JsonOptions::Values::None);
            }
        }
    }

    return result;
}

}  // namespace xrpl
