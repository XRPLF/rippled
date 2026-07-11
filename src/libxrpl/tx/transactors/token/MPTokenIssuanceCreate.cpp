#include <xrpl/tx/transactors/token/MPTokenIssuanceCreate.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/SLEWrappers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <expected>
#include <memory>

namespace xrpl {

bool
MPTokenIssuanceCreate::checkExtraFeatures(PreflightContext const& ctx)
{
    if (ctx.tx.isFieldPresent(sfDomainID) &&
        !(ctx.rules.enabled(featurePermissionedDomains) &&
          ctx.rules.enabled(featureSingleAssetVault)))
        return false;

    if (ctx.tx.isFieldPresent(sfMutableFlags) && !ctx.rules.enabled(featureDynamicMPT))
        return false;

    if (ctx.tx.isFlag(tfMPTCanHoldConfidentialBalance) &&
        !ctx.rules.enabled(featureConfidentialTransfer))
        return false;

    // can not set tmfMPTCannotEnableCanHoldConfidentialBalance without featureConfidentialTransfer
    auto const mutableFlags = ctx.tx[~sfMutableFlags];
    return !mutableFlags ||
        ((*mutableFlags & tmfMPTCannotEnableCanHoldConfidentialBalance) == 0u) ||
        ctx.rules.enabled(featureConfidentialTransfer);
}

std::uint32_t
MPTokenIssuanceCreate::getFlagsMask(PreflightContext const& ctx)
{
    // This mask is only compared against sfFlags
    return tfMPTokenIssuanceCreateMask;
}

NotTEC
MPTokenIssuanceCreate::preflight(PreflightContext const& ctx)
{
    // sfReferenceHolding is set only internally by VaultCreate. Reject
    // any user-submitted MPTokenIssuanceCreate that attempts to carry it.
    if (ctx.rules.enabled(fixCleanup3_2_0) && ctx.tx.isFieldPresent(sfReferenceHolding))
        return temMALFORMED;

    // If the mutable flags field is included, at least one flag must be
    // specified.
    if (auto const mutableFlags = ctx.tx[~sfMutableFlags]; mutableFlags &&
        ((*mutableFlags == 0u) || ((*mutableFlags & tmfMPTokenIssuanceCreateMutableMask) != 0u)))
        return temINVALID_FLAG;

    if (auto const fee = ctx.tx[~sfTransferFee])
    {
        if (fee > kMaxTransferFee)
            return temBAD_TRANSFER_FEE;

        // If a non-zero TransferFee is set then the tfTransferable flag
        // must also be set.
        if (fee > 0u && !ctx.tx.isFlag(tfMPTCanTransfer))
            return temMALFORMED;

        // Confidential amounts are encrypted so transfer rate is disallowed.
        if (fee > 0u && ctx.tx.isFlag(tfMPTCanHoldConfidentialBalance))
            return temBAD_TRANSFER_FEE;
    }

    if (auto const domain = ctx.tx[~sfDomainID])
    {
        if (*domain == beast::kZero)
            return temMALFORMED;

        // Domain present implies that MPTokenIssuance is not public
        if (!ctx.tx.isFlag(tfMPTRequireAuth))
            return temMALFORMED;
    }

    if (auto const metadata = ctx.tx[~sfMPTokenMetadata])
    {
        if (metadata->empty() || metadata->length() > kMaxMpTokenMetadataLength)
            return temMALFORMED;
    }

    // Check if maximumAmount is within unsigned 63 bit range
    if (auto const maxAmt = ctx.tx[~sfMaximumAmount])
    {
        if (maxAmt == 0)
            return temMALFORMED;

        if (maxAmt > kMaxMpTokenAmount)
            return temMALFORMED;
    }
    return tesSUCCESS;
}

std::expected<MPTID, TER>
MPTokenIssuanceCreate::create(
    ApplyViewContext ctx,
    beast::Journal journal,
    MPTCreateArgs const& args)
{
    if (!ctx.view.exists(keylet::account(args.account)))
        return std::unexpected(tecINTERNAL);  // LCOV_EXCL_LINE

    auto const mptId = makeMptID(args.sequence, args.account);
    auto const mptIssuanceKeylet = keylet::mptokenIssuance(mptId);

    // create the MPTokenIssuance. Build with the ApplyViewContext so create()
    // performs reserve-sponsorship-aware accounting (a no-op when the issuer is
    // a pseudo-account, e.g. VaultCreate).
    MPTokenIssuanceEntry<ApplyView> mptIssuance{mptIssuanceKeylet, ctx, journal};
    mptIssuance.newSLE();
    (*mptIssuance)[sfFlags] = args.flags & ~tfUniversal;
    (*mptIssuance)[sfIssuer] = args.account;
    (*mptIssuance)[sfOutstandingAmount] = 0;
    (*mptIssuance)[sfSequence] = args.sequence;

    if (args.maxAmount)
        (*mptIssuance)[sfMaximumAmount] = *args.maxAmount;

    if (args.assetScale)
        (*mptIssuance)[sfAssetScale] = *args.assetScale;

    if (args.transferFee)
        (*mptIssuance)[sfTransferFee] = *args.transferFee;

    if (args.metadata)
        (*mptIssuance)[sfMPTokenMetadata] = *args.metadata;

    if (args.domainId)
        (*mptIssuance)[sfDomainID] = *args.domainId;

    if (args.mutableFlags)
        (*mptIssuance)[sfMutableFlags] = *args.mutableFlags;

    if (args.referenceHolding)
    {
        // Defensive: the holding must already exist and be of an
        // expected type. Callers (currently only VaultCreate)
        // populate this after the pseudo-account's MPToken /
        // RippleState has been installed. A missing holding here
        // would dangle the pointer and is a programmer error.
        auto const sleHolding = ctx.view.read(keylet::unchecked(*args.referenceHolding));
        if (!sleHolding)
            return std::unexpected(tecINTERNAL);  // LCOV_EXCL_LINE
        auto const type = sleHolding->getType();
        if (type != ltMPTOKEN && type != ltRIPPLE_STATE)
            return std::unexpected(tecINTERNAL);  // LCOV_EXCL_LINE
        (*mptIssuance)[sfReferenceHolding] = *args.referenceHolding;
    }

    // Reserve check against the issuer's pre-fee balance (skipped when
    // priorBalance is std::nullopt, i.e. VaultCreate's pseudo-account, and
    // honoring any reserve sponsor) + link into the issuer's owner directory +
    // bump the issuer's OwnerCount + stamp the reserve sponsor + insert. See
    // MPTokenIssuanceEntry and SLEBase::create().
    if (auto const ter = mptIssuance.create(args.priorBalance); !isTesSuccess(ter))
        return std::unexpected(ter);

    return mptId;
}

TER
MPTokenIssuanceCreate::doApply()
{
    auto const& tx = ctx_.tx;
    auto const result = create(
        ctx_.getApplyViewContext(),
        j_,
        {
            .priorBalance = preFeeBalance_,
            .account = accountID_,
            .sequence = tx.getSeqValue(),
            .flags = tx.getFlags(),
            .maxAmount = tx[~sfMaximumAmount],
            .assetScale = tx[~sfAssetScale],
            .transferFee = tx[~sfTransferFee],
            .metadata = tx[~sfMPTokenMetadata],
            .domainId = tx[~sfDomainID],
            .mutableFlags = tx[~sfMutableFlags],
        });
    return result ? tesSUCCESS : result.error();
}

void
MPTokenIssuanceCreate::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
MPTokenIssuanceCreate::finalizeInvariants(
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
