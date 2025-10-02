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

#include <xrpld/app/misc/ContractUtils.h>
#include <xrpld/app/tx/apply.h>
#include <xrpld/app/tx/detail/ContractCall.h>
#include <xrpld/app/wasm/ContractHostFuncImpl.h>
#include <xrpld/app/wasm/WasmVM.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/detail/ApplyViewBase.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

XRPAmount
ContractCall::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    XRPAmount extraFee{0};
    if (auto const allowance = tx[~sfComputationAllowance]; allowance)
    {
        extraFee += (*allowance) * view.fees().gasPrice / MICRO_DROPS_PER_DROP;
    }
    return Transactor::calculateBaseFee(view, tx) + extraFee;
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
        JLOG(ctx.j.error())
            << "ContractCall: FunctionName: " << functionNameHexStr
            << " does not exist in contract.";
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

    // // Handle the flags for the contract call.
    if (ctx_.tx.isFieldPresent(sfParameters))
    {
        STArray const& params = ctx_.tx.getFieldArray(sfParameters);
        if (auto ter = contract::handleFlagParameters(
                ctx_.view(),
                ctx_.tx,
                account_,
                contractAccount,
                params,
                mPriorBalance,
                ctx_.journal);
            !isTesSuccess(ter))
        {
            JLOG(ctx_.journal.error())
                << "ContractCall: Failed to handle flag parameters.";
            return ter;
        }
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
        JLOG(j_.error())
            << "ContractCall: FunctionName does not exist in contract.";
        return tefINTERNAL;
    }

    // ContractCall Parameters
    std::vector<ripple::ParameterValueVec> functionParameters;
    if (ctx_.tx.isFieldPresent(sfParameters))
    {
        STArray const& funcParams = ctx_.tx.getFieldArray(sfParameters);
        functionParameters = getParameterValueVec(funcParams);
    }

    // ContractSource/Contract Default Parameters
    std::vector<ripple::ParameterValueVec> instanceParameters;
    if (contractSle->isFieldPresent(sfInstanceParameterValues))
    {
        STArray const& instParams =
            contractSle->getFieldArray(sfInstanceParameterValues);
        instanceParameters = getParameterValueVec(instParams);
    }

    // The parameters don't match the function's ABI.
    std::vector<ParameterTypeVec> typeVec;
    if (function->isFieldPresent(sfParameters))
    {
        STArray const& funcParamsDef = function->getFieldArray(sfParameters);
        typeVec = ripple::getParameterTypeVec(funcParamsDef);
        if (functionParameters.size() != typeVec.size())
            return tecINVALID_PARAMETERS;
    }

    for (std::size_t i = 0; i < functionParameters.size(); i++)
    {
        if (functionParameters[i].value.getInnerSType() !=
            typeVec[i].type.getInnerSType())
            return tecINVALID_PARAMETERS;
    }

    ripple::ContractDataMap dataMap;
    ripple::ContractEventMap eventMap;
    ContractContext contractCtx = {
        .applyCtx = ctx_,
        .instanceParameters = instanceParameters,
        .functionParameters = functionParameters,
        .built_txns = {},
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

    // Wasm Result
    if (re.has_value())
    {
        // TODO: better error handling for this conversion
        ctx_.setGasUsed(static_cast<uint32_t>(re.value().cost));
        auto ret = re.value().result;
        if (ret < 0)
        {
            JLOG(j_.error()) << "Contract Failure: " << ret;
            ctx_.setWasmReturnCode(ret);
            return tecWASM_REJECTED;
        }

        if (auto res = contract::finalizeContractData(
                ctx_,
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

        ctx_.setWasmReturnCode(ret);
        ctx_.setEmittedTxns(contractCtx.result.emittedTxns);
        return tesSUCCESS;
    }
    else
    {
        JLOG(j_.error()) << "WASM Failure: " + transHuman(re.error());
        auto const errorCode = TERtoInt(re.error());
        ctx_.setWasmReturnCode(errorCode);
        return re.error();
    }
    return tesSUCCESS;
}

}  // namespace ripple
