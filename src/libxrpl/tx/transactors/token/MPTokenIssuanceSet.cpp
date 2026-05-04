#include <xrpl/tx/transactors/token/MPTokenIssuanceSet.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/DelegateHelpers.h>
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
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <unordered_set>

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

// Maps set/clear mutable flags in an MPTokenIssuanceSet transaction to the
// corresponding ledger mutable flags that control whether the change is
// allowed.
struct MPTMutabilityFlags
{
    std::uint32_t setFlag{};
    std::uint32_t clearFlag{};
    std::uint32_t mutabilityFlag{};
    std::uint32_t targetFlag{};
    bool isCannotMutate = false;  // if true, cannot mutate by default.
};

static constexpr std::array<MPTMutabilityFlags, 7> kMPT_MUTABILITY_FLAGS = {
    {{.setFlag = tmfMPTSetCanLock,
      .clearFlag = tmfMPTClearCanLock,
      .mutabilityFlag = lsmfMPTCanMutateCanLock,
      .targetFlag = lsfMPTCanLock},
     {.setFlag = tmfMPTSetRequireAuth,
      .clearFlag = tmfMPTClearRequireAuth,
      .mutabilityFlag = lsmfMPTCanMutateRequireAuth,
      .targetFlag = lsfMPTRequireAuth},
     {.setFlag = tmfMPTSetCanEscrow,
      .clearFlag = tmfMPTClearCanEscrow,
      .mutabilityFlag = lsmfMPTCanMutateCanEscrow,
      .targetFlag = lsfMPTCanEscrow},
     {.setFlag = tmfMPTSetCanTrade,
      .clearFlag = tmfMPTClearCanTrade,
      .mutabilityFlag = lsmfMPTCanMutateCanTrade,
      .targetFlag = lsfMPTCanTrade},
     {.setFlag = tmfMPTSetCanTransfer,
      .clearFlag = tmfMPTClearCanTransfer,
      .mutabilityFlag = lsmfMPTCanMutateCanTransfer,
      .targetFlag = lsfMPTCanTransfer},
     {.setFlag = tmfMPTSetCanClawback,
      .clearFlag = tmfMPTClearCanClawback,
      .mutabilityFlag = lsmfMPTCanMutateCanClawback,
      .targetFlag = lsfMPTCanClawback},
     {.setFlag = tmfMPTSetCanConfidentialAmount,
      .clearFlag = tmfMPTClearCanConfidentialAmount,
      .mutabilityFlag = lsmfMPTCannotMutateCanConfidentialAmount,
      .targetFlag = lsfMPTCanConfidentialAmount,
      .isCannotMutate = true}}};

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

    auto const mutatePrivacy = mutableFlags &&
        (((*mutableFlags & (tmfMPTSetCanConfidentialAmount | tmfMPTClearCanConfidentialAmount))) !=
         0u);

    auto const hasDomain = ctx.tx.isFieldPresent(sfDomainID);
    auto const hasHolder = ctx.tx.isFieldPresent(sfHolder);

    if (isMutate && !ctx.rules.enabled(featureDynamicMPT))
        return temDISABLED;

    if ((hasIssuerElGamalKey || hasAuditorElGamalKey || mutatePrivacy) &&
        !ctx.rules.enabled(featureConfidentialTransfer))
        return temDISABLED;

    if (hasDomain && hasHolder)
        return temMALFORMED;

    if (mutatePrivacy && hasHolder)
        return temMALFORMED;

    // fails if both flags are set
    if (((txFlags & tfMPTLock) != 0u) && ((txFlags & tfMPTUnlock) != 0u))
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
        if (isMutate && ((txFlags & tfUniversalMask) != 0u))
            return temMALFORMED;

        if (transferFee && *transferFee > kMAX_TRANSFER_FEE)
            return temBAD_TRANSFER_FEE;

        if (metadata && metadata->length() > kMAX_MP_TOKEN_METADATA_LENGTH)
            return temMALFORMED;

        if (mutableFlags)
        {
            if ((*mutableFlags == 0u) || ((*mutableFlags & tmfMPTokenIssuanceSetMutableMask) != 0u))
                return temINVALID_FLAG;

            // Can not set and clear the same flag
            if (std::ranges::any_of(kMPT_MUTABILITY_FLAGS, [mutableFlags](auto const& f) {
                    return (*mutableFlags & f.setFlag) && (*mutableFlags & f.clearFlag);
                }))
                return temINVALID_FLAG;

            // Trying to set a non-zero TransferFee and clear MPTCanTransfer
            // in the same transaction is not allowed.
            if ((transferFee.value_or(0) != 0u) && ((*mutableFlags & tmfMPTClearCanTransfer) != 0u))
                return temMALFORMED;
        }
    }

    if (hasHolder && (hasIssuerElGamalKey || hasAuditorElGamalKey))
        return temMALFORMED;

    if (hasAuditorElGamalKey && !hasIssuerElGamalKey)
        return temMALFORMED;

    // Cannot set keys while clearing confidential amount
    if ((hasIssuerElGamalKey || hasAuditorElGamalKey) && mutableFlags &&
        ((*mutableFlags & tmfMPTClearCanConfidentialAmount) != 0u))
        return temINVALID_FLAG;

    if (hasIssuerElGamalKey && !isValidCompressedECPoint(ctx.tx[sfIssuerEncryptionKey]))
        return temMALFORMED;

    if (hasAuditorElGamalKey && !isValidCompressedECPoint(ctx.tx[sfAuditorEncryptionKey]))
        return temMALFORMED;

    return tesSUCCESS;
}

