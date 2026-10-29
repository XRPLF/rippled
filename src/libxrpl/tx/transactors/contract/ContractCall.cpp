#include <xrpl/tx/transactors/contract/ContractCall.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/detail/ApplyViewBase.h>
#include <xrpl/ledger/helpers/ContractUtils.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/wasm/ContractContext.h>
#include <xrpl/tx/wasm/ContractHostFuncImpl.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xrpl {

XRPAmount
ContractCall::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // The gas cost in drops, rounded up; widened first, as both factors are 32-bit. Gas is a
    // required field.
    std::uint64_t const allowanceFee =
        ((std::uint64_t{tx[sfGas]} * view.fees().gasPrice) + microDropsPerDrop - 1) /
        microDropsPerDrop;
    return Transactor::calculateBaseFee(view, tx) +
        XRPAmount{static_cast<std::int64_t>(allowanceFee)};
}

NotTEC
ContractCall::preflight(PreflightContext const& ctx)
{
    // Protocol ceiling only; the node's configured gas limit is checked in preclaim.
    if (auto const allowance = ctx.tx[sfGas]; allowance == 0 || allowance > kMaxGasLimit)
    {
        JLOG(ctx.j.trace()) << "ContractCall: Gas out of range: " << allowance;
        return temBAD_LIMIT;
    }

    // A parameter's flag must be a parameter flag, and the value it moves must be one the
    // caller can send.
    if (ctx.tx.isFieldPresent(sfParameters))
    {
        auto const& params = ctx.tx.getFieldArray(sfParameters);
        for (auto const& param : params)
        {
            if (param.isFieldPresent(sfParameterFlag) &&
                !contract::isValidParameterFlag(param.getFieldU32(sfParameterFlag)))
                return temINVALID_FLAG;
        }
        if (auto const ret = contract::preflightFlagParameters(params, ctx.j); !isTesSuccess(ret))
            return ret;
    }

    return tesSUCCESS;
}

