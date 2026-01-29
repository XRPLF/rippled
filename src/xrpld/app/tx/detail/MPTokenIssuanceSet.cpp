#include <xrpld/app/misc/DelegateUtils.h>
#include <xrpld/app/tx/detail/MPTokenIssuanceSet.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TxFlags.h>

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
    std::uint32_t setFlag;
    std::uint32_t clearFlag;
    std::uint32_t mutabilityFlag;
    std::uint32_t targetFlag;
    bool isCannotMutate = false;  // if true, cannot mutate by default.
};

static constexpr std::array<MPTMutabilityFlags, 7> mptMutabilityFlags = {
    {{tmfMPTSetCanLock,
      tmfMPTClearCanLock,
      lsmfMPTCanMutateCanLock,
      lsfMPTCanLock},
     {tmfMPTSetRequireAuth,
      tmfMPTClearRequireAuth,
      lsmfMPTCanMutateRequireAuth,
      lsfMPTRequireAuth},
     {tmfMPTSetCanEscrow,
      tmfMPTClearCanEscrow,
      lsmfMPTCanMutateCanEscrow,
      lsfMPTCanEscrow},
     {tmfMPTSetCanTrade,
      tmfMPTClearCanTrade,
      lsmfMPTCanMutateCanTrade,
      lsfMPTCanTrade},
     {tmfMPTSetCanTransfer,
      tmfMPTClearCanTransfer,
      lsmfMPTCanMutateCanTransfer,
      lsfMPTCanTransfer},
     {tmfMPTSetCanClawback,
      tmfMPTClearCanClawback,
      lsmfMPTCanMutateCanClawback,
      lsfMPTCanClawback},
     {tmfMPTSetPrivacy,
      tmfMPTClearPrivacy,
      lsmfMPTCannotMutatePrivacy,
      lsfMPTCanPrivacy,
      true}}};

NotTEC
MPTokenIssuanceSet::preflight(PreflightContext const& ctx)
{
    auto const mutableFlags = ctx.tx[~sfMutableFlags];
    auto const metadata = ctx.tx[~sfMPTokenMetadata];
    auto const transferFee = ctx.tx[~sfTransferFee];
    auto const isMutate = mutableFlags || metadata || transferFee;
    auto const hasIssuerElGamalKey =
        ctx.tx.isFieldPresent(sfIssuerElGamalPublicKey);
    auto const hasAuditorElGamalKey =
        ctx.tx.isFieldPresent(sfAuditorElGamalPublicKey);
    auto const txFlags = ctx.tx.getFlags();

    auto const mutatePrivacy = mutableFlags &&
        ((*mutableFlags & (tmfMPTSetPrivacy | tmfMPTClearPrivacy)));

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
    if ((txFlags & tfMPTLock) && (txFlags & tfMPTUnlock))
        return temINVALID_FLAG;

    auto const accountID = ctx.tx[sfAccount];
    auto const holderID = ctx.tx[~sfHolder];
    if (holderID && accountID == holderID)
        return temMALFORMED;

    if (ctx.rules.enabled(featureSingleAssetVault) ||
        ctx.rules.enabled(featureDynamicMPT) ||
        ctx.rules.enabled(featureConfidentialTransfer))
    {
        // Is this transaction actually changing anything ?
        if (txFlags == 0 && !hasDomain && !hasIssuerElGamalKey &&
            !hasAuditorElGamalKey && !isMutate)
            return temMALFORMED;
    }

    if (ctx.rules.enabled(featureDynamicMPT))
    {
        // Holder field is not allowed when mutating MPTokenIssuance
        if (isMutate && holderID)
            return temMALFORMED;

        // Can not set flags when mutating MPTokenIssuance
        if (isMutate && (txFlags & tfUniversalMask))
            return temMALFORMED;

        if (transferFee && *transferFee > maxTransferFee)
            return temBAD_TRANSFER_FEE;

        if (metadata && metadata->length() > maxMPTokenMetadataLength)
            return temMALFORMED;

        if (mutableFlags)
        {
            if (!*mutableFlags ||
                (*mutableFlags & tmfMPTokenIssuanceSetMutableMask))
                return temINVALID_FLAG;

            // Can not set and clear the same flag
            if (std::any_of(
                    mptMutabilityFlags.begin(),
                    mptMutabilityFlags.end(),
                    [mutableFlags](auto const& f) {
                        return (*mutableFlags & f.setFlag) &&
                            (*mutableFlags & f.clearFlag);
                    }))
                return temINVALID_FLAG;

            // Trying to set a non-zero TransferFee and clear MPTCanTransfer
            // in the same transaction is not allowed.
            if (transferFee.value_or(0) &&
                (*mutableFlags & tmfMPTClearCanTransfer))
                return temMALFORMED;
        }
    }

    if (hasHolder && (hasIssuerElGamalKey || hasAuditorElGamalKey))
        return temMALFORMED;

    if (hasAuditorElGamalKey && !hasIssuerElGamalKey)
        return temMALFORMED;

    if (hasIssuerElGamalKey &&
        ctx.tx[sfIssuerElGamalPublicKey].length() != ecPubKeyLength)
        return temMALFORMED;

    if (hasAuditorElGamalKey &&
        ctx.tx[sfAuditorElGamalPublicKey].length() != ecPubKeyLength)
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

    if (checkTxPermission(sle, tx) == tesSUCCESS)
        return tesSUCCESS;

    auto const txFlags = tx.getFlags();

    // this is added in case more flags will be added for MPTokenIssuanceSet
    // in the future. Currently unreachable.
    if (txFlags & tfMPTokenIssuanceSetPermissionMask)
        return terNO_DELEGATE_PERMISSION;  // LCOV_EXCL_LINE

    std::unordered_set<GranularPermissionType> granularPermissions;
    loadGranularPermission(sle, ttMPTOKEN_ISSUANCE_SET, granularPermissions);

    if (txFlags & tfMPTLock &&
        !granularPermissions.contains(MPTokenIssuanceLock))
        return terNO_DELEGATE_PERMISSION;

    if (txFlags & tfMPTUnlock &&
        !granularPermissions.contains(MPTokenIssuanceUnlock))
        return terNO_DELEGATE_PERMISSION;

    return tesSUCCESS;
}

