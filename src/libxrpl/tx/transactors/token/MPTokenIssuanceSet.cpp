#include <xrpl/tx/transactors/token/MPTokenIssuanceSet.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace xrpl {

bool
MPTokenIssuanceSet::checkExtraFeatures(PreflightContext const& ctx)
{
    return !ctx.tx.isFieldPresent(sfDomainID) ||
        (ctx.rules.enabled(featurePermissionedDomains) &&
         ctx.rules.enabled(featureSingleAssetVault));
}

std::uint32_t
MPTokenIssuanceSet::getFlagsMask(PreflightContext const& ctx)
{
    return tfMPTokenIssuanceSetMask;
}

// Maps each MPTokenIssuanceSet MutableFlags to the corresponding mutable
// flag and the target ledger flag to mutate.
struct MPTMutabilityFlags
{
    std::uint32_t setFlag;
    std::uint32_t canEnableFlag;
    std::uint32_t ledgerFlag;
};

static constexpr std::array<MPTMutabilityFlags, 6> kMptMutabilityFlags = {
    {{.setFlag = tmfMPTSetCanLock,
      .canEnableFlag = lsmfMPTCanEnableCanLock,
      .ledgerFlag = lsfMPTCanLock},
     {.setFlag = tmfMPTSetRequireAuth,
      .canEnableFlag = lsmfMPTCanEnableRequireAuth,
      .ledgerFlag = lsfMPTRequireAuth},
     {.setFlag = tmfMPTSetCanEscrow,
      .canEnableFlag = lsmfMPTCanEnableCanEscrow,
      .ledgerFlag = lsfMPTCanEscrow},
     {.setFlag = tmfMPTSetCanTrade,
      .canEnableFlag = lsmfMPTCanEnableCanTrade,
      .ledgerFlag = lsfMPTCanTrade},
     {.setFlag = tmfMPTSetCanTransfer,
      .canEnableFlag = lsmfMPTCanEnableCanTransfer,
      .ledgerFlag = lsfMPTCanTransfer},
     {.setFlag = tmfMPTSetCanClawback,
      .canEnableFlag = lsmfMPTCanEnableCanClawback,
      .ledgerFlag = lsfMPTCanClawback}}};

