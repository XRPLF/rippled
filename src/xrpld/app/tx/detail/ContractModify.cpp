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
#include <xrpld/app/tx/detail/ContractModify.h>

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/digest.h>

namespace ripple {

XRPAmount
ContractModify::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    XRPAmount const maxAmount{
        std::numeric_limits<XRPAmount::value_type>::max()};
    XRPAmount createFee{0};

    if (tx.isFieldPresent(sfCreateCode))
        createFee = XRPAmount{
            contract::contractCreateFee(tx.getFieldVL(sfCreateCode).size())};

    if (createFee > maxAmount - view.fees().increment)
    {
        JLOG(debugLog().error())
            << "ContractModify: Create fee overflow detected.";
        return XRPAmount{INITIAL_XRP};
    }

    auto baseFee = Transactor::calculateBaseFee(view, tx);
    if (baseFee > maxAmount - createFee)
    {
        JLOG(debugLog().error())
            << "ContractModify: Total fee overflow detected.";
        return XRPAmount{INITIAL_XRP};
    }

    return createFee + baseFee;
}

NotTEC
ContractModify::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureSmartContract))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto const flags = ctx.tx.getFlags();
    if (flags & tfUniversalMask)
    {
        JLOG(ctx.j.error())
            << "ContractModify: only tfUniversalMask is allowed.";
        return temINVALID_FLAG;
    }

    // Either ContractCode or ContractHash must be present.
    if ((!ctx.tx.isFieldPresent(sfContractCode) &&
         !ctx.tx.isFieldPresent(sfContractHash)) ||
        (ctx.tx.isFieldPresent(sfContractCode) &&
         ctx.tx.isFieldPresent(sfContractHash)))
    {
        JLOG(ctx.j.error()) << "ContractModify: Either ContractCode or "
                               "ContractHash must be present, but not both.";
        return temMALFORMED;
    }

    // Validate Functions & Function Parameters.
    if (auto const res = contract::preflightFunctions(ctx.tx, ctx.j);
        !isTesSuccess(res))
        return res;

    // Validate Instance Parameters.
    if (auto const res = contract::preflightInstanceParameters(ctx.tx, ctx.j);
        !isTesSuccess(res))
        return res;

    // Validate Instance Parameter Values.
    if (auto const res =
            contract::preflightInstanceParameterValues(ctx.tx, ctx.j);
        !isTesSuccess(res))
        return res;

    return preflight2(ctx);
}

TER
ContractModify::preclaim(PreclaimContext const& ctx)
{
    AccountID const account = ctx.tx.getAccountID(sfAccount);
    AccountID const contractAccount = ctx.tx.isFieldPresent(sfContractAccount)
        ? ctx.tx.getAccountID(sfContractAccount)
        : account;

    auto const caSle = ctx.view.read(keylet::account(contractAccount));
    if (!caSle)
    {
        JLOG(ctx.j.error())
            << "ContractModify: Contract Account does not exist.";
        return tecNO_TARGET;
    }

    uint256 const contractID = caSle->getFieldH256(sfContractID);
    auto const contractSle = ctx.view.read(keylet::contract(contractID));
    if (!contractSle)
    {
        JLOG(ctx.j.error()) << "ContractModify: Contract does not exist.";
        return tecNO_TARGET;
    }

    if (ctx.tx.isFieldPresent(sfContractAccount) &&
        contractSle->getAccountID(sfAccount) != account)
    {
        JLOG(ctx.j.error()) << "ContractModify: Cannot modify a contract that "
                               "does not belong to the account.";
        return tecNO_PERMISSION;
    }

    std::uint32_t flags = contractSle->getFlags();

    // Check if the contract is immutable.
    if (flags & tfImmutable)
    {
        JLOG(ctx.j.error()) << "ContractModify: Contract is immutable.";
        return tecNO_PERMISSION;
    }

    // Check if the contract code is immutable.
    if (flags & tfCodeImmutable && ctx.tx.isFieldPresent(sfContractCode))
    {
        JLOG(ctx.j.error()) << "ContractModify: ContractCode is immutable.";
        return tecNO_PERMISSION;
    }

    // Check if the contract ABI is immutable.
    if (flags & tfABIImmutable)
    {
        if (!ctx.tx.isFieldPresent(sfContractCode))
        {
            JLOG(ctx.j.error()) << "ContractModify: ContractCode must be "
                                   "present when modifying ABI.";
            return tecNO_PERMISSION;
        }

        if (!ctx.tx.isFieldPresent(sfFunctions))
        {
            JLOG(ctx.j.error()) << "ContractModify: Functions must be present "
                                   "when modifying ABI.";
            return tecNO_PERMISSION;
        }

        JLOG(ctx.j.error()) << "ContractModify: ABI is immutable.";
        return tecNO_PERMISSION;
    }

    // Can only include 1 or the 3 flags: tfCodeImmutable, tfABIImmutable,
    // tfImmutable.
    if ((flags & (tfCodeImmutable | tfABIImmutable | tfImmutable)) >
        tfImmutable)
    {
        JLOG(ctx.j.error())
            << "ContractModify: Cannot set more than one immutability flag.";
        return temINVALID_FLAG;
    }

    bool isInstall = ctx.tx.isFieldPresent(sfContractHash);
    auto contractHash = ctx.tx.at(~sfContractHash);
    if (ctx.tx.isFieldPresent(sfContractCode))
    {
        ripple::Blob wasmBytes = ctx.tx.getFieldVL(sfContractCode);
        if (wasmBytes.empty())
        {
            JLOG(ctx.j.error())
                << "ContractModify: ContractCode provided is empty.";
            return temMALFORMED;
        }

        contractHash = ripple::sha512Half_s(
            ripple::Slice(wasmBytes.data(), wasmBytes.size()));
        if (ctx.view.exists(keylet::contractSource(*contractHash)))
            isInstall = true;

        // Iterate through the functions and validate them?
        // HostFunctions mock;
        // auto const re = preflightEscrowWasm(wasmBytes, "finish", {}, &mock,
        // ctx.j); if (!isTesSuccess(re))
        // {
        //     JLOG(ctx.j.debug()) << "EscrowCreate.FinishFunction bad WASM";
        //     return re;
        // }
    }

    // The ABI provided in Functions doesn't match the code.

    if (isInstall)
    {
        auto const sle = ctx.view.read(keylet::contractSource(*contractHash));
        if (!sle)
        {
            JLOG(ctx.j.error())
                << "ContractModify: ContractSource ledger object not found for "
                   "the provided ContractHash.";
            return tefINTERNAL;  // LCOV_EXCL_LINE
        }

        if (sle->isFieldPresent(sfInstanceParameters) &&
            !ctx.tx.isFieldPresent(sfInstanceParameterValues))
        {
            JLOG(ctx.j.error())
                << "ContractModify: ContractHash is present, but "
                   "InstanceParameterValues is missing.";
            return temMALFORMED;
        }

        auto const& instanceParams = sle->getFieldArray(sfInstanceParameters);
        auto const& instanceParamValues =
            ctx.tx.getFieldArray(sfInstanceParameterValues);
        if (auto const isValit = contract::validateParameterMapping(
                instanceParams, instanceParamValues, ctx.j);
            !isValit)
        {
            JLOG(ctx.j.error())
                << "ContractModify: InstanceParameters do not match what's in "
                   "the existing ContractSource ledger object.";
            return temMALFORMED;
        }
    }
    return tesSUCCESS;
}

