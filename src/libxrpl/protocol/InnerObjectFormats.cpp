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
            {sfAccount, SoeRequired, soeCONSTANT},
            {sfSignerWeight, SoeRequired, soeNOTCONSTANT},
            {sfWalletLocator, SoeOptional, soeNOTCONSTANT},
        });

    add(sfSigner.jsonName,
        sfSigner.getCode(),
        {
            {sfAccount, SoeRequired, soeCONSTANT},
            {sfSigningPubKey, SoeRequired, soeCONSTANT},
            {sfTxnSignature, SoeRequired, soeCONSTANT},
        });

    add(sfMajority.jsonName,
        sfMajority.getCode(),
        {
            {sfAmendment, SoeRequired, soeCONSTANT},
            {sfCloseTime, SoeRequired, soeCONSTANT},
        });

    add(sfDisabledValidator.jsonName,
        sfDisabledValidator.getCode(),
        {
            {sfPublicKey, SoeRequired, soeCONSTANT},
            {sfFirstLedgerSequence, SoeRequired, soeCONSTANT},
        });

    add(sfNFToken.jsonName,
        sfNFToken.getCode(),
        {
            {sfNFTokenID, SoeRequired, soeCONSTANT},
            {sfURI, SoeOptional, soeNOTCONSTANT},
        });

    add(sfVoteEntry.jsonName,
        sfVoteEntry.getCode(),
        {
            {sfAccount, SoeRequired, soeCONSTANT},
            {sfTradingFee, SoeDefault, soeNOTCONSTANT},
            {sfVoteWeight, SoeRequired, soeNOTCONSTANT},
        });

    add(sfAuctionSlot.jsonName,
        sfAuctionSlot.getCode(),
        {{sfAccount, SoeRequired, soeCONSTANT},
         {sfExpiration, SoeRequired, soeCONSTANT},
         {sfDiscountedFee, SoeDefault, soeCONSTANT},
         {sfPrice, SoeRequired, soeCONSTANT},
         {sfAuthAccounts, SoeOptional, soeCONSTANT}});

    add(sfXChainClaimAttestationCollectionElement.jsonName,
        sfXChainClaimAttestationCollectionElement.getCode(),
        {
            {sfAttestationSignerAccount, SoeRequired, soeCONSTANT},
            {sfPublicKey, SoeRequired, soeCONSTANT},
            {sfSignature, SoeRequired, soeCONSTANT},
            {sfAmount, SoeRequired, soeCONSTANT},
            {sfAccount, SoeRequired, soeCONSTANT},
            {sfAttestationRewardAccount, SoeRequired, soeCONSTANT},
            {sfWasLockingChainSend, SoeRequired, soeCONSTANT},
            {sfXChainClaimID, SoeRequired, soeCONSTANT},
            {sfDestination, SoeOptional, soeCONSTANT},
        });

    add(sfXChainCreateAccountAttestationCollectionElement.jsonName,
        sfXChainCreateAccountAttestationCollectionElement.getCode(),
        {
            {sfAttestationSignerAccount, SoeRequired, soeCONSTANT},
            {sfPublicKey, SoeRequired, soeCONSTANT},
            {sfSignature, SoeRequired, soeCONSTANT},
            {sfAmount, SoeRequired, soeCONSTANT},
            {sfAccount, SoeRequired, soeCONSTANT},
            {sfAttestationRewardAccount, SoeRequired, soeCONSTANT},
            {sfWasLockingChainSend, SoeRequired, soeCONSTANT},
            {sfXChainAccountCreateCount, SoeRequired, soeCONSTANT},
            {sfDestination, SoeRequired, soeCONSTANT},
            {sfSignatureReward, SoeRequired, soeCONSTANT},
        });

    add(sfXChainClaimProofSig.jsonName,
        sfXChainClaimProofSig.getCode(),
        {
            {sfAttestationSignerAccount, SoeRequired, soeCONSTANT},
            {sfPublicKey, SoeRequired, soeCONSTANT},
            {sfAmount, SoeRequired, soeCONSTANT},
            {sfAttestationRewardAccount, SoeRequired, soeCONSTANT},
            {sfWasLockingChainSend, SoeRequired, soeCONSTANT},
            {sfDestination, SoeOptional, soeCONSTANT},
        });

    add(sfXChainCreateAccountProofSig.jsonName,
        sfXChainCreateAccountProofSig.getCode(),
        {
            {sfAttestationSignerAccount, SoeRequired, soeCONSTANT},
            {sfPublicKey, SoeRequired, soeCONSTANT},
            {sfAmount, SoeRequired, soeCONSTANT},
            {sfSignatureReward, SoeRequired, soeCONSTANT},
            {sfAttestationRewardAccount, SoeRequired, soeCONSTANT},
            {sfWasLockingChainSend, SoeRequired, soeCONSTANT},
            {sfDestination, SoeRequired, soeCONSTANT},
        });

    add(sfAuthAccount.jsonName,
        sfAuthAccount.getCode(),
        {
            {sfAccount, SoeRequired, soeCONSTANT},
        });

    add(sfPriceData.jsonName,
        sfPriceData.getCode(),
        {
            {sfBaseAsset, SoeRequired, soeCONSTANT},
            {sfQuoteAsset, SoeRequired, soeCONSTANT},
            {sfAssetPrice, SoeOptional, soeCONSTANT},
            {sfScale, SoeDefault, soeCONSTANT},
        });

    add(sfCredential.jsonName,
        sfCredential.getCode(),
        {
            {sfIssuer, SoeRequired, soeCONSTANT},
            {sfCredentialType, SoeRequired, soeCONSTANT},
        });

    add(sfPermission.jsonName.c_str(),
        sfPermission.getCode(),
        {{sfPermissionValue, SoeRequired, soeCONSTANT}});

    add(sfBatchSigner.jsonName.c_str(),
        sfBatchSigner.getCode(),
        {{sfAccount, SoeRequired, soeCONSTANT},
         {sfSigningPubKey, SoeOptional, soeCONSTANT},
         {sfTxnSignature, SoeOptional, soeCONSTANT},
         {sfSigners, SoeOptional, soeCONSTANT}});

    add(sfBook.jsonName,
        sfBook.getCode(),
        {
            {sfBookDirectory, SoeRequired, soeCONSTANT},
            {sfBookNode, SoeRequired, soeCONSTANT},
        });

    add(sfCounterpartySignature.jsonName,
        sfCounterpartySignature.getCode(),
        {
            {sfSigningPubKey, SoeOptional, soeCONSTANT},
            {sfTxnSignature, SoeOptional, soeCONSTANT},
            {sfSigners, SoeOptional, soeCONSTANT},
        });
}

InnerObjectFormats const&
InnerObjectFormats::getInstance()
{
    static InnerObjectFormats const instance;
    return instance;
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
