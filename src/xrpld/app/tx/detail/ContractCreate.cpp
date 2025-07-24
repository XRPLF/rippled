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

#include <xrpld/app/tx/apply.h>
#include <xrpld/app/tx/detail/ContractCreate.h>
#include <xrpld/ledger/Sandbox.h>
#include <xrpld/ledger/View.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/digest.h>

namespace ripple {

XRPAmount
ContractCreate::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    return view.fees().increment;
}

NotTEC
ContractCreate::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureSmartContract))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto const flags = ctx.tx.getFlags();
    if (flags & tfContractCreateMask)
    {
        JLOG(ctx.j.error())
            << "ContractCreate: tfContractCreateMask is not allowed.";
        return temINVALID_FLAG;
    }

    // Either ContractCode or ContractHash must be present.
    if (!ctx.tx.isFieldPresent(sfContractCode) &&
        !ctx.tx.isFieldPresent(sfContractHash))
    {
        JLOG(ctx.j.error())
            << "ContractCreate: Missing both ContractCode and ContractHash.";
        return temMALFORMED;
    }

    // If ContractCode is present, InstanceParameters and Functions must also be
    // present. if (ctx.tx.isFieldPresent(sfContractCode) &&
    // (!ctx.tx.isFieldPresent(sfInstanceParameters) ||
    // !ctx.tx.isFieldPresent(sfFunctions)))
    // {
    //     JLOG(ctx.j.error())
    //         << "ContractCreate: InstanceParameters & Functions must be
    //         present with ContractCode.";
    //     return temMALFORMED;
    // }

    return preflight2(ctx);
}

TER
ContractCreate::preclaim(PreclaimContext const& ctx)
{
    // ContractHash is provided but there is no existing corresponding
    // ContractSource ledger object
    auto const contractHash = ctx.tx[~sfContractHash];
    if (contractHash && !ctx.view.exists(keylet::contractSource(*contractHash)))
    {
        JLOG(ctx.j.error())
            << "ContractCreate: ContractHash is provided but there is no "
               "existing corresponding ContractSource ledger object.";
        return temMALFORMED;
    }

    // The ContractCode provided is invalid.

    // The ABI provided in Functions doesn't match the code.

    // InstanceParameters don't match what's in the existing ContractSource
    // ledger object.

    return tesSUCCESS;
}

TER
ContractCreate::doApply()
{
    if (ctx_.tx.isFieldPresent(sfContractHash))
    {
        auto const contractHash = ctx_.tx[sfContractHash];
        auto const sle = ctx_.view().peek(keylet::contractSource(contractHash));
        if (!sle)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        // Update ContractSource ReferenceCount
    }
    else
    {
        ripple::Blob wasmBytes = ctx_.tx.getFieldVL(sfContractCode);
        auto const contractHash = ripple::sha512Half_s(
            ripple::Slice(wasmBytes.data(), wasmBytes.size()));
        auto contractSourceSle =
            std::make_shared<SLE>(keylet::contractSource(contractHash));

        contractSourceSle->at(sfContractHash) = contractHash;
        contractSourceSle->at(sfContractCode) = makeSlice(wasmBytes);
        contractSourceSle->setFieldArray(
            sfFunctions, ctx_.tx.getFieldArray(sfFunctions));
        // if (ctx_.tx.isFieldPresent(sfInstanceParameters))
        //     contractSourceSle->at(sfInstanceParameters) =
        //     ctx_.tx.getFieldArray(sfInstanceParameters);
        contractSourceSle->at(sfReferenceCount) = 1;

        ctx_.view().insert(contractSourceSle);

        auto maybePseudo =
            createPseudoAccount(view(), contractSourceSle->key(), sfContractID);
        if (!maybePseudo)
            return maybePseudo.error();  // LCOV_EXCL_LINE
        auto& pseudo = *maybePseudo;
        auto pseudoId = pseudo->at(sfAccount);
        auto contractSle = std::make_shared<SLE>(keylet::contract(pseudoId));
        contractSle->at(sfOwner) = pseudoId;
        contractSle->at(sfAccount) = account_;
        contractSle->at(sfFlags) = ctx_.tx.getFlags();
        contractSle->at(sfContractHash) = contractHash;

        // if (ctx_.tx.isFieldPresent(sfInstanceParameters))
        //     contractSle->at(sfInstanceParameters) =
        //     ctx_.tx.getFieldArray(sfInstanceParameters);

        // if (ctx_.tx.isFieldPresent(sfURI))
        //     contractSle->at(sfURI) = ctx_.tx.getFieldVL(sfURI);

        ctx_.view().insert(contractSle);
    }
    return tesSUCCESS;
}

}  // namespace ripple
