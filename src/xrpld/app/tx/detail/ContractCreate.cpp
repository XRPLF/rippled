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
#include <xrpld/app/tx/detail/ContractCreate.h>
#include <xrpld/ledger/View.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STData.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/digest.h>

namespace ripple {

XRPAmount
ContractCreate::calculateBaseFee(ReadView const& view, STTx const& tx)
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
            << "ContractCreate: Create fee overflow detected.";
        return XRPAmount{INITIAL_XRP};
    }

    createFee += view.fees().increment;

    auto baseFee = Transactor::calculateBaseFee(view, tx);
    if (baseFee > maxAmount - createFee)
    {
        JLOG(debugLog().error())
            << "ContractCreate: Total fee overflow detected.";
        return XRPAmount{INITIAL_XRP};
    }

    return createFee + baseFee;
}

NotTEC
ContractCreate::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureSmartContract))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto const flags = ctx.tx.getFlags();
    if (flags & tfContractMask)
    {
        JLOG(ctx.j.error()) << "ContractCreate: tfContractMask is not allowed.";
        return temINVALID_FLAG;
    }

    // Either ContractCode or ContractHash must be present.
    if ((!ctx.tx.isFieldPresent(sfContractCode) &&
         !ctx.tx.isFieldPresent(sfContractHash)) ||
        (ctx.tx.isFieldPresent(sfContractCode) &&
         ctx.tx.isFieldPresent(sfContractHash)))
    {
        JLOG(ctx.j.error()) << "ContractCreate: Either ContractCode or "
                               "ContractHash must be present, but not both.";
        return temMALFORMED;
    }

    // Valudate Functions & Function Parameters.
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
ContractCreate::preclaim(PreclaimContext const& ctx)
{
    // ContractHash is provided but there is no existing corresponding
    // ContractSource ledger object
    bool isInstall = ctx.tx.isFieldPresent(sfContractHash);
    auto contractHash = ctx.tx.at(~sfContractHash);
    if (isInstall && !ctx.view.exists(keylet::contractSource(*contractHash)))
    {
        JLOG(ctx.j.error())
            << "ContractCreate: ContractHash is provided but there is no "
               "existing corresponding ContractSource ledger object.";
        return temMALFORMED;
    }

    // The ContractCode provided is invalid.
    if (ctx.tx.isFieldPresent(sfContractCode))
    {
        ripple::Blob wasmBytes = ctx.tx.getFieldVL(sfContractCode);
        if (wasmBytes.empty())
        {
            JLOG(ctx.j.error())
                << "ContractCreate: ContractCode provided is empty.";
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

    // InstanceParameters don't match what's in the existing ContractSource
    // ledger object.
    if (isInstall && ctx.tx.isFieldPresent(sfInstanceParameterValues))
    {
        auto const sle = ctx.view.read(keylet::contractSource(*contractHash));
        if (!sle)
        {
            JLOG(ctx.j.error())
                << "ContractCreate: ContractSource ledger object not found for "
                   "the provided ContractHash.";
            return tefINTERNAL;  // LCOV_EXCL_LINE
        }

        // Already validated in preflight, but we can check here too.
        auto const& instanceParams = sle->getFieldArray(sfInstanceParameters);
        auto const& instanceParamValues =
            ctx.tx.getFieldArray(sfInstanceParameterValues);
        if (auto const isValit = contract::validateParameterMapping(
                instanceParams, instanceParamValues, ctx.j);
            !isValit)
        {
            JLOG(ctx.j.error())
                << "ContractCreate: InstanceParameters do not match what's in "
                   "the existing ContractSource ledger object.";
            return temMALFORMED;
        }
    }

    return tesSUCCESS;
}

TER
ContractCreate::doApply()
{
    auto const sleAccount = ctx_.view().peek(keylet::account(account_));
    if (!sleAccount)
    {
        JLOG(ctx_.journal.error()) << "ContractCreate: Account not found.";
        return tefINTERNAL;  // LCOV_EXCL_LINE
    }

    std::shared_ptr<SLE> sourceSle;
    bool isInstall = ctx_.tx.isFieldPresent(sfContractHash);
    auto contractHash = ctx_.tx[~sfContractHash];
    if (isInstall)
    {
        sourceSle = ctx_.view().peek(keylet::contractSource(*contractHash));
        if (!sourceSle)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        sourceSle->at(sfReferenceCount) =
            sourceSle->getFieldU64(sfReferenceCount) + 1;
        ctx_.view().update(sourceSle);
    }
    else
    {
        ripple::Blob wasmBytes = ctx_.tx.getFieldVL(sfContractCode);
        contractHash = ripple::sha512Half_s(
            ripple::Slice(wasmBytes.data(), wasmBytes.size()));
        sourceSle =
            std::make_shared<SLE>(keylet::contractSource(*contractHash));

        sourceSle->at(sfContractHash) = *contractHash;
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

    std::uint32_t const seq = ctx_.tx.getSeqValue();
    auto const contractKeylet = keylet::contract(*contractHash, seq);
    auto contractSle = std::make_shared<SLE>(contractKeylet);

    auto maybePseudo =
        createPseudoAccount(view(), contractSle->key(), sfContractID);
    if (!maybePseudo)
        return maybePseudo.error();  // LCOV_EXCL_LINE

    auto& pseudoSle = *maybePseudo;
    auto pseudoAccount = pseudoSle->at(sfAccount);

    contractSle->at(sfContractAccount) = pseudoAccount;
    contractSle->at(sfOwner) = account_;
    contractSle->at(sfFlags) = ctx_.tx.getFlags();
    contractSle->at(sfSequence) = seq;
    contractSle->at(sfContractHash) = *contractHash;
    if (ctx_.tx.isFieldPresent(sfInstanceParameterValues))
        contractSle->setFieldArray(
            sfInstanceParameterValues,
            ctx_.tx.getFieldArray(sfInstanceParameterValues));

    if (ctx_.tx.isFieldPresent(sfURI))
        contractSle->setFieldVL(sfURI, ctx_.tx.getFieldVL(sfURI));

    ctx_.view().insert(contractSle);

    // Handle the flags for the contract creation.
    if (auto ter = contract::handleFlagParameters(
            ctx_.view(), ctx_.tx, account_, pseudoAccount, ctx_.journal);
        !isTesSuccess(ter))
    {
        JLOG(ctx_.journal.error())
            << "ContractCreate: Failed to handle flag parameters.";
        return ter;
    }

    // Check Reserve Requirements
    {
        STAmount const reserve{view().fees().accountReserve(
            sleAccount->getFieldU32(sfOwnerCount) + 1)};
        STAmount const priorBalance = sleAccount->getFieldAmount(sfBalance);
        if (priorBalance < reserve)
            return tecINSUFFICIENT_RESERVE;
    }

    // Add Contract to ContractAccount Dir
    auto const page = view().dirInsert(
        keylet::ownerDir(pseudoAccount),
        contractKeylet,
        describeOwnerDir(pseudoAccount));

    if (!page)
        return tecDIR_FULL;

    contractSle->setFieldU64(sfOwnerNode, *page);

    // Update the pseudo account's owner count.
    adjustOwnerCount(view(), pseudoSle, 1, ctx_.journal);

    return tesSUCCESS;
}

}  // namespace ripple
