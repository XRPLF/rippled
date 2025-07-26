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
    {
        JLOG(j_.error()) << "ContractCall: Function is not defined.";
        return tefINTERNAL;
    }

    // ContractCall Parameters
    STArray const& callParams = ctx_.tx.getFieldArray(sfCallParameters);
    std::vector<ripple::ParameterValueVec> callParameters =
        getParameterValueVec(callParams);
    // ContractSource/Contract Default Parameters
    STArray const& funcParams = function->getFieldArray(sfFunctionParameters);
    std::vector<ripple::ParameterValueVec> funcParameters =
        getParameterValueVec(funcParams);

    // NOTE: This does not handle optional parameters. Any parameters listed in
    // the ContractSource are required in the ContractCall.
    STArray const& callParamsDef = function->getFieldArray(sfCallParameters);
    auto const typeVec = ripple::getParameterTypeVec(callParamsDef);
    if (callParameters.size() != typeVec.size())
        return tecINTERNAL;

    for (std::size_t i = 0; i < callParameters.size(); i++)
    {
        if (callParameters[i].value.getInnerSType() !=
            typeVec[i].type.getInnerSType())
            return tecINTERNAL;
    }

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
                .otxnAccount = contractAccount,
                .exitType = ripple::ExitType::ROLLBACK,
                .exitReason = std::string(""),
                .exitCode = -1,
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
    ContractResult const& contractResult = ledgerDataProvider.getResult();
    JLOG(j_.error()) << "Call WASM ran: " << re.has_value() << ", exitType: "
                     << static_cast<int>(contractResult.exitType)
                     << ", exitReason: " << contractResult.exitReason
                     << ", exitCode: " << contractResult.exitCode;
    if (re.has_value())
    {
        auto reValue = re.value().result;
        // TODO: better error handling for this conversion
        JLOG(j_.error()) << "WASM Success: " + std::to_string(reValue)
                         << ", cost: " << re.value().cost;

        ApplyViewImpl& avi =
            dynamic_cast<ApplyViewImpl&>(contractCtx.applyCtx.view());
        STObject meta{sfContractExecution};
        meta.setFieldU8(sfContractResult, contractResult.exitType);
        meta.setAccountID(sfContractAccount, contractResult.contractAccount);
        uint64_t unsigned_exit_code =
            (contractResult.exitCode >= 0
                 ? contractResult.exitCode
                 : 0x8000000000000000ULL + (-1 * contractResult.exitCode));
        meta.setFieldU64(sfContractReturnCode, unsigned_exit_code);
        meta.setFieldVL(
            sfContractReturnString,
            ripple::Slice{
                contractResult.exitReason.data(),
                contractResult.exitReason.size()});
        meta.setFieldH256(sfContractHash, contractResult.contractHash);
        meta.setFieldU32(sfGasUsed, static_cast<uint32_t>(re.value().cost));
        avi.addContractMetaData(std::move(meta));

        if (!reValue)
        {
            // ctx_.view().update(slep);
            return tecWASM_REJECTED;
        }

        // Submit Transaction to ledger
        auto& app = contractCtx.applyCtx.app;
        auto& j = contractCtx.applyCtx.journal;
        OpenView wholeBatchView(batch_view, contractCtx.applyCtx.openView());
        auto const parentBatchId = ctx_.tx.getTransactionID();
        auto applyOneTransaction = [&app, &j, &parentBatchId, &wholeBatchView](std::shared_ptr<const STTx> const& tx) {
                OpenView perTxBatchView(batch_view, wholeBatchView);

                auto const ret = ripple::apply(app, perTxBatchView, parentBatchId, *tx, tapBATCH, j);
                JLOG(j.error()) << "WASM [" << parentBatchId
                                << "]: " << tx->getTransactionID() << " "
                                << (ret.applied ? "applied" : "failure") <<
                                ": "
                                << transToken(ret.ter);

                if (ret.applied && (isTesSuccess(ret.ter) || isTecClaim(ret.ter)))
                    perTxBatchView.apply(wholeBatchView);

                return ret;
            };

        int applied = 0;

        auto txns = contractCtx.result.pendingTxns;
        while (!txns.empty())
        {
            auto rb = txns.front();
            txns.pop();
            std::cout << "WASM: Submitting transaction: "
                      << rb->getID() << std::endl;
            auto const result = applyOneTransaction(rb->getSTransaction());
            if (result.applied)
                ++applied;

            if (!isTesSuccess(result.ter))
                applied = 0;
        }

        if (applied != 0)
            wholeBatchView.apply(contractCtx.applyCtx.openView());
    }
    else
    {
        JLOG(j_.error()) << "WASM Failure: " + transHuman(re.error());
        return re.error();
    }
    return tesSUCCESS;
}

}  // namespace ripple
