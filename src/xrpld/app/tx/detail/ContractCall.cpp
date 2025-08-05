//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <xrpld/app/misc/ContractHostFuncImpl.h>
#include <xrpld/app/misc/ContractUtils.h>
#include <xrpld/app/misc/WasmVM.h>
#include <xrpld/app/tx/apply.h>
#include <xrpld/app/tx/detail/ContractCall.h>
#include <xrpld/ledger/View.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

XRPAmount
ContractCall::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    return Transactor::calculateBaseFee(view, tx);
}

NotTEC
ContractCall::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureSmartContract))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto const flags = ctx.tx.getFlags();
    if (flags & tfUniversalMask)
    {
        JLOG(ctx.j.error())
            << "ContractCreate: tfUniversalMask is not allowed.";
        return temINVALID_FLAG;
    }

    return preflight2(ctx);
}

TER
ContractCall::preclaim(PreclaimContext const& ctx)
{
    // The ContractAccount doesn't exist or isn't a smart contract
    // pseudo-account.
    AccountID const contractAccount = ctx.tx[sfContractAccount];
    auto const caSle = ctx.view.read(keylet::account(contractAccount));
    if (!caSle)
    {
        JLOG(ctx.j.error()) << "ContractCall: Account does not exist.";
        return tecNO_TARGET;
    }

    // The function doesn't exist on the provided contract.
    uint256 const contractID = caSle->getFieldH256(sfContractID);
    auto const contractSle = ctx.view.read(keylet::contract(contractID));
    if (!contractSle)
    {
        JLOG(ctx.j.error()) << "ContractCall: Contract does not exist.";
        return tecNO_TARGET;
    }

    if (!contractSle->at(sfContractHash))
    {
        JLOG(ctx.j.error()) << "ContractCall: Contract does not have a hash.";
        return tecNO_TARGET;
    }

    auto const contractSourceSle =
        ctx.view.read(keylet::contractSource(contractSle->at(sfContractHash)));
    if (!contractSourceSle)
    {
        JLOG(ctx.j.error()) << "ContractCall: ContractSource does not exist.";
        return tecNO_TARGET;
    }

    if (!contractSourceSle->isFieldPresent(sfFunctions))
    {
        JLOG(ctx.j.error())
            << "ContractCall: Contract does not have any functions defined.";
        return temMALFORMED;
    }

    auto const& functions = contractSourceSle->getFieldArray(sfFunctions);
    auto const functionName = ctx.tx.getFieldVL(sfFunctionName);
    std::string functionNameHexStr(functionName.begin(), functionName.end());
    auto it = std::find_if(
        functions.begin(),
        functions.end(),
        [&functionNameHexStr](STObject const& func) {
            auto const funcName = func.getFieldVL(sfFunctionName);
            std::string functionNameDefHexStr(funcName.begin(), funcName.end());
            return functionNameDefHexStr == functionNameHexStr;
        });

    if (it == functions.end())
    {
        JLOG(ctx.j.error()) << "ContractCall: Function is not defined.";
        return temMALFORMED;
    }

    // The parameters don't match the function's ABI.
    return tesSUCCESS;
}