NotTEC
MPTokenIssuanceSet::preflight(PreflightContext const& ctx)
{
    auto const mutableFlags = ctx.tx[~sfMutableFlags];
    auto const metadata = ctx.tx[~sfMPTokenMetadata];
    auto const transferFee = ctx.tx[~sfTransferFee];
    auto const isMutate = mutableFlags || metadata || transferFee;
    auto const hasIssuerElGamalKey = ctx.tx.isFieldPresent(sfIssuerEncryptionKey);
    auto const hasAuditorElGamalKey = ctx.tx.isFieldPresent(sfAuditorEncryptionKey);
    auto const txFlags = ctx.tx.getFlags();

    bool const enablePrivacy =
        mutableFlags && (*mutableFlags & tmfMPTSetCanHoldConfidentialBalance) != 0u;

    auto const hasDomain = ctx.tx.isFieldPresent(sfDomainID);
    auto const hasHolder = ctx.tx.isFieldPresent(sfHolder);

    if (isMutate && !ctx.rules.enabled(featureDynamicMPT))
        return temDISABLED;

    if ((hasIssuerElGamalKey || hasAuditorElGamalKey || enablePrivacy) &&
        !ctx.rules.enabled(featureConfidentialTransfer))
        return temDISABLED;

    if (hasDomain && hasHolder)
        return temMALFORMED;

    if (enablePrivacy && hasHolder)
        return temMALFORMED;

    // fails if both flags are set
    if (ctx.tx.isFlag(tfMPTLock) && ctx.tx.isFlag(tfMPTUnlock))
        return temINVALID_FLAG;

    auto const accountID = ctx.tx[sfAccount];
    auto const holderID = ctx.tx[~sfHolder];
    if (holderID && accountID == holderID)
        return temMALFORMED;

    if (ctx.rules.enabled(featureSingleAssetVault) || ctx.rules.enabled(featureDynamicMPT) ||
        ctx.rules.enabled(featureConfidentialTransfer))
    {
        // Is this transaction actually changing anything ?
        if (txFlags == 0 && !hasDomain && !hasIssuerElGamalKey && !hasAuditorElGamalKey &&
            !isMutate)
            return temMALFORMED;
    }

    if (ctx.rules.enabled(featureDynamicMPT))
    {
        // Holder field is not allowed when mutating MPTokenIssuance
        if (isMutate && holderID)
            return temMALFORMED;

        // Can not set flags when mutating MPTokenIssuance
        if (isMutate && ((ctx.tx.getFlags() & tfUniversalMask) != 0u))
            return temMALFORMED;

        if (transferFee && *transferFee > kMaxTransferFee)
            return temBAD_TRANSFER_FEE;

        if (transferFee && *transferFee > 0u && enablePrivacy)
            return temBAD_TRANSFER_FEE;

        if (metadata && metadata->length() > kMaxMpTokenMetadataLength)
            return temMALFORMED;

        if (mutableFlags)
        {
            if ((*mutableFlags == 0u) || ((*mutableFlags & tmfMPTokenIssuanceSetMutableMask) != 0u))
                return temINVALID_FLAG;
        }
    }

    if (hasHolder && (hasIssuerElGamalKey || hasAuditorElGamalKey))
        return temMALFORMED;

    if (hasAuditorElGamalKey && !hasIssuerElGamalKey)
        return temMALFORMED;

    if (hasIssuerElGamalKey && !isValidCompressedECPoint(ctx.tx[sfIssuerEncryptionKey]))
        return temMALFORMED;

    if (hasAuditorElGamalKey && !isValidCompressedECPoint(ctx.tx[sfAuditorEncryptionKey]))
        return temMALFORMED;

    return tesSUCCESS;
}

