#include <xrpl/tx/transactors/contract/ContractCreate.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/ContractUtils.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <bit>
#include <cstdint>
#include <limits>
#include <memory>
#include <string_view>

namespace xrpl {

XRPAmount
ContractCreate::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    XRPAmount const maxAmount{std::numeric_limits<XRPAmount::value_type>::max()};
    XRPAmount createFee{0};

    if (tx.isFieldPresent(sfContractCode))
        createFee = XRPAmount{contract::contractCreateFee(tx.getFieldVL(sfContractCode).size())};

    if (createFee > maxAmount - view.fees().increment)
    {
        JLOG(debugLog().trace()) << "ContractCreate: Create fee overflow detected.";
        return XRPAmount{kInitialXrp};
    }

    createFee += view.fees().increment;

    auto const baseFee = Transactor::calculateBaseFee(view, tx);
    if (baseFee > maxAmount - createFee)
    {
        JLOG(debugLog().trace()) << "ContractCreate: Total fee overflow detected.";
        return XRPAmount{kInitialXrp};
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

    if (std::popcount(flags & (tfCodeImmutable | tfABIImmutable | tfImmutable)) > 1)
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

    if (ctx.tx.isFieldPresent(sfContractCode) &&
        ctx.tx.getFieldVL(sfContractCode).size() > kMaxBytecodeSizeLimit)
    {
        JLOG(ctx.j.trace()) << "ContractCreate: ContractCode over the protocol size limit";
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
    // Installing reuses an existing ContractSource, named by ContractHash or by the code's hash.
    uint256 const contractHash = sourceHash(ctx.tx);
    bool const isInstall = ctx.view.exists(keylet::contractSource(contractHash));
    if (ctx.tx.isFieldPresent(sfContractHash) && !isInstall)
    {
        JLOG(ctx.j.trace()) << "ContractCreate: ContractHash provided but no "
                               "corresponding ContractSource exists";
        return temMALFORMED;
    }

    if (ctx.tx.isFieldPresent(sfContractCode))
    {
        if (auto const ter = checkContractCode(ctx.registry.get().getFees(), ctx.tx, ctx.j);
            !isTesSuccess(ter))
            return ter;
    }

    if (isInstall)
    {
        if (auto const ter = checkInstallFunctions(ctx.view, ctx.tx, contractHash);
            !isTesSuccess(ter))
        {
            JLOG(ctx.j.trace()) << "ContractCreate: Functions differ from the installed ABI.";
            return ter;
        }
    }

    // InstanceParameters don't match what's in the existing ContractSource
    // ledger object.
    if (isInstall && ctx.tx.isFieldPresent(sfInstanceParameterValues))
    {
        auto const sle = ctx.view.read(keylet::contractSource(contractHash));
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

NotTEC
ContractCreate::checkInstallFunctions(
    ReadView const& view,
    STTx const& tx,
    uint256 const& contractHash)
{
    if (!tx.isFieldPresent(sfFunctions))
        return tesSUCCESS;
    auto const source = view.read(keylet::contractSource(contractHash));
    if (!source)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    if (source->getFieldArray(sfFunctions) != tx.getFieldArray(sfFunctions))
        return temMALFORMED;
    return tesSUCCESS;
}

uint256
ContractCreate::sourceHash(STTx const& tx)
{
    if (tx.isFieldPresent(sfContractCode))
        return sha512HalfS(makeSlice(tx.getFieldVL(sfContractCode)));
    return tx.getFieldH256(sfContractHash);
}

TER
ContractCreate::checkContractCode(Fees const& fees, STTx const& tx, beast::Journal j)
{
    xrpl::Blob const wasmBytes = tx.getFieldVL(sfContractCode);
    if (wasmBytes.empty())
    {
        JLOG(j.trace()) << "Contract: ContractCode provided is empty.";
        return temMALFORMED;
    }

    // The fee setup's limits, read and answered as EscrowCreate does; a zero bytecodeSizeLimit
    // or gasLimit deactivates WASM uploads.
    if (fees.bytecodeSizeLimit == 0 || fees.gasLimit == 0)
    {
        JLOG(j.trace()) << "Contract: WASM upload deactivated by the node configuration.";
        return temTEMP_DISABLED;
    }

    if (wasmBytes.size() > fees.bytecodeSizeLimit)
    {
        JLOG(j.trace()) << "Contract: ContractCode over the configured size limit: "
                        << wasmBytes.size();
        return temMALFORMED;
    }

    // Screen the module once per function the ABI names, so each is a callable entry point.
    for (auto const& function : tx.getFieldArray(sfFunctions))
    {
        auto const name = function.getFieldVL(sfFunctionName);
        auto const re = preflightEscrowWasm(
            wasmBytes,
            j,
            std::string_view{reinterpret_cast<char const*>(name.data()), name.size()});
        if (!isTesSuccess(re))
        {
            JLOG(j.trace()) << "Contract: ContractCode bad WASM.";
            return re;
        }
    }

    return tesSUCCESS;
}

TER
ContractCreate::doApply()
{
    auto const accountSle = ctx_.view().peek(keylet::account(accountID_));
    if (!accountSle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    std::shared_ptr<SLE> sourceSle;
    uint256 const contractHash = sourceHash(ctx_.tx);
    bool const isInstall = ctx_.view().exists(keylet::contractSource(contractHash));

    if (isInstall)
    {
        sourceSle = ctx_.view().peek(keylet::contractSource(contractHash));
        if (!sourceSle)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        sourceSle->at(sfReferenceCount) = sourceSle->getFieldU64(sfReferenceCount) + 1;
        ctx_.view().update(sourceSle);
    }
    else
    {
        sourceSle = std::make_shared<SLE>(keylet::contractSource(contractHash));
        sourceSle->at(sfContractHash) = contractHash;
        sourceSle->setFieldVL(sfContractCode, ctx_.tx.getFieldVL(sfContractCode));
        sourceSle->setFieldArray(sfFunctions, ctx_.tx.getFieldArray(sfFunctions));
        if (ctx_.tx.isFieldPresent(sfInstanceParameters))
        {
            sourceSle->setFieldArray(
                sfInstanceParameters, ctx_.tx.getFieldArray(sfInstanceParameters));
        }
        sourceSle->at(sfReferenceCount) = 1;

        ctx_.view().insert(sourceSle);
    }

    std::uint32_t const seq = ctx_.tx.getSeqProxy().value();
    auto const contractKeylet = keylet::contract(contractHash, accountID_, seq);
    auto const contractSle = std::make_shared<SLE>(contractKeylet);

    auto maybePseudo = createPseudoAccount(view(), contractSle->key(), sfContractID);
    if (!maybePseudo)
        return maybePseudo.error();  // LCOV_EXCL_LINE

    auto const& pseudoSle = *maybePseudo;
    AccountID const pseudoAccount = pseudoSle->at(sfAccount);

    contractSle->at(sfContractAccount) = pseudoAccount;
    contractSle->at(sfOwner) = accountID_;
    contractSle->at(sfFlags) = ctx_.tx.getFlags();
    contractSle->at(sfSequence) = seq;
    contractSle->at(sfContractHash) = contractHash;
    if (ctx_.tx.isFieldPresent(sfInstanceParameterValues))
    {
        contractSle->setFieldArray(
            sfInstanceParameterValues, ctx_.tx.getFieldArray(sfInstanceParameterValues));
    }

    if (ctx_.tx.isFieldPresent(sfURI))
        contractSle->setFieldVL(sfURI, ctx_.tx.getFieldVL(sfURI));

    ctx_.view().insert(contractSle);

    // Handle the instance parameters for the contract creation.
    if (ctx_.tx.isFieldPresent(sfInstanceParameterValues))
    {
        STArray const& params = ctx_.tx.getFieldArray(sfInstanceParameterValues);

        // Note: We have to do preclaim and apply here because we will only have
        // the pseudo account after the contract is created.
        if (auto ter = contract::preclaimFlagParameters(
                ctx_.view(), accountID_, pseudoAccount, params, XRPAmount{0}, j_);
            !isTesSuccess(ter))
        {
            JLOG(j_.trace()) << "ContractCreate: Failed to preclaim flag parameters.";
            return ter;
        }

        if (auto ter = contract::doApplyFlagParameters(
                ctx_.view(), ctx_.tx, accountID_, pseudoAccount, params, preFeeBalance_, j_);
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
        increaseOwnerCount(ctx_.view(), pseudoSle, {}, 1, j_);
    }

    return tesSUCCESS;
}

void
ContractCreate::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
ContractCreate::finalizeInvariants(
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