TER
ContractCall::doApply()
{
    AccountID const contractAccount = ctx_.tx[sfContractAccount];
    auto const caSle = ctx_.view().read(keylet::account(contractAccount));
    if (!caSle)
    {
        JLOG(j_.error()) << "ContractCall: ContractAccount does not exist.";
        return tefINTERNAL;
    }

    auto const accountSle = ctx_.view().read(keylet::account(account_));
    if (!accountSle)
    {
        JLOG(j_.error()) << "ContractCall: Account does not exist.";
        return tefINTERNAL;
    }

    uint256 const contractID = caSle->getFieldH256(sfContractID);
    Keylet const k = keylet::contract(contractID);
    auto const contractSle = ctx_.view().read(k);
    if (!contractSle)
    {
        JLOG(j_.error()) << "ContractCall: Contract does not exist.";
        return tefINTERNAL;
    }

    uint256 const contractHash = contractSle->at(sfContractHash);
    auto const contractSourceSle = ctx_.view().read(
        keylet::contractSource(contractSle->at(sfContractHash)));
    if (!contractSourceSle)
    {
        JLOG(j_.error()) << "ContractCall: ContractSource does not exist.";
        return tefINTERNAL;
    }

    // WASM execution
    auto const wasmStr = contractSourceSle->getFieldVL(sfContractCode);
    std::vector<uint8_t> wasm(wasmStr.begin(), wasmStr.end());
    auto const functionName = ctx_.tx.getFieldVL(sfFunctionName);
    std::string funcName(functionName.begin(), functionName.end());

    auto const contractFunctions = contractSle->isFieldPresent(sfFunctions)
        ? contractSle->getFieldArray(sfFunctions)
        : contractSourceSle->getFieldArray(sfFunctions);
    std::optional<STObject> function;
    for (auto const& contractFunction : contractFunctions)
    {
        if (contractFunction.getFieldVL(sfFunctionName) == functionName)
            function = contractFunction;
    }
    if (!function)
    {
        JLOG(j_.error()) << "ContractCall: Function is not defined.";
        return tefINTERNAL;
    }

    // ContractCall Parameters
    std::vector<ripple::ParameterValueVec> callParameters;
    if (ctx_.tx.isFieldPresent(sfParameters))
    {
        STArray const& callParams = ctx_.tx.getFieldArray(sfParameters);
        callParameters = getParameterValueVec(callParams);
    }

    // ContractSource/Contract Default Parameters
    std::vector<ripple::ParameterValueVec> funcParameters;
    if (contractSle->isFieldPresent(sfInstanceParameters))
    {
        STArray const& funcParams =
            contractSle->getFieldArray(sfInstanceParameters);
        funcParameters = getParameterValueVec(funcParams);
    }

    // NOTE: This does not handle optional parameters. Any parameters listed in
    // the ContractSource are required in the ContractCall.
    std::vector<ParameterTypeVec> typeVec;
    if (function->isFieldPresent(sfParameters))
    {
        STArray const& callParamsDef = function->getFieldArray(sfParameters);
        typeVec = ripple::getParameterTypeVec(callParamsDef);
        if (callParameters.size() != typeVec.size())
            return tecINVALID_PARAMETERS;
    }

    for (std::size_t i = 0; i < callParameters.size(); i++)
    {
        if (callParameters[i].value.getInnerSType() !=
            typeVec[i].type.getInnerSType())
            return tecINVALID_PARAMETERS;
    }

    ripple::ContractDataMap dataMap;
    ripple::ContractEventMap eventMap;
    ContractContext contractCtx = {
        .applyCtx = ctx_,
        .callParameters = callParameters,
        .funcParameters = funcParameters,
        .expected_etxn_count = 1,
        .generation = 0,
        .burden = 0,
        .result =
            {
                .contractHash = contractHash,
                .contractKeylet = k,
                .contractSourceKeylet = k,
                .contractAccountKeylet = k,
                .contractAccount = contractAccount,
                .nextSequence = caSle->getFieldU32(sfSequence),
                .otxnAccount = account_,
                .exitType = ripple::ExitType::ROLLBACK,
                .exitCode = -1,
                .dataMap = dataMap,
                .eventMap = eventMap,
                .changedDataCount = 0,
            },
    };

    ContractHostFunctionsImpl ledgerDataProvider(contractCtx);

    if (!ctx_.tx.isFieldPresent(sfComputationAllowance))
    {
        JLOG(j_.error()) << "ContractCall: Computation allowance is not set.";
        return tefINTERNAL;
    }
    std::uint32_t allowance = ctx_.tx[sfComputationAllowance];
    auto re = runEscrowWasm(wasm, funcName, {}, &ledgerDataProvider, allowance);

    // Create MetaData
    ApplyViewImpl& avi =
        dynamic_cast<ApplyViewImpl&>(contractCtx.applyCtx.view());
    STObject meta{sfContractExecution};
    meta.setAccountID(sfContractAccount, contractAccount);
    meta.setFieldH256(sfContractHash, contractHash);

    // Wasm Result
    if (re.has_value())
    {
        meta.setFieldU32(sfGasUsed, static_cast<uint32_t>(re.value().cost));
        auto ret = re.value().result;
        if (ret < 0)
        {
            JLOG(j_.error()) << "Contract Failure: " << ret;
            uint64_t exit_code = 0x8000000000000000ULL + (-1 * ret);
            meta.setFieldU64(sfContractReturnCode, exit_code);
            meta.setFieldU8(sfContractResult, ripple::ExitType::ROLLBACK);
            avi.addContractMetaData(std::move(meta));
            return tecWASM_REJECTED;
        }

        if (auto res = contract::finalizeContractData(
                contractCtx.applyCtx,
                contractAccount,
                contractCtx.result.dataMap,
                contractCtx.result.eventMap,
                ctx_.tx.getTransactionID());
            !isTesSuccess(res))
        {
            JLOG(j_.error())
                << "Contract data finalization failed: " << transHuman(res);
            return res;
        }

        meta.setFieldU64(sfContractReturnCode, 0);
        meta.setFieldU8(sfContractResult, ripple::ExitType::ACCEPT);
        avi.addContractMetaData(std::move(meta));
        return tesSUCCESS;
    }
    else
    {
        JLOG(j_.error()) << "WASM Failure: " + transHuman(re.error());
        auto const errorCode = TERtoInt(re.error());
        uint64_t unsigned_exit_code =
            (errorCode >= 0 ? errorCode
                            : 0x8000000000000000ULL + (-1 * errorCode));
        meta.setFieldU64(sfContractReturnCode, unsigned_exit_code);
        meta.setFieldU8(sfContractResult, ripple::ExitType::ROLLBACK);
        avi.addContractMetaData(std::move(meta));
        return re.error();
    }
    return tesSUCCESS;
}

}  // namespace ripple
