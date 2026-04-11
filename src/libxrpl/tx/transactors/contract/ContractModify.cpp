#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/ContractUtils.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/tx/transactors/contract/ContractModify.h>

namespace xrpl {

XRPAmount
ContractModify::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    XRPAmount const maxAmount{std::numeric_limits<XRPAmount::value_type>::max()};
    XRPAmount createFee{0};

    if (tx.isFieldPresent(sfCreateCode))
        createFee = XRPAmount{contract::contractCreateFee(tx.getFieldVL(sfCreateCode).size())};

    if (createFee > maxAmount - view.fees().increment)
    {
        JLOG(debugLog().trace()) << "ContractModify: Create fee overflow detected.";
        return XRPAmount{INITIAL_XRP};
    }

    auto baseFee = Transactor::calculateBaseFee(view, tx);
    if (baseFee > maxAmount - createFee)
    {
        JLOG(debugLog().trace()) << "ContractModify: Total fee overflow detected.";
        return XRPAmount{INITIAL_XRP};
    }

    return createFee + baseFee;
}

NotTEC
ContractModify::preflight(PreflightContext const& ctx)
{
    auto const flags = ctx.tx.getFlags();
    if (flags & tfUniversalMask)
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

    uint256 const contractID = contractAccountSle->getFieldH256(sfContractID);
    auto const contractSle = ctx.view.read(keylet::contract(contractID));
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
    if (flags & tfImmutable)
    {
        JLOG(ctx.j.trace()) << "ContractModify: Contract is immutable.";
        return tecNO_PERMISSION;
    }

    // Check if the contract code is immutable.
    if (flags & tfCodeImmutable && ctx.tx.isFieldPresent(sfContractCode))
    {
        JLOG(ctx.j.trace()) << "ContractModify: ContractCode is immutable.";
        return tecNO_PERMISSION;
    }

    // Check if the contract ABI is immutable.
    if (flags & tfABIImmutable)
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

    // Can only include 1 of the 3 flags: tfCodeImmutable, tfABIImmutable,
    // tfImmutable.
    if ((flags & (tfCodeImmutable | tfABIImmutable | tfImmutable)) > tfImmutable)
    {
        JLOG(ctx.j.trace()) << "ContractModify: Cannot set more than one immutability flag.";
        return temINVALID_FLAG;
    }

    bool isInstall = ctx.tx.isFieldPresent(sfContractHash);
    auto contractHash = ctx.tx.at(~sfContractHash);
    if (ctx.tx.isFieldPresent(sfContractCode))
    {
        xrpl::Blob wasmBytes = ctx.tx.getFieldVL(sfContractCode);
        if (wasmBytes.empty())
        {
            JLOG(ctx.j.trace()) << "ContractModify: ContractCode provided is empty.";
            return temMALFORMED;
        }

        contractHash = xrpl::sha512Half_s(xrpl::Slice(wasmBytes.data(), wasmBytes.size()));
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

    auto const contractAccountSle = ctx_.view().read(keylet::account(contractAccount));
    if (!contractAccountSle)
    {
        JLOG(ctx_.journal.trace()) << "ContractModify: Account does not exist.";
        return tefINTERNAL;
    }

    uint256 const contractID = contractAccountSle->getFieldH256(sfContractID);
    auto const contractSle = ctx_.view().peek(keylet::contract(contractID));
    if (!contractSle)
    {
        JLOG(ctx_.journal.trace()) << "ContractModify: Contract does not exist.";
        return tefINTERNAL;
    }

    auto currentSourceSle =
        ctx_.view().peek(keylet::contractSource(contractSle->getFieldH256(sfContractHash)));
    if (!currentSourceSle)
    {
        JLOG(ctx_.journal.trace()) << "ContractModify: ContractSource does not exist.";
        return tefINTERNAL;
    }

    if (ctx_.tx.isFieldPresent(sfContractCode))
    {
        JLOG(ctx_.journal.trace()) << "ContractModify: Modifying ContractCode/ContractHash.";
        xrpl::Blob wasmBytes = ctx_.tx.getFieldVL(sfContractCode);
        auto const contractHash =
            xrpl::sha512Half_s(xrpl::Slice(wasmBytes.data(), wasmBytes.size()));
        auto const sourceKeylet = keylet::contractSource(contractHash);
        auto sourceSle = ctx_.view().peek(sourceKeylet);
        if (!sourceSle)
        {
            JLOG(ctx_.journal.trace()) << "ContractModify: Creating new ContractSource.";
            // create the new ContractSource
            sourceSle = std::make_shared<SLE>(sourceKeylet);
            sourceSle->at(sfContractHash) = contractHash;
            sourceSle->at(sfContractCode) = makeSlice(wasmBytes);
            sourceSle->setFieldArray(sfFunctions, ctx_.tx.getFieldArray(sfFunctions));
            if (ctx_.tx.isFieldPresent(sfInstanceParameters))
                sourceSle->setFieldArray(
                    sfInstanceParameters, ctx_.tx.getFieldArray(sfInstanceParameters));
            sourceSle->at(sfReferenceCount) = 1;
            ctx_.view().insert(sourceSle);
        }

        // update the Contract
        contractSle->setFieldH256(sfContractHash, contractHash);
        if (ctx_.tx.isFieldPresent(sfInstanceParameterValues))
            contractSle->setFieldArray(
                sfInstanceParameterValues, ctx_.tx.getFieldArray(sfInstanceParameterValues));

        ctx_.view().update(contractSle);

        // update the existing ContractSource
        if (currentSourceSle->getFieldU64(sfReferenceCount) == 1)
        {
            // remove the old ContractSource if no more references
            ctx_.view().erase(currentSourceSle);
        }
        else
        {
            // decrement the reference count
            currentSourceSle->setFieldU64(
                sfReferenceCount, currentSourceSle->getFieldU64(sfReferenceCount) - 1);
            ctx_.view().update(currentSourceSle);
        }
    }
    else if (ctx_.tx.isFieldPresent(sfContractHash))
    {
        auto sourceSle =
            ctx_.view().peek(keylet::contractSource(ctx_.tx.getFieldH256(sfContractHash)));
        if (!sourceSle)
        {
            JLOG(ctx_.journal.trace()) << "ContractModify: ContractSource does not exist.";
            return tefINTERNAL;
        }

        // set new contract hash
        contractSle->setFieldH256(sfContractHash, ctx_.tx.getFieldH256(sfContractHash));

        // set new instance parameter values if present
        if (ctx_.tx.isFieldPresent(sfInstanceParameterValues))
            contractSle->setFieldArray(
                sfInstanceParameterValues, ctx_.tx.getFieldArray(sfInstanceParameterValues));

        ctx_.view().update(contractSle);

        sourceSle->setFieldU64(sfReferenceCount, sourceSle->getFieldU64(sfReferenceCount) + 1);
        ctx_.view().update(sourceSle);

        // update the existing ContractSource
        if (currentSourceSle->getFieldU64(sfReferenceCount) == 1)
        {
            // remove the old ContractSource if no more references
            ctx_.view().erase(currentSourceSle);
        }
        else
        {
            // decrement the reference count
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

}  // namespace xrpl
