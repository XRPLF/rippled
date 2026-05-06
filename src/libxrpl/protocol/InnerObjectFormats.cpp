#include <xrpl/protocol/InnerObjectFormats.h>

#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>

namespace xrpl {

InnerObjectFormats::InnerObjectFormats()
{
    // inner objects with the default fields have to be
    // constructed with STObject::makeInnerObject()

    add(sfSignerEntry.jsonName,
        sfSignerEntry.getCode(),
        {
            {sfAccount, SoeRequired, SoeImmutable},
            {sfSignerWeight, SoeRequired, SoeMutable},
            {sfWalletLocator, SoeOptional, SoeMutable},
        });

    add(sfSigner.jsonName,
        sfSigner.getCode(),
        {
            {sfAccount, SoeRequired, SoeImmutable},
            {sfSigningPubKey, SoeRequired, SoeImmutable},
            {sfTxnSignature, SoeRequired, SoeImmutable},
        });

    add(sfMajority.jsonName,
        sfMajority.getCode(),
        {
            {sfAmendment, SoeRequired, SoeImmutable},
            {sfCloseTime, SoeRequired, SoeImmutable},
        });

    add(sfDisabledValidator.jsonName,
        sfDisabledValidator.getCode(),
        {
            {sfPublicKey, SoeRequired, SoeImmutable},
            {sfFirstLedgerSequence, SoeRequired, SoeImmutable},
        });

    add(sfNFToken.jsonName,
        sfNFToken.getCode(),
        {
            {sfNFTokenID, SoeRequired, SoeImmutable},
            {sfURI, SoeOptional, SoeMutable},
        });

    add(sfVoteEntry.jsonName,
        sfVoteEntry.getCode(),
        {
            {sfAccount, SoeRequired, SoeImmutable},
            {sfTradingFee, SoeDefault, SoeMutable},
            {sfVoteWeight, SoeRequired, SoeMutable},
        });

    add(sfAuctionSlot.jsonName,
        sfAuctionSlot.getCode(),
        {{sfAccount, SoeRequired, SoeImmutable},
         {sfExpiration, SoeRequired, SoeImmutable},
         {sfDiscountedFee, SoeDefault, SoeImmutable},
         {sfPrice, SoeRequired, SoeImmutable},
         {sfAuthAccounts, SoeOptional, SoeImmutable}});

    add(sfXChainClaimAttestationCollectionElement.jsonName,
        sfXChainClaimAttestationCollectionElement.getCode(),
        {
            {sfAttestationSignerAccount, SoeRequired, SoeImmutable},
            {sfPublicKey, SoeRequired, SoeImmutable},
            {sfSignature, SoeRequired, SoeImmutable},
            {sfAmount, SoeRequired, SoeImmutable},
            {sfAccount, SoeRequired, SoeImmutable},
            {sfAttestationRewardAccount, SoeRequired, SoeImmutable},
            {sfWasLockingChainSend, SoeRequired, SoeImmutable},
            {sfXChainClaimID, SoeRequired, SoeImmutable},
            {sfDestination, SoeOptional, SoeImmutable},
        });

    add(sfXChainCreateAccountAttestationCollectionElement.jsonName,
        sfXChainCreateAccountAttestationCollectionElement.getCode(),
        {
            {sfAttestationSignerAccount, SoeRequired, SoeImmutable},
            {sfPublicKey, SoeRequired, SoeImmutable},
            {sfSignature, SoeRequired, SoeImmutable},
            {sfAmount, SoeRequired, SoeImmutable},
            {sfAccount, SoeRequired, SoeImmutable},
            {sfAttestationRewardAccount, SoeRequired, SoeImmutable},
            {sfWasLockingChainSend, SoeRequired, SoeImmutable},
            {sfXChainAccountCreateCount, SoeRequired, SoeImmutable},
            {sfDestination, SoeRequired, SoeImmutable},
            {sfSignatureReward, SoeRequired, SoeImmutable},
        });

    add(sfXChainClaimProofSig.jsonName,
        sfXChainClaimProofSig.getCode(),
        {
            {sfAttestationSignerAccount, SoeRequired, SoeImmutable},
            {sfPublicKey, SoeRequired, SoeImmutable},
            {sfAmount, SoeRequired, SoeImmutable},
            {sfAttestationRewardAccount, SoeRequired, SoeImmutable},
            {sfWasLockingChainSend, SoeRequired, SoeImmutable},
            {sfDestination, SoeOptional, SoeImmutable},
        });

    add(sfXChainCreateAccountProofSig.jsonName,
        sfXChainCreateAccountProofSig.getCode(),
        {
            {sfAttestationSignerAccount, SoeRequired, SoeImmutable},
            {sfPublicKey, SoeRequired, SoeImmutable},
            {sfAmount, SoeRequired, SoeImmutable},
            {sfSignatureReward, SoeRequired, SoeImmutable},
            {sfAttestationRewardAccount, SoeRequired, SoeImmutable},
            {sfWasLockingChainSend, SoeRequired, SoeImmutable},
            {sfDestination, SoeRequired, SoeImmutable},
        });

    add(sfAuthAccount.jsonName,
        sfAuthAccount.getCode(),
        {
            {sfAccount, SoeRequired, SoeImmutable},
        });

    add(sfPriceData.jsonName,
        sfPriceData.getCode(),
        {
            {sfBaseAsset, SoeRequired, SoeImmutable},
            {sfQuoteAsset, SoeRequired, SoeImmutable},
            {sfAssetPrice, SoeOptional, SoeImmutable},
            {sfScale, SoeDefault, SoeImmutable},
        });

    add(sfCredential.jsonName,
        sfCredential.getCode(),
        {
            {sfIssuer, SoeRequired, SoeImmutable},
            {sfCredentialType, SoeRequired, SoeImmutable},
        });

    add(sfPermission.jsonName,
        sfPermission.getCode(),
        {{sfPermissionValue, SoeRequired, SoeImmutable}});

    add(sfBatchSigner.jsonName,
        sfBatchSigner.getCode(),
        {{sfAccount, SoeRequired, SoeImmutable},
         {sfSigningPubKey, SoeOptional, SoeImmutable},
         {sfTxnSignature, SoeOptional, SoeImmutable},
         {sfSigners, SoeOptional, SoeImmutable}});

    add(sfBook.jsonName,
        sfBook.getCode(),
        {
            {sfBookDirectory, SoeRequired, SoeImmutable},
            {sfBookNode, SoeRequired, SoeImmutable},
        });

    add(sfCounterpartySignature.jsonName,
        sfCounterpartySignature.getCode(),
        {
            {sfSigningPubKey, SoeOptional, SoeImmutable},
            {sfTxnSignature, SoeOptional, SoeImmutable},
            {sfSigners, SoeOptional, SoeImmutable},
        });
}

InnerObjectFormats const&
InnerObjectFormats::getInstance()
{
    static InnerObjectFormats const kINSTANCE;
    return kINSTANCE;
}

SOTemplate const*
InnerObjectFormats::findSOTemplateBySField(SField const& sField) const
{
    auto itemPtr = findByType(sField.getCode());
    if (itemPtr != nullptr)
        return &(itemPtr->getSOTemplate());

    return nullptr;
}

}  // namespace xrpl