TER
MPTokenIssuanceSet::preclaim(PreclaimContext const& ctx)
{
    // ensure that issuance exists
    auto const sleMptIssuance = ctx.view.read(keylet::mptokenIssuance(ctx.tx[sfMPTokenIssuanceID]));
    if (!sleMptIssuance)
        return tecOBJECT_NOT_FOUND;

    if (!sleMptIssuance->isFlag(lsfMPTCanLock))
    {
        // For readability two separate `if` rather than `||` of two conditions
        if (!ctx.view.rules().enabled(featureSingleAssetVault) &&
            !ctx.view.rules().enabled(featureDynamicMPT))
        {
            return tecNO_PERMISSION;
        }
        if (ctx.tx.isFlag(tfMPTLock) || ctx.tx.isFlag(tfMPTUnlock))
        {
            return tecNO_PERMISSION;
        }
    }

    // ensure it is issued by the tx submitter
    if ((*sleMptIssuance)[sfIssuer] != ctx.tx[sfAccount])
        return tecNO_PERMISSION;

    if (auto const holderID = ctx.tx[~sfHolder])
    {
        // make sure holder account exists
        if (!ctx.view.exists(keylet::account(*holderID)))
            return tecNO_DST;

        // the mptoken must exist
        if (!ctx.view.exists(keylet::mptoken(ctx.tx[sfMPTokenIssuanceID], *holderID)))
            return tecOBJECT_NOT_FOUND;
    }

    if (auto const domain = ctx.tx[~sfDomainID])
    {
        if (not sleMptIssuance->isFlag(lsfMPTRequireAuth))
            return tecNO_PERMISSION;

        if (*domain != beast::kZero)
        {
            auto const sleDomain = ctx.view.read(keylet::permissionedDomain(*domain));
            if (!sleDomain)
                return tecOBJECT_NOT_FOUND;
        }
    }

    // sfMutableFlags is soeDEFAULT, defaulting to 0 if not specified on
    // the ledger.
    auto const currentMutableFlags = sleMptIssuance->getFieldU32(sfMutableFlags);

    auto isMutableFlag = [&](std::uint32_t mutableFlag) -> bool {
        return currentMutableFlags & mutableFlag;
    };

    auto const mutableFlags = ctx.tx[~sfMutableFlags];
    // Whether the transaction is enabling confidential amounts.
    bool const enablesConfidentialAmount =
        mutableFlags && (*mutableFlags & tmfMPTSetCanHoldConfidentialBalance) != 0u;
    if (mutableFlags)
    {
        if (std::ranges::any_of(kMptMutabilityFlags, [mutableFlags, &isMutableFlag](auto const& f) {
                return !isMutableFlag(f.canEnableFlag) && ((*mutableFlags & f.setFlag) != 0u);
            }))
            return tecNO_PERMISSION;

        if (enablesConfidentialAmount &&
            isMutableFlag(lsmfMPTCannotEnableCanHoldConfidentialBalance))
            return tecNO_PERMISSION;
    }

    if (!isMutableFlag(lsmfMPTCanMutateMetadata) && ctx.tx.isFieldPresent(sfMPTokenMetadata))
        return tecNO_PERMISSION;

    if (auto const fee = ctx.tx[~sfTransferFee])
    {
        // A non-zero TransferFee is only valid if the lsfMPTCanTransfer flag
        // was previously enabled (at issuance or via a prior mutation). Setting
        // it by tmfMPTSetCanTransfer in the current transaction does not meet
        // this requirement.
        if (fee > 0u && !sleMptIssuance->isFlag(lsfMPTCanTransfer))
            return tecNO_PERMISSION;

        // Cannot set a non-zero TransferFee on an issuance that has confidential
        // transfer enabled
        if (fee > 0u && sleMptIssuance->isFlag(lsfMPTCanHoldConfidentialBalance))
            return tecNO_PERMISSION;

        if (!isMutableFlag(lsmfMPTCanMutateTransferFee))
            return tecNO_PERMISSION;
    }

    // cannot update issuer public key
    if (ctx.tx.isFieldPresent(sfIssuerEncryptionKey) &&
        sleMptIssuance->isFieldPresent(sfIssuerEncryptionKey))
    {
        return tecNO_PERMISSION;
    }

    // cannot update auditor public key
    if (ctx.tx.isFieldPresent(sfAuditorEncryptionKey) &&
        sleMptIssuance->isFieldPresent(sfAuditorEncryptionKey))
    {
        return tecNO_PERMISSION;  // LCOV_EXCL_LINE
    }

    if (enablesConfidentialAmount && sleMptIssuance->isFieldPresent(sfTransferFee) &&
        (*sleMptIssuance)[sfTransferFee] > 0u)
        return tecNO_PERMISSION;

    // Encryption keys can only be set if confidential amounts are already
    // enabled on the issuance OR if the transaction is enabling it
    if (ctx.tx.isFieldPresent(sfIssuerEncryptionKey) &&
        !sleMptIssuance->isFlag(lsfMPTCanHoldConfidentialBalance) && !enablesConfidentialAmount)
    {
        return tecNO_PERMISSION;
    }

    if (ctx.tx.isFieldPresent(sfAuditorEncryptionKey) &&
        !sleMptIssuance->isFlag(lsfMPTCanHoldConfidentialBalance) && !enablesConfidentialAmount)
    {
        return tecNO_PERMISSION;
    }

    // cannot upload key if there's circulating supply of COA
    if ((ctx.tx.isFieldPresent(sfIssuerEncryptionKey) ||
         ctx.tx.isFieldPresent(sfAuditorEncryptionKey) || enablesConfidentialAmount) &&
        (*sleMptIssuance)[~sfConfidentialOutstandingAmount].value_or(0) > 0)
    {
        return tecNO_PERMISSION;  // LCOV_EXCL_LINE
    }

    return tesSUCCESS;
}