TER
ContractModify::doApply()
{
    AccountID const account = ctx_.tx.getAccountID(sfAccount);
    AccountID const contractAccount = ctx_.tx.isFieldPresent(sfContractAccount)
        ? ctx_.tx.getAccountID(sfContractAccount)
        : account;

    auto const caSle = ctx_.view().read(keylet::account(contractAccount));
    if (!caSle)
    {
        JLOG(ctx_.journal.error()) << "ContractModify: Account does not exist.";
        return tefINTERNAL;
    }

    uint256 const contractID = caSle->getFieldH256(sfContractID);
    auto const contractSle = ctx_.view().peek(keylet::contract(contractID));
    if (!contractSle)
    {
        JLOG(ctx_.journal.error())
            << "ContractModify: Contract does not exist.";
        return tefINTERNAL;
    }

    if (ctx_.tx.isFieldPresent(sfContractCode))
    {
        ripple::Blob wasmBytes = ctx_.tx.getFieldVL(sfContractCode);
        auto const contractHash = ripple::sha512Half_s(
            ripple::Slice(wasmBytes.data(), wasmBytes.size()));
        auto const sourceKeylet = keylet::contractSource(contractHash);
        auto sourceSle = ctx_.view().peek(sourceKeylet);
        if (!sourceSle)
        {
            // create the new ContractSource
            sourceSle = std::make_shared<SLE>(sourceKeylet);
            sourceSle->at(sfContractHash) = contractHash;
            sourceSle->at(sfContractCode) = makeSlice(wasmBytes);
            sourceSle->setFieldArray(
                sfFunctions, ctx_.tx.getFieldArray(sfFunctions));
            if (ctx_.tx.isFieldPresent(sfInstanceParameters))
                sourceSle->setFieldArray(
                    sfInstanceParameters,
                    ctx_.tx.getFieldArray(sfInstanceParameters));
            sourceSle->at(sfReferenceCount) = 1;
            ctx_.view().insert(sourceSle);
        }
        else
        {
            // update the existing ContractSource
            sourceSle->setFieldU64(
                sfReferenceCount, sourceSle->getFieldU64(sfReferenceCount) + 1);
            ctx_.view().update(sourceSle);
        }

        // update the Contract
        contractSle->setFieldH256(sfContractHash, contractHash);
        if (ctx_.tx.isFieldPresent(sfInstanceParameterValues))
            contractSle->setFieldArray(
                sfInstanceParameterValues,
                ctx_.tx.getFieldArray(sfInstanceParameterValues));

        ctx_.view().update(contractSle);
    }
    else if (ctx_.tx.isFieldPresent(sfContractHash))
    {
        // set new contract hash
        contractSle->setFieldH256(
            sfContractHash, ctx_.tx.getFieldH256(sfContractHash));

        // set new instance parameter values if present
        if (ctx_.tx.isFieldPresent(sfInstanceParameterValues))
            contractSle->setFieldArray(
                sfInstanceParameterValues,
                ctx_.tx.getFieldArray(sfInstanceParameterValues));

        ctx_.view().update(contractSle);
    }

    // lower the reference count of the previous ContractSource
    auto oldSourceSle = ctx_.view().peek(
        keylet::contractSource(contractSle->getFieldH256(sfContractHash)));
    if (oldSourceSle->getFieldU64(sfReferenceCount) == 1)
    {
        ctx_.view().erase(oldSourceSle);
    }
    else
    {
        oldSourceSle->setFieldU64(
            sfReferenceCount, oldSourceSle->getFieldU64(sfReferenceCount) - 1);
        ctx_.view().update(oldSourceSle);
    }

    return tesSUCCESS;
}

}  // namespace ripple
