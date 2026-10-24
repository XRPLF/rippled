#include <xrpl/tx/transactors/contract/ContractModify.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/ContractUtils.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/transactors/contract/ContractCreate.h>

#include <cstdint>
#include <limits>
#include <memory>

namespace xrpl {

XRPAmount
ContractModify::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    XRPAmount const maxAmount{std::numeric_limits<XRPAmount::value_type>::max()};
    XRPAmount createFee{0};

    if (tx.isFieldPresent(sfContractCode))
        createFee = XRPAmount{contract::contractCreateFee(tx.getFieldVL(sfContractCode).size())};

    if (createFee > maxAmount - view.fees().increment)
    {
        JLOG(debugLog().trace()) << "ContractModify: Create fee overflow detected.";
        return XRPAmount{kInitialXrp};
    }

    auto const baseFee = Transactor::calculateBaseFee(view, tx);
    if (baseFee > maxAmount - createFee)
    {
        JLOG(debugLog().trace()) << "ContractModify: Total fee overflow detected.";
        return XRPAmount{kInitialXrp};
    }

    return createFee + baseFee;
}

NotTEC
ContractModify::preflight(PreflightContext const& ctx)
{
    auto const flags = ctx.tx.getFlags();
    if ((flags & tfUniversalMask) != 0u)
    {
        JLOG(ctx.j.trace()) << "ContractModify: only tfUniversalMask is allowed.";
        return temINVALID_FLAG;
    }

    // Either ContractCode or ContractHash must be present.
    if (ctx.tx.isFieldPresent(sfContractCode) && ctx.tx.isFieldPresent(sfContractHash))
    {
        JLOG(ctx.j.trace()) << "ContractModify: Both ContractCode and ContractHash present";
        return temMALFORMED;
    }

    if (ctx.tx.isFieldPresent(sfContractCode) &&
        ctx.tx.getFieldVL(sfContractCode).size() > kMaxBytecodeSizeLimit)
    {
        JLOG(ctx.j.trace()) << "ContractModify: ContractCode over the protocol size limit";
        return temMALFORMED;
    }

    // Validate Functions & Function Parameters.
    if (auto const res = contract::preflightFunctions(ctx.tx, ctx.j); !isTesSuccess(res))
        return res;

    // Validate Instance Parameters.
    if (auto const res = contract::preflightInstanceParameters(ctx.tx, ctx.j); !isTesSuccess(res))
        return res;

    // Validate Instance Parameter Values.
    if (auto const res = contract::preflightInstanceParameterValues(ctx.tx, ctx.j);
        !isTesSuccess(res))
        return res;

    if (ctx.tx.isFieldPresent(sfOwner))
    {
        if (ctx.tx.getAccountID(sfOwner) == ctx.tx.getAccountID(sfAccount))
            return temMALFORMED;

        if (ctx.tx.getAccountID(sfOwner) == ctx.tx.getAccountID(sfContractAccount))
            return temMALFORMED;
    }

    return tesSUCCESS;
}