TER
MPTokenIssuanceSet::doApply()
{
    auto const mptIssuanceID = ctx_.tx[sfMPTokenIssuanceID];
    auto const holderID = ctx_.tx[~sfHolder];
    auto const domainID = ctx_.tx[~sfDomainID];
    SLE::pointer sle;

    if (holderID)
    {
        sle = view().peek(keylet::mptoken(mptIssuanceID, *holderID));
    }
    else
    {
        sle = view().peek(keylet::mptokenIssuance(mptIssuanceID));
    }

    if (!sle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    std::uint32_t const flagsIn = sle->getFieldU32(sfFlags);
    std::uint32_t flagsOut = flagsIn;

    if (ctx_.tx.isFlag(tfMPTLock))
    {
        flagsOut |= lsfMPTLocked;
    }
    else if (ctx_.tx.isFlag(tfMPTUnlock))
    {
        flagsOut &= ~lsfMPTLocked;
    }

    if (auto const mutableFlags = ctx_.tx[~sfMutableFlags].value_or(0))
    {
        for (auto const& f : kMptMutabilityFlags)
        {
            if ((mutableFlags & f.setFlag) != 0u)
            {
                flagsOut |= f.ledgerFlag;
            }
        }

        if ((mutableFlags & tmfMPTSetCanHoldConfidentialBalance) != 0u)
            flagsOut |= lsfMPTCanHoldConfidentialBalance;
    }

    if (flagsIn != flagsOut)
        sle->setFieldU32(sfFlags, flagsOut);

    if (auto const transferFee = ctx_.tx[~sfTransferFee])
    {
        // TransferFee uses soeDEFAULT style:
        // - If the field is absent, it is interpreted as 0.
        // - If the field is present, it must be non-zero.
        // Therefore, when TransferFee is 0, the field should be removed.
        if (transferFee == 0)
        {
            sle->makeFieldAbsent(sfTransferFee);
        }
        else
        {
            sle->setFieldU16(sfTransferFee, *transferFee);
        }
    }

    if (auto const metadata = ctx_.tx[~sfMPTokenMetadata])
    {
        if (metadata->empty())
        {
            sle->makeFieldAbsent(sfMPTokenMetadata);
        }
        else
        {
            sle->setFieldVL(sfMPTokenMetadata, *metadata);
        }
    }

    if (domainID)
    {
        // This is enforced in preflight.
        XRPL_ASSERT(
            sle->getType() == ltMPTOKEN_ISSUANCE,
            "MPTokenIssuanceSet::doApply : modifying MPTokenIssuance");

        if (*domainID != beast::kZero)
        {
            sle->setFieldH256(sfDomainID, *domainID);
        }
        else
        {
            if (sle->isFieldPresent(sfDomainID))
                sle->makeFieldAbsent(sfDomainID);
        }
    }

    if (auto const pubKey = ctx_.tx[~sfIssuerEncryptionKey])
    {
        // This is enforced in preflight.
        XRPL_ASSERT(
            sle->getType() == ltMPTOKEN_ISSUANCE,
            "MPTokenIssuanceSet::doApply : modifying MPTokenIssuance");

        sle->setFieldVL(sfIssuerEncryptionKey, *pubKey);
    }

    if (auto const pubKey = ctx_.tx[~sfAuditorEncryptionKey])
    {
        // This is enforced in preflight.
        XRPL_ASSERT(
            sle->getType() == ltMPTOKEN_ISSUANCE,
            "MPTokenIssuanceSet::doApply : modifying MPTokenIssuance");

        sle->setFieldVL(sfAuditorEncryptionKey, *pubKey);
    }

    view().update(sle);

    return tesSUCCESS;
}

void
MPTokenIssuanceSet::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
MPTokenIssuanceSet::finalizeInvariants(
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
