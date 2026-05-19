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

InnerObjectFormats const&
InnerObjectFormats::getInstance()
{
    static InnerObjectFormats const kInstance;
    return kInstance;
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