TER
ContractCall::preclaim(PreclaimContext const& ctx)
{
    // The node's configured limits, read as EscrowFinish does; a zero gasLimit
    // deactivates the WASM runtime.
    auto const fees = ctx.registry.get().getFees();
    if (fees.gasLimit == 0)
    {
        JLOG(ctx.j.trace()) << "ContractCall: WASM runtime deactivated by the node configuration.";
        return temTEMP_DISABLED;
    }

    if (ctx.tx[sfGas] > fees.gasLimit)
    {
        JLOG(ctx.j.trace()) << "ContractCall: Gas over the configured limit: " << ctx.tx[sfGas];
        return temBAD_LIMIT;
    }

    AccountID const account = ctx.tx[sfAccount];

    // The ContractAccount doesn't exist or isn't a smart contract
    // pseudo-account.
    AccountID const contractAccount = ctx.tx[sfContractAccount];
    auto const caSle = ctx.view.read(keylet::account(contractAccount));
    if (!caSle)
    {
        JLOG(ctx.j.trace()) << "ContractCall: Contract Account does not exist.";
        return tecNO_TARGET;
    }

    // An account that is not a contract's pseudo-account names no contract.
    auto const contractSle = caSle->isFieldPresent(sfContractID)
        ? ctx.view.read(keylet::contract(caSle->getFieldH256(sfContractID)))
        : nullptr;
    if (!contractSle)
    {
        JLOG(ctx.j.trace()) << "ContractCall: Contract does not exist.";
        return tecNO_TARGET;
    }

    // Every Contract holds a reference to its ContractSource.
    auto const contractSourceSle =
        ctx.view.read(keylet::contractSource(contractSle->at(sfContractHash)));
    if (!contractSourceSle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const& functions = contractSourceSle->getFieldArray(sfFunctions);
    auto const functionName = ctx.tx.getFieldVL(sfFunctionName);
    std::string functionNameHexStr(functionName.begin(), functionName.end());
    auto const it = std::ranges::find_if(functions, [&functionNameHexStr](STObject const& func) {
        auto const funcName = func.getFieldVL(sfFunctionName);
        std::string const functionNameDefHexStr(funcName.begin(), funcName.end());
        return functionNameDefHexStr == functionNameHexStr;
    });

    if (it == functions.end())
    {
        JLOG(ctx.j.trace()) << "ContractCall: FunctionName: " << functionNameHexStr
                            << " does not exist in contract abi.";
        return tecNO_ENTRY;
    }

    if (ctx.tx.isFieldPresent(sfParameters))
    {
        STArray const& params = ctx.tx.getFieldArray(sfParameters);

        // Each parameter carries the flag the ABI gives it, so a send the contract expects
        // happens and one it does not expect does not.
        if (it->isFieldPresent(sfParameters))
        {
            auto const flagOf = [](STObject const& param) {
                return param.isFieldPresent(sfParameterFlag) ? param.getFieldU32(sfParameterFlag)
                                                             : 0u;
            };
            STArray const& abi = it->getFieldArray(sfParameters);
            for (std::size_t i = 0; i < std::min(abi.size(), params.size()); ++i)
            {
                if (flagOf(params[i]) != flagOf(abi[i]))
                    return tecINVALID_PARAMETERS;
            }
        }

        if (auto ter = contract::preclaimFlagParameters(
                ctx.view, account, contractAccount, params, ctx.tx[sfFee].xrp(), ctx.j);
            !isTesSuccess(ter))
        {
            JLOG(ctx.j.trace()) << "ContractCall: Failed to preclaim flag parameters.";
            return ter;
        }
    }

    return tesSUCCESS;
}

TER
ContractCall::doApply()
{
    AccountID const contractAccount = ctx_.tx[sfContractAccount];

    // Preclaim found the contract account, its Contract and its ContractSource.
    auto const caSle = ctx_.view().read(keylet::account(contractAccount));
    if (!caSle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    Keylet const k = keylet::contract(caSle->getFieldH256(sfContractID));
    auto const contractSle = ctx_.view().read(k);
    if (!contractSle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    uint256 const contractHash = contractSle->at(sfContractHash);
    auto const contractSourceSle = ctx_.view().read(keylet::contractSource(contractHash));
    if (!contractSourceSle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // Move what the parameters' flags send to the contract.
    if (ctx_.tx.isFieldPresent(sfParameters))
    {
        STArray const& params = ctx_.tx.getFieldArray(sfParameters);
        if (auto ter = contract::doApplyFlagParameters(
                ctx_.view(),
                ctx_.tx,
                accountID_,
                contractAccount,
                params,
                preFeeBalance_,
                ctx_.journal);
            !isTesSuccess(ter))
        {
            JLOG(ctx_.journal.trace()) << "ContractCall: Failed to handle flag parameters.";
            return ter;
        }
    }

    // WASM execution
    auto const wasmStr = contractSourceSle->getFieldVL(sfContractCode);
    std::vector<uint8_t> const wasm(wasmStr.begin(), wasmStr.end());
    auto const functionName = ctx_.tx.getFieldVL(sfFunctionName);
    std::string const funcName(functionName.begin(), functionName.end());

    std::optional<STObject> function;
    for (auto const& contractFunction : contractSourceSle->getFieldArray(sfFunctions))
    {
        if (contractFunction.getFieldVL(sfFunctionName) == functionName)
            function = contractFunction;
    }
    // Preclaim found the function in the ABI.
    if (!function)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // ContractCall Parameters
    std::vector<xrpl::ParameterValueVec> functionParameters;
    if (ctx_.tx.isFieldPresent(sfParameters))
    {
        STArray const& funcParams = ctx_.tx.getFieldArray(sfParameters);
        functionParameters = getParameterValueVec(funcParams);
    }

    // ContractSource/Contract Default Parameters
    std::vector<xrpl::ParameterValueVec> instanceParameters;
    if (contractSle->isFieldPresent(sfInstanceParameterValues))
    {
        STArray const& instParams = contractSle->getFieldArray(sfInstanceParameterValues);
        instanceParameters = getParameterValueVec(instParams);
    }

    // The parameters match the function's ABI in number and in type; a function may declare
    // none.
    std::vector<ParameterTypeVec> const typeVec = function->isFieldPresent(sfParameters)
        ? xrpl::getParameterTypeVec(function->getFieldArray(sfParameters))
        : std::vector<ParameterTypeVec>{};
    if (functionParameters.size() != typeVec.size())
        return tecINVALID_PARAMETERS;

    for (std::size_t i = 0; i < functionParameters.size(); i++)
    {
        if (functionParameters[i].value.getInnerSType() != typeVec[i].type.getInnerSType())
            return tecINVALID_PARAMETERS;
    }

    xrpl::ContractDataMap const dataMap;
    ContractContext contractCtx = {
        .applyCtx = ctx_,
        .instanceParameters = instanceParameters,
        .functionParameters = functionParameters,
        .builtTxns = {},
        .expectedEtxnCount = 1,
        .nonceUsed = {},
        .generation = 0,
        .burden = 0,
        .result =
            {
                .contractHash = contractHash,
                .contractKeylet = k,
                .contractSourceKeylet = keylet::contractSource(contractHash),
                .contractAccountKeylet = keylet::account(contractAccount),
                .contractAccount = contractAccount,
                .nextSequence = caSle->getFieldU32(sfSequence),
                .otxnAccount = accountID_,
                .otxnId = ctx_.tx.getTransactionID(),
                .exitReason = "",
                .exitCode = -1,
                .dataMap = dataMap,
                .events = {},
                .emittedTxns = {},
                .changedDataCount = 0,
            },
        .emitView = std::nullopt,
    };

    ContractHostFunctionsImpl ledgerDataProvider(contractCtx);

    std::uint32_t const allowance = ctx_.tx[sfGas];
    auto const re = runEscrowWasm(wasm, ledgerDataProvider, allowance, funcName);

    // Charge for what the contract burned whether or not it completed. A
    // cost outside the allowance is an engine fault.
    std::optional<std::int64_t> const cost = re.has_value() ? re->cost : re.error().cost;
    if (cost.has_value())
    {
        if (*cost < 0 || *cost > allowance)
            return tecINTERNAL;
        ctx_.setGasUsed(static_cast<std::uint32_t>(*cost));
    }

    if (!re.has_value())
    {
        JLOG(j_.trace()) << "WASM Failure: " << transHuman(re.error().ter);
        ctx_.setVMReturnCode(TERtoInt(re.error().ter));
        return re.error().ter;
    }

    auto const ret = re->result;
    ctx_.setVMReturnCode(ret);
    if (ret < 0)
    {
        JLOG(j_.trace()) << "WASM Execution Failed: " << ret;
        return tecBYTECODE_REJECTED;
    }

    if (auto const res = contract::finalizeContractData(
            ctx_.registry,
            ctx_.view(),
            contractAccount,
            contractCtx.result.dataMap,
            ctx_.tx.getTransactionID());
        !isTesSuccess(res))
    {
        JLOG(j_.trace()) << "Contract data finalization failed: " << transHuman(res);
        return res;
    }

    if (!contractCtx.result.events.empty())
        ctx_.setContractEvents(contract::contractEvents(contractCtx.result.events));

    ctx_.setEmittedTxns(contractCtx.result.emittedTxns);
    return tesSUCCESS;
}

void
ContractCall::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
ContractCall::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
