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

NotTEC
MPTokenIssuanceSet::preflight(PreflightContext const& ctx)
{
    auto const txFlags = ctx.tx.getFlags();
    auto const enableFlags = txFlags & tfMPTokenIssuanceSetEnableFlagMask;
    auto const metadata = ctx.tx[~sfMPTokenMetadata];
    auto const transferFee = ctx.tx[~sfTransferFee];
    auto const immutableFlags = ctx.tx[~sfImmutableFlags];
    auto const isMutate = (enableFlags != 0u) || metadata || transferFee || immutableFlags;
    auto const hasIssuerElGamalKey = ctx.tx.isFieldPresent(sfIssuerEncryptionKey);
    auto const hasAuditorElGamalKey = ctx.tx.isFieldPresent(sfAuditorEncryptionKey);

    bool const enablePrivacy = (enableFlags & tfMPTSetCanHoldConfidentialBalance) != 0u;
    auto const hasDomain = ctx.tx.isFieldPresent(sfDomainID);
    auto const hasHolder = ctx.tx.isFieldPresent(sfHolder);
    auto const hasHolderElGamalKey = ctx.tx.isFieldPresent(sfHolderEncryptionKey);
    auto const hasRecoveryKey = ctx.tx.isFieldPresent(sfRecoveryKey);

    if (isMutate && !ctx.rules.enabled(featureDynamicMPT))
        return temDISABLED;

    bool const setConfidentialBalanceImmutable =
        immutableFlags && (*immutableFlags & tifMPTCanHoldConfidentialBalance) != 0u;
    if ((hasIssuerElGamalKey || hasAuditorElGamalKey || enablePrivacy ||
         setConfidentialBalanceImmutable) &&
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
            !hasHolderElGamalKey && !hasRecoveryKey && !isMutate)
            return temMALFORMED;
    }

    if (ctx.rules.enabled(featureDynamicMPT))
    {
        // Holder field is not allowed when mutating MPTokenIssuance
        if (isMutate && holderID)
            return temMALFORMED;

        // A single transaction may either lock/unlock or mutate capability
        // flags, but not both.
        if (isMutate && (ctx.tx.isFlag(tfMPTLock) || ctx.tx.isFlag(tfMPTUnlock)))
            return temMALFORMED;

        if (transferFee && *transferFee > kMaxTransferFee)
            return temBAD_TRANSFER_FEE;

        if (transferFee && *transferFee > 0u && enablePrivacy)
            return temBAD_TRANSFER_FEE;

        if (metadata && metadata->length() > kMaxMpTokenMetadataLength)
            return temMALFORMED;

        // If the immutable flags field is included, at least one flag must be
        // specified, and undefined flags must not be specified.
        if (immutableFlags &&
            ((*immutableFlags == 0u) ||
             ((*immutableFlags & tifMPTokenIssuanceImmutableMask) != 0u)))
            return temINVALID_FLAG;
    }

    if (hasHolder && (hasIssuerElGamalKey || hasAuditorElGamalKey))
        return temMALFORMED;

    // Pre-ConfidentialMPTKeyRotation amendment, the auditor key could not be
    // registered independently of the issuer key. The issuer could either:
    // - Register only the issuer key (in which case an auditor key could not be added later), or
    // - Register both the issuer and auditor keys simultaneously.
    //
    // Post-ConfidentialMPTKeyRotation amendment, the auditor key can be
    // registered after the issuer key has already been registered.
    if (hasAuditorElGamalKey && !hasIssuerElGamalKey &&
        !ctx.rules.enabled(featureConfidentialMPTKeyRotation))
        return temMALFORMED;

    if (hasIssuerElGamalKey && !isValidCompressedECPoint(ctx.tx[sfIssuerEncryptionKey]))
        return temMALFORMED;

    if (hasAuditorElGamalKey && !isValidCompressedECPoint(ctx.tx[sfAuditorEncryptionKey]))
        return temMALFORMED;

    // TEMPORARY: Allow holder encryption key when holder is present (for testing recovery)
    // TODO: Remove when ConfidentialMPTHolderKeyUpdate is implemented
    if (hasHolderElGamalKey && !hasHolder)
        return temMALFORMED;

    if (hasHolderElGamalKey && !isValidCompressedECPoint(ctx.tx[sfHolderEncryptionKey]))
        return temMALFORMED;

    // TEMPORARY: Allow recovery key when holder is present (for testing recovery)
    // TODO: Remove when ConfidentialMPTHolderKeyUpdate is implemented
    if (hasRecoveryKey && !hasHolder)
        return temMALFORMED;

    if (hasRecoveryKey && !isValidCompressedECPoint(ctx.tx[sfRecoveryKey]))
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

    // sfImmutableFlags is soeDEFAULT, defaulting to 0 if not specified on
    // the ledger.
    auto const currentImmutableFlags = sleMptIssuance->getFieldU32(sfImmutableFlags);

    auto isImmutable = [&](std::uint32_t flag) -> bool { return currentImmutableFlags & flag; };

    auto const enableFlags = ctx.tx.getFlags() & tfMPTokenIssuanceSetEnableFlagMask;
    if (enableFlags != 0u)
    {
        // If any of the flags to be set is immutable, return tecNO_PERMISSION.
        if (std::ranges::any_of(flagMapping, [&](auto const& f) {
                return isImmutable(f.immutableFlag) && ctx.tx.isFlag(f.setFlag);
            }))
            return tecNO_PERMISSION;
    }

    if (isImmutable(lsifMPTMetadata) && ctx.tx.isFieldPresent(sfMPTokenMetadata))
        return tecNO_PERMISSION;

    if (auto const fee = ctx.tx[~sfTransferFee])
    {
        // A non-zero TransferFee is only valid if the lsfMPTCanTransfer flag
        // is already set on the ledger object, or is being enabled by this
        // same transaction. The Immutability of lsfMPTCanTransfer is checked above.
        if (fee > 0u && !sleMptIssuance->isFlag(lsfMPTCanTransfer) &&
            (enableFlags & tfMPTSetCanTransfer) == 0u)
            return tecNO_PERMISSION;

        // Cannot set a non-zero TransferFee on an issuance that has confidential
        // transfer enabled
        if (fee > 0u && sleMptIssuance->isFlag(lsfMPTCanHoldConfidentialBalance))
            return tecNO_PERMISSION;

        // Cannot set TransferFee if it is immutable
        if (isImmutable(lsifMPTTransferFee))
            return tecNO_PERMISSION;
    }

    // Updating an existing encryption key requires the
    // ConfidentialMPTKeyRotation amendment.
    bool const canRotateKey = ctx.view.rules().enabled(featureConfidentialMPTKeyRotation);

    bool const txHasIssuerKey = ctx.tx.isFieldPresent(sfIssuerEncryptionKey);
    bool const txHasAuditorKey = ctx.tx.isFieldPresent(sfAuditorEncryptionKey);
    bool const sleHasIssuerKey = sleMptIssuance->isFieldPresent(sfIssuerEncryptionKey);
    bool const sleHasAuditorKey = sleMptIssuance->isFieldPresent(sfAuditorEncryptionKey);

    if (canRotateKey)
    {
        // Post-ConfidentialMPTKeyRotation amendment, the encryption keys can be updated.
        // A first-time auditor key registration requires an issuer key,
        // either already on the issuance or set by the same transaction.
        bool const registersAuditorKey = txHasAuditorKey && !sleHasAuditorKey;
        bool const issuerKeyExists = sleHasIssuerKey || txHasIssuerKey;
        if (registersAuditorKey && !issuerKeyExists)
            return tecNO_PERMISSION;

        // Rotating a key to its current value is not permitted: a key epoch
        // increment must always correspond to an actual key change.
        if (txHasIssuerKey && sleHasIssuerKey &&
            ctx.tx[sfIssuerEncryptionKey] == (*sleMptIssuance)[sfIssuerEncryptionKey])
            return tecDUPLICATE;

        if (txHasAuditorKey && sleHasAuditorKey &&
            ctx.tx[sfAuditorEncryptionKey] == (*sleMptIssuance)[sfAuditorEncryptionKey])
            return tecDUPLICATE;
    }
    else
    {
        // Pre-ConfidentialMPTKeyRotation amendment, the encryption keys can not be updated.
        // cannot update issuer public key
        if (txHasIssuerKey && sleHasIssuerKey)
            return tecNO_PERMISSION;

        // cannot update auditor public key
        if (txHasAuditorKey && sleHasAuditorKey)
            return tecNO_PERMISSION;  // LCOV_EXCL_LINE
    }

    auto const enablesConfidentialBalance =
        (enableFlags & tfMPTSetCanHoldConfidentialBalance) != 0u;
    if (enablesConfidentialBalance && sleMptIssuance->isFieldPresent(sfTransferFee) &&
        (*sleMptIssuance)[sfTransferFee] > 0u)
        return tecNO_PERMISSION;

    // Encryption keys can only be set if confidential amounts are already
    // enabled on the issuance OR if the transaction is enabling it
    if (txHasIssuerKey && !sleMptIssuance->isFlag(lsfMPTCanHoldConfidentialBalance) &&
        !enablesConfidentialBalance)
    {
        return tecNO_PERMISSION;
    }

    if (txHasAuditorKey && !sleMptIssuance->isFlag(lsfMPTCanHoldConfidentialBalance) &&
        !enablesConfidentialBalance)
    {
        return tecNO_PERMISSION;
    }

    bool const hasConfidentialOA =
        (*sleMptIssuance)[~sfConfidentialOutstandingAmount].value_or(0) > 0;

    // Pre-ConfidentialMPTKeyRotation amendment, keys cannot be uploaded while
    // COA > 0. Post-amendment they can be uploaded even if COA > 0.
    if (!canRotateKey && (txHasIssuerKey || txHasAuditorKey) && hasConfidentialOA)
        return tecNO_PERMISSION;  // LCOV_EXCL_LINE

    // Enabling confidential balances when COA > 0 is not permitted, regardless of
    // ConfidentialMPTKeyRotation.
    if (enablesConfidentialBalance && hasConfidentialOA)
        return tecNO_PERMISSION;

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

    if (auto const enableFlags = (ctx_.tx.getFlags() & tfMPTokenIssuanceSetEnableFlagMask);
        enableFlags != 0u)
    {
        for (auto const& f : flagMapping)
        {
            if (ctx_.tx.isFlag(f.setFlag))
            {
                flagsOut |= f.ledgerFlag;
            }
        }
    }

    if (flagsIn != flagsOut)
        sle->setFieldU32(sfFlags, flagsOut);

    if (auto const immutableFlags = ctx_.tx[~sfImmutableFlags])
    {
        // sle is guaranteed to be an ltMPTOKEN_ISSUANCE rather than an ltMPTOKEN.
        // Preflight verification ensures that sfHolder and sfImmutableFlags can
        // never both be present in the same transaction. Therefore, if
        // sfImmutableFlags is present, sfHolder must be absent.
        //
        // In doApply, the absence of sfHolder causes the MPTokenIssuance keylet
        // to be peeked. The runtime check below is a defensive fallback in case
        // this invariant is ever broken by a future change.
        XRPL_ASSERT(
            sle->getType() == ltMPTOKEN_ISSUANCE,
            "MPTokenIssuanceSet::doApply : modifying MPTokenIssuance");

        if (sle->getType() != ltMPTOKEN_ISSUANCE)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        (*sle)[sfImmutableFlags] = (*sle)[sfImmutableFlags] | *immutableFlags;
    }

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

    // Sets an encryption key on the issuance. Overwriting an existing key
    // (a rotation) increments the corresponding key epoch; a first-time
    // registration leaves the epoch absent (epoch 0), matching issuances
    // whose keys were registered before the ConfidentialMPTKeyRotation
    // amendment.
    auto const setEncryptionKey = [&](SF_VL const& keyField, SF_UINT32 const& epochField) {
        auto const pubKey = ctx_.tx[~keyField];
        if (!pubKey)
            return;

        // This is enforced in preflight.
        XRPL_ASSERT(
            sle->getType() == ltMPTOKEN_ISSUANCE,
            "MPTokenIssuanceSet::doApply : modifying MPTokenIssuance");

        // NOTE: presence must be checked before the key is overwritten below.
        bool const isRotation = sle->isFieldPresent(keyField);

        sle->setFieldVL(keyField, *pubKey);

        if (isRotation)
            (*sle)[epochField] = (*sle)[~epochField].valueOr(0) + 1;
    };

    setEncryptionKey(sfIssuerEncryptionKey, sfIssuerKeyEpoch);
    setEncryptionKey(sfAuditorEncryptionKey, sfAuditorKeyEpoch);

    // TEMPORARY CODE: Handle holder-specific confidential fields for testing
    // TODO: Remove this when ConfidentialMPTHolderKeyUpdate is implemented
    if (holderID)
    {
        // Check if sfRecoveryKey is present in the transaction
        if (ctx_.tx.isFieldPresent(sfRecoveryKey))
        {
            // Set the recovery key field on the MPToken ledger object
            auto const recoveryPubKey = ctx_.tx.getFieldVL(sfRecoveryKey);
            sle->setFieldVL(sfRecoveryKey, recoveryPubKey);
        }
        // Check if normal holder encryption key is present
        else if (ctx_.tx.isFieldPresent(sfHolderEncryptionKey))
        {
            // Normal holder key update
            auto const holderPubKey = ctx_.tx.getFieldVL(sfHolderEncryptionKey);
            sle->setFieldVL(sfHolderEncryptionKey, holderPubKey);
        }
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