TER
ContractModify::preclaim(PreclaimContext const& ctx)
{
    AccountID const account = ctx.tx.getAccountID(sfAccount);
    AccountID const contractAccount =
        ctx.tx.isFieldPresent(sfContractAccount) ? ctx.tx.getAccountID(sfContractAccount) : account;

    auto const contractAccountSle = ctx.view.read(keylet::account(contractAccount));
    if (!contractAccountSle)
    {
        JLOG(ctx.j.trace()) << "ContractModify: Contract Account does not exist.";
        return tecNO_TARGET;
    }

    // Only a contract's pseudo-account names a Contract.
    auto const contractSle = contractAccountSle->isFieldPresent(sfContractID)
        ? ctx.view.read(keylet::contract(contractAccountSle->getFieldH256(sfContractID)))
        : nullptr;
    if (!contractSle)
    {
        JLOG(ctx.j.trace()) << "ContractModify: Contract does not exist.";
        return tecNO_TARGET;
    }

    if (ctx.tx.isFieldPresent(sfContractAccount) && contractSle->getAccountID(sfOwner) != account)
    {
        JLOG(ctx.j.trace()) << "ContractModify: Cannot modify a contract that "
                               "does not belong to the account.";
        return tecNO_PERMISSION;
    }

    std::uint32_t const flags = contractSle->getFlags();

    // Check if the contract is immutable.
    if ((flags & tfImmutable) != 0u)
    {
        JLOG(ctx.j.trace()) << "ContractModify: Contract is immutable.";
        return tecNO_PERMISSION;
    }

    // Check if the contract code is immutable.
    if (((flags & tfCodeImmutable) != 0u) && ctx.tx.isFieldPresent(sfContractCode))
    {
        JLOG(ctx.j.trace()) << "ContractModify: ContractCode is immutable.";
        return tecNO_PERMISSION;
    }

    // Check if the contract ABI is immutable.
    if ((flags & tfABIImmutable) != 0u)
    {
        if (!ctx.tx.isFieldPresent(sfContractCode))
        {
            JLOG(ctx.j.trace()) << "ContractModify: ContractCode must be "
                                   "present when modifying ABI.";
            return tecNO_PERMISSION;
        }

        if (!ctx.tx.isFieldPresent(sfFunctions))
        {
            JLOG(ctx.j.trace()) << "ContractModify: Functions must be present "
                                   "when modifying ABI.";
            return tecNO_PERMISSION;
        }

        JLOG(ctx.j.trace()) << "ContractModify: ABI is immutable.";
        return tecNO_PERMISSION;
    }

    // Installing reuses an existing ContractSource, named by ContractHash or by the code's hash.
    bool isInstall = false;
    uint256 contractHash;
    if (ctx.tx.isFieldPresent(sfContractCode) || ctx.tx.isFieldPresent(sfContractHash))
    {
        contractHash = ContractCreate::sourceHash(ctx.tx);
        isInstall = ctx.view.exists(keylet::contractSource(contractHash));
        if (ctx.tx.isFieldPresent(sfContractHash) && !isInstall)
        {
            JLOG(ctx.j.trace()) << "ContractModify: ContractHash provided but no "
                                   "corresponding ContractSource exists";
            return temMALFORMED;
        }
    }

    if (ctx.tx.isFieldPresent(sfContractCode))
    {
        if (auto const ter =
                ContractCreate::checkContractCode(ctx.registry.get().getFees(), ctx.tx, ctx.j);
            !isTesSuccess(ter))
            return ter;
    }

    // The ABI provided in Functions doesn't match the code.

    if (isInstall)
    {
        auto const sle = ctx.view.read(keylet::contractSource(contractHash));
        if (!sle)
        {
            JLOG(ctx.j.trace()) << "ContractModify: ContractSource ledger object not found for "
                                   "the provided ContractHash.";
            return tefINTERNAL;  // LCOV_EXCL_LINE
        }

        if (sle->isFieldPresent(sfInstanceParameters) &&
            !ctx.tx.isFieldPresent(sfInstanceParameterValues))
        {
            JLOG(ctx.j.trace()) << "ContractModify: ContractHash is present, but "
                                   "InstanceParameterValues is missing.";
            return temMALFORMED;
        }

        auto const& instanceParams = sle->getFieldArray(sfInstanceParameters);
        auto const& instanceParamValues = ctx.tx.getFieldArray(sfInstanceParameterValues);
        if (auto const isValid =
                contract::validateParameterMapping(instanceParams, instanceParamValues, ctx.j);
            !isValid)
        {
            JLOG(ctx.j.trace()) << "ContractModify: InstanceParameters do not match what's in "
                                   "the existing ContractSource ledger object.";
            return temMALFORMED;
        }
    }

    if (ctx.tx.isFieldPresent(sfOwner))
    {
        auto const ownerSle = ctx.view.read(keylet::account(ctx.tx.getAccountID(sfOwner)));
        if (!ownerSle)
        {
            JLOG(ctx.j.trace()) << "ContractModify: New owner account does not exist.";
            return tecNO_TARGET;
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

    // Preclaim found the contract account and its Contract; a Contract always has its source.
    auto const contractAccountSle = ctx_.view().read(keylet::account(contractAccount));
    if (!contractAccountSle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const contractSle =
        ctx_.view().peek(keylet::contract(contractAccountSle->getFieldH256(sfContractID)));
    if (!contractSle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const currentSourceSle =
        ctx_.view().peek(keylet::contractSource(contractSle->getFieldH256(sfContractHash)));
    if (!currentSourceSle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if (ctx_.tx.isFieldPresent(sfContractCode) || ctx_.tx.isFieldPresent(sfContractHash))
    {
        // Point the contract at the named source, creating it from ContractCode when absent.
        auto const contractHash = ContractCreate::sourceHash(ctx_.tx);
        auto const sourceKeylet = keylet::contractSource(contractHash);
        if (auto const sourceSle = ctx_.view().peek(sourceKeylet))
        {
            sourceSle->setFieldU64(sfReferenceCount, sourceSle->getFieldU64(sfReferenceCount) + 1);
            ctx_.view().update(sourceSle);
        }
        else
        {
            auto const newSourceSle = std::make_shared<SLE>(sourceKeylet);
            newSourceSle->at(sfContractHash) = contractHash;
            newSourceSle->at(sfContractCode) = makeSlice(ctx_.tx.getFieldVL(sfContractCode));
            newSourceSle->setFieldArray(sfFunctions, ctx_.tx.getFieldArray(sfFunctions));
            if (ctx_.tx.isFieldPresent(sfInstanceParameters))
            {
                newSourceSle->setFieldArray(
                    sfInstanceParameters, ctx_.tx.getFieldArray(sfInstanceParameters));
            }
            newSourceSle->at(sfReferenceCount) = 1;
            ctx_.view().insert(newSourceSle);
        }

        contractSle->setFieldH256(sfContractHash, contractHash);
        if (ctx_.tx.isFieldPresent(sfInstanceParameterValues))
        {
            contractSle->setFieldArray(
                sfInstanceParameterValues, ctx_.tx.getFieldArray(sfInstanceParameterValues));
        }
        ctx_.view().update(contractSle);

        // Release the previous source, erasing it with its last reference.
        if (currentSourceSle->getFieldU64(sfReferenceCount) == 1)
        {
            ctx_.view().erase(currentSourceSle);
        }
        else
        {
            currentSourceSle->setFieldU64(
                sfReferenceCount, currentSourceSle->getFieldU64(sfReferenceCount) - 1);
            ctx_.view().update(currentSourceSle);
        }
    }
    else if (ctx_.tx.isFieldPresent(sfInstanceParameterValues))
    {
        // only updating instance parameter values
        contractSle->setFieldArray(
            sfInstanceParameterValues, ctx_.tx.getFieldArray(sfInstanceParameterValues));

        ctx_.view().update(contractSle);
    }

    if (ctx_.tx.isFieldPresent(sfOwner))
    {
        contractSle->setAccountID(sfOwner, ctx_.tx.getAccountID(sfOwner));
        ctx_.view().update(contractSle);
    }

    return tesSUCCESS;
}

void
ContractModify::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
ContractModify::finalizeInvariants(
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