NotTEC
MPTokenIssuanceSet::checkPermission(ReadView const& view, STTx const& tx)
{
    auto const delegate = tx[~sfDelegate];
    if (!delegate)
        return tesSUCCESS;

    auto const delegateKey = keylet::delegate(tx[sfAccount], *delegate);
    auto const sle = view.read(delegateKey);

    if (!sle)
        return terNO_DELEGATE_PERMISSION;

    if (isTesSuccess(checkTxPermission(sle, tx)))
        return tesSUCCESS;

    auto const txFlags = tx.getFlags();

    // this is added in case more flags will be added for MPTokenIssuanceSet
    // in the future. Currently unreachable.
    if ((txFlags & tfMPTokenIssuanceSetMask) != 0u)
        return terNO_DELEGATE_PERMISSION;  // LCOV_EXCL_LINE

    std::unordered_set<GranularPermissionType> granularPermissions;
    loadGranularPermission(sle, ttMPTOKEN_ISSUANCE_SET, granularPermissions);

    if (((txFlags & tfMPTLock) != 0u) && !granularPermissions.contains(MPTokenIssuanceLock))
        return terNO_DELEGATE_PERMISSION;

    if (((txFlags & tfMPTUnlock) != 0u) && !granularPermissions.contains(MPTokenIssuanceUnlock))
        return terNO_DELEGATE_PERMISSION;

    return tesSUCCESS;
}