TER
MPTokenIssuanceSet::preclaim(PreclaimContext const& ctx)
{
    // ensure that issuance exists
    auto const sleMptIssuance =
        ctx.view.read(keylet::mptIssuance(ctx.tx[sfMPTokenIssuanceID]));
    if (!sleMptIssuance)
        return tecOBJECT_NOT_FOUND;

    if (!sleMptIssuance->isFlag(lsfMPTCanLock))
    {
        // For readability two separate `if` rather than `||` of two conditions
        if (!ctx.view.rules().enabled(featureSingleAssetVault) &&
            !ctx.view.rules().enabled(featureDynamicMPT))
            return tecNO_PERMISSION;
        else if (ctx.tx.isFlag(tfMPTLock) || ctx.tx.isFlag(tfMPTUnlock))
            return tecNO_PERMISSION;
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
        if (!ctx.view.exists(
                keylet::mptoken(ctx.tx[sfMPTokenIssuanceID], *holderID)))
            return tecOBJECT_NOT_FOUND;
    }

    if (auto const domain = ctx.tx[~sfDomainID])
    {
        if (not sleMptIssuance->isFlag(lsfMPTRequireAuth))
            return tecNO_PERMISSION;

        if (*domain != beast::zero)
        {
            auto const sleDomain =
                ctx.view.read(keylet::permissionedDomain(*domain));
            if (!sleDomain)
                return tecOBJECT_NOT_FOUND;
        }
    }

    // sfMutableFlags is soeDEFAULT, defaulting to 0 if not specified on
    // the ledger.
    auto const currentMutableFlags =
        sleMptIssuance->getFieldU32(sfMutableFlags);

    auto isMutableFlag = [&](std::uint32_t mutableFlag) -> bool {
        return currentMutableFlags & mutableFlag;
    };

    auto const mutableFlags = ctx.tx[~sfMutableFlags];
    if (mutableFlags)
    {
        if (std::any_of(
                mptMutabilityFlags.begin(),
                mptMutabilityFlags.end(),
                [mutableFlags, &isMutableFlag](auto const& f) {
                    bool const canMutate = f.isCannotMutate
                        ? isMutableFlag(f.mutabilityFlag)
                        : !isMutableFlag(f.mutabilityFlag);
                    return canMutate &&
                        (*mutableFlags & (f.setFlag | f.clearFlag));
                }))
            return tecNO_PERMISSION;

        if ((*mutableFlags & tmfMPTSetPrivacy) ||
            (*mutableFlags & tmfMPTClearPrivacy))
        {
            std::uint64_t const confidentialOA =
                (*sleMptIssuance)[~sfConfidentialOutstandingAmount].value_or(0);

            // If there's any confidential outstanding amount, disallow toggling
            // the lsfMPTCanPrivacy flag
            if (confidentialOA > 0)
                return tecNO_PERMISSION;
        }
    }

    if (!isMutableFlag(lsmfMPTCanMutateMetadata) &&
        ctx.tx.isFieldPresent(sfMPTokenMetadata))
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
    if (ctx.tx.isFieldPresent(sfIssuerElGamalPublicKey) &&
        sleMptIssuance->isFieldPresent(sfIssuerElGamalPublicKey))
    {
        return tecNO_PERMISSION;
    }

    // cannot update auditor public key
    if (ctx.tx.isFieldPresent(sfAuditorElGamalPublicKey) &&
        sleMptIssuance->isFieldPresent(sfAuditorElGamalPublicKey))
    {
        return tecNO_PERMISSION;  // LCOV_EXCL_LINE
    }

    if (ctx.tx.isFieldPresent(sfIssuerElGamalPublicKey) &&
        !sleMptIssuance->isFlag(lsfMPTCanPrivacy))
    {
        return tecNO_PERMISSION;
    }

    if (ctx.tx.isFieldPresent(sfAuditorElGamalPublicKey) &&
        !sleMptIssuance->isFlag(lsfMPTCanPrivacy))
    {
        return tecNO_PERMISSION;
    }

    // cannot upload key if there's circulating supply of COA
    if ((ctx.tx.isFieldPresent(sfIssuerElGamalPublicKey) ||
         ctx.tx.isFieldPresent(sfAuditorElGamalPublicKey)) &&
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
        sle = view().peek(keylet::mptoken(mptIssuanceID, *holderID));
    else
        sle = view().peek(keylet::mptIssuance(mptIssuanceID));

    if (!sle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    std::uint32_t const flagsIn = sle->getFieldU32(sfFlags);
    std::uint32_t flagsOut = flagsIn;

    if (txFlags & tfMPTLock)
        flagsOut |= lsfMPTLocked;
    else if (txFlags & tfMPTUnlock)
        flagsOut &= ~lsfMPTLocked;

    if (auto const mutableFlags = ctx_.tx[~sfMutableFlags].value_or(0))
    {
        for (auto const& f : mptMutabilityFlags)
        {
            if (mutableFlags & f.setFlag)
                flagsOut |= f.targetFlag;
            else if (mutableFlags & f.clearFlag)
                flagsOut &= ~f.targetFlag;
        }

        if (mutableFlags & tmfMPTClearCanTransfer)
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
            sle->makeFieldAbsent(sfTransferFee);
        else
            sle->setFieldU16(sfTransferFee, *transferFee);
    }

    if (auto const metadata = ctx_.tx[~sfMPTokenMetadata])
    {
        if (metadata->empty())
            sle->makeFieldAbsent(sfMPTokenMetadata);
        else
            sle->setFieldVL(sfMPTokenMetadata, *metadata);
    }

    if (domainID)
    {
        // This is enforced in preflight.
        XRPL_ASSERT(
            sle->getType() == ltMPTOKEN_ISSUANCE,
            "MPTokenIssuanceSet::doApply : modifying MPTokenIssuance");

        if (*domainID != beast::zero)
        {
            sle->setFieldH256(sfDomainID, *domainID);
        }
        else
        {
            if (sle->isFieldPresent(sfDomainID))
                sle->makeFieldAbsent(sfDomainID);
        }
    }

    if (auto const pubKey = ctx_.tx[~sfIssuerElGamalPublicKey])
    {
        // This is enforced in preflight.
        XRPL_ASSERT(
            sle->getType() == ltMPTOKEN_ISSUANCE,
            "MPTokenIssuanceSet::doApply : modifying MPTokenIssuance");

        sle->setFieldVL(sfIssuerElGamalPublicKey, *pubKey);
    }

    if (auto const pubKey = ctx_.tx[~sfAuditorElGamalPublicKey])
    {
        // This is enforced in preflight.
        XRPL_ASSERT(
            sle->getType() == ltMPTOKEN_ISSUANCE,
            "MPTokenIssuanceSet::doApply : modifying MPTokenIssuance");

        sle->setFieldVL(sfAuditorElGamalPublicKey, *pubKey);
    }

    view().update(sle);

    return tesSUCCESS;
}

}  // namespace xrpl
