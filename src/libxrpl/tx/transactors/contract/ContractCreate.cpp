#include <xrpl/basics/Log.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/ContractUtils.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STData.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/tx/transactors/contract/ContractCreate.h>

namespace xrpl {

XRPAmount
ContractCreate::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    XRPAmount const maxAmount{std::numeric_limits<XRPAmount::value_type>::max()};
    XRPAmount createFee{0};

    if (tx.isFieldPresent(sfCreateCode))
        createFee = XRPAmount{contract::contractCreateFee(tx.getFieldVL(sfCreateCode).size())};

    if (createFee > maxAmount - view.fees().increment)
    {
        JLOG(debugLog().trace()) << "ContractCreate: Create fee overflow detected.";
        return XRPAmount{INITIAL_XRP};
    }

    createFee += view.fees().increment;

    auto baseFee = Transactor::calculateBaseFee(view, tx);
    if (baseFee > maxAmount - createFee)
    {
        JLOG(debugLog().trace()) << "ContractCreate: Total fee overflow detected.";
        return XRPAmount{INITIAL_XRP};
    }

    return createFee + baseFee;
}

std::uint32_t
ContractCreate::getFlagsMask(PreflightContext const& ctx)
{
    return tfContractMask;
}

NotTEC
ContractCreate::preflight(PreflightContext const& ctx)
{
    auto const flags = ctx.tx.getFlags();

    if ((flags & (tfCodeImmutable | tfABIImmutable | tfImmutable)) > tfImmutable)
    {
        JLOG(ctx.j.trace()) << "ContractCreate: Cannot set more than one immutability flag.";
        return temINVALID_FLAG;
    }

    if (!ctx.tx.isFieldPresent(sfContractCode) && !ctx.tx.isFieldPresent(sfContractHash))
    {
        JLOG(ctx.j.trace()) << "ContractCreate: Neither ContractCode nor ContractHash present";
        return temMALFORMED;
    }

    if (ctx.tx.isFieldPresent(sfContractCode) && ctx.tx.isFieldPresent(sfContractHash))
    {
        JLOG(ctx.j.trace()) << "ContractCreate: Both ContractCode and ContractHash present";
        return temMALFORMED;
    }

    if (auto const res = contract::preflightFunctions(ctx.tx, ctx.j); !isTesSuccess(res))
    {
        JLOG(ctx.j.trace()) << "ContractCreate: Functions validation failed: " << transToken(res);
        return res;
    }

    if (auto const res = contract::preflightInstanceParameters(ctx.tx, ctx.j); !isTesSuccess(res))
    {
        JLOG(ctx.j.trace()) << "ContractCreate: InstanceParameters validation failed: "
                            << transToken(res);
        return res;
    }

    if (auto const res = contract::preflightInstanceParameterValues(ctx.tx, ctx.j);
        !isTesSuccess(res))
    {
        JLOG(ctx.j.trace()) << "ContractCreate: InstanceParameterValues validation failed: "
                            << transToken(res);
        return res;
    }

    return tesSUCCESS;
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
        JLOG(ctx.j.trace()) << "ContractCreate: ContractHash provided but no "
                               "corresponding ContractSource exists";
        return temMALFORMED;
    }

    // The ContractCode provided is invalid.
    if (ctx.tx.isFieldPresent(sfContractCode))
    {
        xrpl::Blob wasmBytes = ctx.tx.getFieldVL(sfContractCode);
        if (wasmBytes.empty())
        {
            JLOG(ctx.j.trace()) << "ContractCreate: ContractCode provided is empty.";
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

    // InstanceParameters don't match what's in the existing ContractSource
    // ledger object.
    if (isInstall && ctx.tx.isFieldPresent(sfInstanceParameterValues))
    {
        auto const sle = ctx.view.read(keylet::contractSource(*contractHash));
        if (!sle)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        // Already validated in preflight, but we can check here too.
        auto const& instanceParams = sle->getFieldArray(sfInstanceParameters);
        auto const& instanceParamValues = ctx.tx.getFieldArray(sfInstanceParameterValues);
        if (auto const isValid =
                contract::validateParameterMapping(instanceParams, instanceParamValues, ctx.j);
            !isValid)
        {
            JLOG(ctx.j.trace()) << "ContractCreate: InstanceParameters do not match what's in "
                                   "the existing ContractSource ledger object.";
            return temMALFORMED;
        }
    }

    return tesSUCCESS;
}

TER
ContractCreate::doApply()
{
    auto const accountSle = ctx_.view().peek(keylet::account(account_));
    if (!accountSle)
    {
        JLOG(j_.trace()) << "ContractCreate: Account not found.";
        return tefINTERNAL;  // LCOV_EXCL_LINE
    }

    std::shared_ptr<SLE> sourceSle;
    bool isInstall = ctx_.tx.isFieldPresent(sfContractHash);
    auto contractHash = ctx_.tx[~sfContractHash];
    xrpl::Blob wasmBytes;
    if (ctx_.tx.isFieldPresent(sfContractCode))
    {
        wasmBytes = ctx_.tx.getFieldVL(sfContractCode);
        contractHash = xrpl::sha512Half_s(xrpl::Slice(wasmBytes.data(), wasmBytes.size()));
        if (ctx_.view().exists(keylet::contractSource(*contractHash)))
            isInstall = true;
    }

    if (isInstall)
    {
        sourceSle = ctx_.view().peek(keylet::contractSource(*contractHash));
        if (!sourceSle)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        sourceSle->at(sfReferenceCount) = sourceSle->getFieldU64(sfReferenceCount) + 1;
        ctx_.view().update(sourceSle);
    }
    else
    {
        sourceSle = std::make_shared<SLE>(keylet::contractSource(*contractHash));
        sourceSle->at(sfContractHash) = *contractHash;
        sourceSle->at(sfContractCode) = makeSlice(wasmBytes);
        sourceSle->setFieldArray(sfFunctions, ctx_.tx.getFieldArray(sfFunctions));
        if (ctx_.tx.isFieldPresent(sfInstanceParameters))
            sourceSle->setFieldArray(
                sfInstanceParameters, ctx_.tx.getFieldArray(sfInstanceParameters));
        sourceSle->at(sfReferenceCount) = 1;

        ctx_.view().insert(sourceSle);
    }

    std::uint32_t const seq = ctx_.tx.getSeqValue();
    auto const contractKeylet = keylet::contract(*contractHash, account_, seq);
    auto contractSle = std::make_shared<SLE>(contractKeylet);

    auto maybePseudo = createPseudoAccount(view(), contractSle->key(), sfContractID);
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
            sfInstanceParameterValues, ctx_.tx.getFieldArray(sfInstanceParameterValues));

    if (ctx_.tx.isFieldPresent(sfURI))
        contractSle->setFieldVL(sfURI, ctx_.tx.getFieldVL(sfURI));

    ctx_.view().insert(contractSle);

    // Handle the instance parameters for the contract creation.
    if (ctx_.tx.isFieldPresent(sfInstanceParameterValues))
    {
        STArray const& params = ctx_.tx.getFieldArray(sfInstanceParameterValues);

        // Note: We have to do preclaim and apply here because we will only have
        // the pseudo account after the contract is created.
        if (auto ter =
                contract::preclaimFlagParameters(ctx_.view(), account_, pseudoAccount, params, j_);
            !isTesSuccess(ter))
        {
            JLOG(j_.trace()) << "ContractCreate: Failed to preclaim flag parameters.";
            return ter;
        }

        if (auto ter = contract::doApplyFlagParameters(
                ctx_.view(), ctx_.tx, account_, pseudoAccount, params, preFeeBalance_, j_);
            !isTesSuccess(ter))
        {
            JLOG(j_.trace()) << "ContractCreate: Failed to apply flag parameters.";
            return ter;
        }
    }

    // Add Contract to ContractAccount Dir
    // TODO: use dirLink
    {
        auto const page = view().dirInsert(
            keylet::ownerDir(pseudoAccount), contractKeylet, describeOwnerDir(pseudoAccount));

        if (!page)
            return tecDIR_FULL;

        contractSle->setFieldU64(sfOwnerNode, *page);
        adjustOwnerCount(ctx_.view(), pseudoSle, 1, j_);
    }

    return tesSUCCESS;
}

}  // namespace xrpl