TER
MPTokenIssuanceSet::preclaim(PreclaimContext const& ctx)
{
    // ensure that issuance exists
    auto const sleMptIssuance = ctx.view.read(keylet::mptIssuance(ctx.tx[sfMPTokenIssuanceID]));
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

        if (*domain != beast::kZERO)
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
    if (mutableFlags)
    {
        if (std::ranges::any_of(kMPT_MUTABILITY_FLAGS, [mutableFlags, &isMutableFlag](auto const& f) {
                bool const canMutate = f.isCannotMutate ? isMutableFlag(f.mutabilityFlag)
                                                        : !isMutableFlag(f.mutabilityFlag);
                return canMutate && (*mutableFlags & (f.setFlag | f.clearFlag));
            }))
            return tecNO_PERMISSION;

        // Clearing lsfMPTRequireAuth is invalid when the issuance already has
        // a DomainID set, because a DomainID requires RequireAuth to be active.
        if ((*mutableFlags & tmfMPTClearRequireAuth) != 0u &&
            sleMptIssuance->isFieldPresent(sfDomainID))
            return tecNO_PERMISSION;

        if (((*mutableFlags & tmfMPTSetCanConfidentialAmount) != 0u) ||
            ((*mutableFlags & tmfMPTClearCanConfidentialAmount) != 0u))
        {
            std::uint64_t const confidentialOA =
                (*sleMptIssuance)[~sfConfidentialOutstandingAmount].value_or(0);

            // If there's any confidential outstanding amount, disallow toggling
            // the lsfMPTCanConfidentialAmount flag
            if (confidentialOA > 0)
                return tecNO_PERMISSION;
        }
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

    // Check if the transaction is enabling confidential amounts
    bool const enablesConfidentialAmount =
        mutableFlags && ((*mutableFlags & tmfMPTSetCanConfidentialAmount) != 0u);

    // Encryption keys can only be set if confidential amounts are already
    // enabled on the issuance OR if the transaction is enabling it
    if (ctx.tx.isFieldPresent(sfIssuerEncryptionKey) &&
        !sleMptIssuance->isFlag(lsfMPTCanConfidentialAmount) && !enablesConfidentialAmount)
    {
        return tecNO_PERMISSION;
    }

    if (ctx.tx.isFieldPresent(sfAuditorEncryptionKey) &&
        !sleMptIssuance->isFlag(lsfMPTCanConfidentialAmount) && !enablesConfidentialAmount)
    {
        return tecNO_PERMISSION;
    }

    // cannot upload key if there's circulating supply of COA
    if ((ctx.tx.isFieldPresent(sfIssuerEncryptionKey) ||
         ctx.tx.isFieldPresent(sfAuditorEncryptionKey)) &&
        sleMptIssuance->isFieldPresent(sfConfidentialOutstandingAmount))
    {
        return tecNO_PERMISSION;  // LCOV_EXCL_LINE
    }

    return tesSUCCESS;
}

TER
MPTokenIssuanceSet::doApply()
{
    auto const mptIssuanceID = ctx_.tx[sfMPTokenIssuanceID];
    auto const txFlags = ctx_.tx.getFlags();
    auto const holderID = ctx_.tx[~sfHolder];
    auto const domainID = ctx_.tx[~sfDomainID];
    std::shared_ptr<SLE> sle;

    if (holderID)
    {
        sle = view().peek(keylet::mptoken(mptIssuanceID, *holderID));
    }
    else
    {
        sle = view().peek(keylet::mptIssuance(mptIssuanceID));
    }

    if (!sle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    std::uint32_t const flagsIn = sle->getFieldU32(sfFlags);
    std::uint32_t flagsOut = flagsIn;

    if ((txFlags & tfMPTLock) != 0u)
    {
        flagsOut |= lsfMPTLocked;
    }
    else if ((txFlags & tfMPTUnlock) != 0u)
    {
        flagsOut &= ~lsfMPTLocked;
    }

    if (auto const mutableFlags = ctx_.tx[~sfMutableFlags].value_or(0))
    {
        for (auto const& f : kMPT_MUTABILITY_FLAGS)
        {
            if ((mutableFlags & f.setFlag) != 0u)
            {
                flagsOut |= f.targetFlag;
            }
            else if ((mutableFlags & f.clearFlag) != 0u)
            {
                flagsOut &= ~f.targetFlag;
            }
        }

        if ((mutableFlags & tmfMPTClearCanTransfer) != 0u)
        {
            // If the lsfMPTCanTransfer flag is being cleared, then also clear
            // the TransferFee field.
            sle->makeFieldAbsent(sfTransferFee);
        }
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

        if (*domainID != beast::kZERO)
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
MPTokenIssuanceSet::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

bool
MPTokenIssuanceSet::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
