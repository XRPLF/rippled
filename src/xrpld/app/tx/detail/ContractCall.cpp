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

#include <xrpld/app/misc/WasmHostFuncImpl.h>
#include <xrpld/app/misc/WasmVM.h>
#include <xrpld/app/tx/apply.h>
#include <xrpld/app/tx/detail/ContractCall.h>
#include <xrpld/ledger/Sandbox.h>
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
    auto const accountSle = ctx.view.read(keylet::account(contractAccount));
    if (!accountSle)
    {
        JLOG(ctx.j.error()) << "ContractCall: Account does not exist.";
        return tecNO_TARGET;
    }

    // The function doesn't exist on the provided contract.
    auto const contractSle = ctx.view.read(keylet::contract(contractAccount));
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
    Keylet const k = keylet::contract(contractAccount);

    auto const contractSle = ctx_.view().read(k);
    if (!contractSle)
    {
        JLOG(j_.error()) << "ContractCall: Contract does not exist.";
        return tefINTERNAL;
    }

    if (!contractSle->at(sfContractHash))
    {
        JLOG(j_.error()) << "ContractCall: Contract does not have a hash.";
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
        return tecINTERNAL;

    STArray const& callParams = ctx_.tx.getFieldArray(sfCallParameters);
    STArray const& funcParams = function->getFieldArray(sfFunctionParameters);
    STArray const& funcParamsDef = function->getFieldArray(sfFunctionParameters);

    std::cout << "Function name: " << funcName << std::endl;
    std::cout << "Call parameters: " << callParams.size() << std::endl;
    std::cout << "Function parameters: " << funcParams.size() << std::endl;
    std::cout << "Function parameters (def): " << funcParamsDef.size() << std::endl;


    std::vector<ripple::ParameterValueVec> funcParameters;
    funcParameters = getParameterValueVec(funcParams);
    std::cout << "Function parameters (values): " << funcParameters.size() << std::endl;
    auto typeVec = ripple::getParameterTypeVec(funcParamsDef);
    std::cout << "Function parameters (typeVec): " << typeVec.size() << std::endl;
    // if (funcParameters.size() != typeVec.size())
    //     return tecINTERNAL;

    for (std::size_t i = 0; i < funcParameters.size(); i++)
    {
        if (funcParameters[i].value.getInnerSType() !=
            typeVec[i].type.getInnerSType())
            return tecINTERNAL;
    }

    std::vector<ripple::ParameterValueVec> callParameters = getParameterValueVec(callParams);

    ContractContext contractCtx = {
        .applyCtx = ctx_,
        .callParameters = callParameters,
        .funcParameters = funcParameters,
        .expected_etxn_count = 1,
        .generation = 0,
        .burden = 0,
        .result = {
            .contractHash = contractHash,
            .contractKeylet = k,
            .contractSourceKeylet = k,
            .contractAccountKeylet = k,
            .contractAccount = contractAccount,
            .otxnAccount = contractAccount,
        },
    };

    WasmHostFunctionsImpl ledgerDataProvider(contractCtx);

    if (!ctx_.tx.isFieldPresent(sfComputationAllowance))
    {
        JLOG(j_.error()) << "ContractCall: Computation allowance is not set.";
        return tefINTERNAL;
    }
    std::uint32_t allowance = ctx_.tx[sfComputationAllowance];
    auto re = runEscrowWasm(wasm, funcName, {}, &ledgerDataProvider, allowance);
    JLOG(j_.error()) << "Call WASM ran";
    if (re.has_value())
    {
        auto reValue = re.value().result;
        // TODO: better error handling for this conversion
        ctx_.setGasUsed(static_cast<uint32_t>(re.value().cost));
        JLOG(j_.error()) << "WASM Success: " + std::to_string(reValue)
                         << ", cost: " << re.value().cost;
        if (!reValue)
        {
            // ctx_.view().update(slep);
            return tecWASM_REJECTED;
        }
    }
    else
    {
        JLOG(j_.error()) << "WASM Failure: " + transHuman(re.error());
        return re.error();
    }
    return tesSUCCESS;
}

}  // namespace ripple
