/** @file
 *  Defines the `InnerObjectFormats` singleton registry, which declares the
 *  canonical field schemas (`SOTemplate`) for every structured sub-object that
 *  can appear inside an XRPL serialized object (`STObject`).
 *
 *  This registry is the inner-object counterpart to `TxFormats` and
 *  `LedgerFormats`: those govern top-level transaction and ledger-entry shapes,
 *  while `InnerObjectFormats` governs nested objects embedded within them.
 *  Callers in `STObject` (`makeInnerObject`, `applyTemplateFromSField`,
 *  `getFieldObject`) use this registry to enforce field-presence rules during
 *  construction and deserialization.
 */

#include <xrpl/protocol/InnerObjectFormats.h>

#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>

namespace xrpl {

/** Registers all known inner object schemas.
 *
 *  Each `add()` call binds an `SField`'s JSON name and integer field code to
 *  an `SOTemplate` that declares which child fields are required, optional, or
 *  default.  Using the field's own code as the registry key means no separate
 *  enum is needed — the `SField` identity doubles as the lookup key.
 *
 *  @note Any inner object whose `SOTemplate` carries `soeDEFAULT` fields (e.g.
 *      `sfTradingFee`, `sfDiscountedFee`, `sfScale`) must be constructed via
 *      `STObject::makeInnerObject()` so that the template is applied before
 *      field values are set; constructing such objects directly and then
 *      calling `set()` bypasses the default-omission logic.
 */
InnerObjectFormats::InnerObjectFormats()
{
    add(sfSignerEntry.jsonName,
        sfSignerEntry.getCode(),
        {
            {sfAccount, SoeRequired},
            {sfSignerWeight, SoeRequired},
            {sfWalletLocator, SoeOptional},
        });

    add(sfSigner.jsonName,
        sfSigner.getCode(),
        {
            {sfAccount, SoeRequired},
            {sfSigningPubKey, SoeRequired},
            {sfTxnSignature, SoeRequired},
        });

    add(sfMajority.jsonName,
        sfMajority.getCode(),
        {
            {sfAmendment, SoeRequired},
            {sfCloseTime, SoeRequired},
        });

    add(sfDisabledValidator.jsonName,
        sfDisabledValidator.getCode(),
        {
            {sfPublicKey, SoeRequired},
            {sfFirstLedgerSequence, SoeRequired},
        });

    add(sfNFToken.jsonName,
        sfNFToken.getCode(),
        {
            {sfNFTokenID, SoeRequired},
            {sfURI, SoeOptional},
        });

    add(sfVoteEntry.jsonName,
        sfVoteEntry.getCode(),
        {
            {sfAccount, SoeRequired},
            {sfTradingFee, SoeDefault},
            {sfVoteWeight, SoeRequired},
        });

    add(sfAuctionSlot.jsonName,
        sfAuctionSlot.getCode(),
        {{sfAccount, SoeRequired},
         {sfExpiration, SoeRequired},
         {sfDiscountedFee, SoeDefault},
         {sfPrice, SoeRequired},
         {sfAuthAccounts, SoeOptional}});

    add(sfXChainClaimAttestationCollectionElement.jsonName,
        sfXChainClaimAttestationCollectionElement.getCode(),
        {
            {sfAttestationSignerAccount, SoeRequired},
            {sfPublicKey, SoeRequired},
            {sfSignature, SoeRequired},
            {sfAmount, SoeRequired},
            {sfAccount, SoeRequired},
            {sfAttestationRewardAccount, SoeRequired},
            {sfWasLockingChainSend, SoeRequired},
            {sfXChainClaimID, SoeRequired},
            {sfDestination, SoeOptional},
        });

    add(sfXChainCreateAccountAttestationCollectionElement.jsonName,
        sfXChainCreateAccountAttestationCollectionElement.getCode(),
        {
            {sfAttestationSignerAccount, SoeRequired},
            {sfPublicKey, SoeRequired},
            {sfSignature, SoeRequired},
            {sfAmount, SoeRequired},
            {sfAccount, SoeRequired},
            {sfAttestationRewardAccount, SoeRequired},
            {sfWasLockingChainSend, SoeRequired},
            {sfXChainAccountCreateCount, SoeRequired},
            {sfDestination, SoeRequired},
            {sfSignatureReward, SoeRequired},
        });

    add(sfXChainClaimProofSig.jsonName,
        sfXChainClaimProofSig.getCode(),
        {
            {sfAttestationSignerAccount, SoeRequired},
            {sfPublicKey, SoeRequired},
            {sfAmount, SoeRequired},
            {sfAttestationRewardAccount, SoeRequired},
            {sfWasLockingChainSend, SoeRequired},
            {sfDestination, SoeOptional},
        });

    add(sfXChainCreateAccountProofSig.jsonName,
        sfXChainCreateAccountProofSig.getCode(),
        {
            {sfAttestationSignerAccount, SoeRequired},
            {sfPublicKey, SoeRequired},
            {sfAmount, SoeRequired},
            {sfSignatureReward, SoeRequired},
            {sfAttestationRewardAccount, SoeRequired},
            {sfWasLockingChainSend, SoeRequired},
            {sfDestination, SoeRequired},
        });

    add(sfAuthAccount.jsonName,
        sfAuthAccount.getCode(),
        {
            {sfAccount, SoeRequired},
        });

    add(sfPriceData.jsonName,
        sfPriceData.getCode(),
        {
            {sfBaseAsset, SoeRequired},
            {sfQuoteAsset, SoeRequired},
            {sfAssetPrice, SoeOptional},
            {sfScale, SoeDefault},
        });

    add(sfCredential.jsonName,
        sfCredential.getCode(),
        {
            {sfIssuer, SoeRequired},
            {sfCredentialType, SoeRequired},
        });

    add(sfPermission.jsonName.cStr(), sfPermission.getCode(), {{sfPermissionValue, SoeRequired}});

    add(sfBatchSigner.jsonName.cStr(),
        sfBatchSigner.getCode(),
        {{sfAccount, SoeRequired},
         {sfSigningPubKey, SoeOptional},
         {sfTxnSignature, SoeOptional},
         {sfSigners, SoeOptional}});

    add(sfBook.jsonName,
        sfBook.getCode(),
        {
            {sfBookDirectory, SoeRequired},
            {sfBookNode, SoeRequired},
        });

    add(sfCounterpartySignature.jsonName,
        sfCounterpartySignature.getCode(),
        {
            {sfSigningPubKey, SoeOptional},
            {sfTxnSignature, SoeOptional},
            {sfSigners, SoeOptional},
        });
}

/** Return the process-wide singleton instance.
 *
 *  Initialized on first call via a Meyer's static local; safe for concurrent
 *  access after that point.  The object is immutable after construction.
 *
 *  @return A const reference to the singleton `InnerObjectFormats` registry.
 */
InnerObjectFormats const&
InnerObjectFormats::getInstance()
{
    static InnerObjectFormats const kINSTANCE;
    return kINSTANCE;
}

/** Look up the field schema for a structured inner object.
 *
 *  Translates an `SField` to its registered `SOTemplate` by matching on the
 *  field's integer code.  The returned pointer is stable for the lifetime of
 *  the process; callers may cache it.
 *
 *  @param sField The `SField` identifying the inner object type.
 *  @return A pointer to the `SOTemplate` if the field is a registered inner
 *      object type, or `nullptr` if it is not.
 */
SOTemplate const*
InnerObjectFormats::findSOTemplateBySField(SField const& sField) const
{
    auto itemPtr = findByType(sField.getCode());
    if (itemPtr != nullptr)
        return &(itemPtr->getSOTemplate());

    return nullptr;
}

}  // namespace xrpl
