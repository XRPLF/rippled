#include <xrpld/app/misc/DelegateUtils.h>
#include <xrpld/app/tx/detail/MPTokenIssuanceSet.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

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
    std::uint32_t canMutateFlag;
};

static constexpr std::array<MPTMutabilityFlags, 6> mptMutabilityFlags = {
    {{tmfMPTSetCanLock, tmfMPTClearCanLock, lsmfMPTCanMutateCanLock},
     {tmfMPTSetRequireAuth,
      tmfMPTClearRequireAuth,
      lsmfMPTCanMutateRequireAuth},
     {tmfMPTSetCanEscrow, tmfMPTClearCanEscrow, lsmfMPTCanMutateCanEscrow},
     {tmfMPTSetCanTrade, tmfMPTClearCanTrade, lsmfMPTCanMutateCanTrade},
     {tmfMPTSetCanTransfer,
      tmfMPTClearCanTransfer,
      lsmfMPTCanMutateCanTransfer},
     {tmfMPTSetCanClawback,
      tmfMPTClearCanClawback,
      lsmfMPTCanMutateCanClawback}}};

NotTEC
MPTokenIssuanceSet::preflight(PreflightContext const& ctx)
{
    auto const mutableFlags = ctx.tx[~sfMutableFlags];
    auto const metadata = ctx.tx[~sfMPTokenMetadata];
    auto const transferFee = ctx.tx[~sfTransferFee];
    auto const isMutate = mutableFlags || metadata || transferFee;

    if (isMutate && !ctx.rules.enabled(featureDynamicMPT))
        return temDISABLED;

    if (ctx.tx.isFieldPresent(sfDomainID) && ctx.tx.isFieldPresent(sfHolder))
        return temMALFORMED;

    auto const txFlags = ctx.tx.getFlags();

    // fails if both flags are set
    if ((txFlags & tfMPTLock) && (txFlags & tfMPTUnlock))
        return temINVALID_FLAG;

    auto const accountID = ctx.tx[sfAccount];
    auto const holderID = ctx.tx[~sfHolder];
    if (holderID && accountID == holderID)
        return temMALFORMED;

    if (ctx.rules.enabled(featureSingleAssetVault) ||
        ctx.rules.enabled(featureDynamicMPT))
    {
        // Is this transaction actually changing anything ?
        if (txFlags == 0 && !ctx.tx.isFieldPresent(sfDomainID) && !isMutate)
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

    if (auto const mutableFlags = ctx.tx[~sfMutableFlags])
    {
        if (std::any_of(
                mptMutabilityFlags.begin(),
                mptMutabilityFlags.end(),
                [mutableFlags, &isMutableFlag](auto const& f) {
                    return !isMutableFlag(f.canMutateFlag) &&
                        ((*mutableFlags & (f.setFlag | f.clearFlag)));
                }))
            return tecNO_PERMISSION;
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
                flagsOut |= f.canMutateFlag;
            else if (mutableFlags & f.clearFlag)
                flagsOut &= ~f.canMutateFlag;
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

    view().update(sle);

    return tesSUCCESS;
}

}  // namespace ripple
