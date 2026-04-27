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
            {sfAccount, soeREQUIRED, soeCONSTANT},
            {sfSignerWeight, soeREQUIRED, soeNOTCONSTANT},
            {sfWalletLocator, soeOPTIONAL, soeNOTCONSTANT},
        });

    add(sfSigner.jsonName,
        sfSigner.getCode(),
        {
            {sfAccount, soeREQUIRED, soeCONSTANT},
            {sfSigningPubKey, soeREQUIRED, soeCONSTANT},
            {sfTxnSignature, soeREQUIRED, soeCONSTANT},
        });

    add(sfMajority.jsonName,
        sfMajority.getCode(),
        {
            {sfAmendment, soeREQUIRED, soeCONSTANT},
            {sfCloseTime, soeREQUIRED, soeCONSTANT},
        });

    add(sfDisabledValidator.jsonName,
        sfDisabledValidator.getCode(),
        {
            {sfPublicKey, soeREQUIRED, soeCONSTANT},
            {sfFirstLedgerSequence, soeREQUIRED, soeCONSTANT},
        });

    add(sfNFToken.jsonName,
        sfNFToken.getCode(),
        {
            {sfNFTokenID, soeREQUIRED, soeCONSTANT},
            {sfURI, soeOPTIONAL, soeNOTCONSTANT},
        });

    add(sfVoteEntry.jsonName,
        sfVoteEntry.getCode(),
        {
            {sfAccount, soeREQUIRED, soeCONSTANT},
            {sfTradingFee, soeDEFAULT, soeNOTCONSTANT},
            {sfVoteWeight, soeREQUIRED, soeNOTCONSTANT},
        });

    add(sfAuctionSlot.jsonName,
        sfAuctionSlot.getCode(),
        {{sfAccount, soeREQUIRED, soeCONSTANT},
         {sfExpiration, soeREQUIRED, soeCONSTANT},
         {sfDiscountedFee, soeDEFAULT, soeCONSTANT},
         {sfPrice, soeREQUIRED, soeCONSTANT},
         {sfAuthAccounts, soeOPTIONAL, soeCONSTANT}});

    add(sfXChainClaimAttestationCollectionElement.jsonName,
        sfXChainClaimAttestationCollectionElement.getCode(),
        {
            {sfAttestationSignerAccount, soeREQUIRED, soeCONSTANT},
            {sfPublicKey, soeREQUIRED, soeCONSTANT},
            {sfSignature, soeREQUIRED, soeCONSTANT},
            {sfAmount, soeREQUIRED, soeCONSTANT},
            {sfAccount, soeREQUIRED, soeCONSTANT},
            {sfAttestationRewardAccount, soeREQUIRED, soeCONSTANT},
            {sfWasLockingChainSend, soeREQUIRED, soeCONSTANT},
            {sfXChainClaimID, soeREQUIRED, soeCONSTANT},
            {sfDestination, soeOPTIONAL, soeCONSTANT},
        });

    add(sfXChainCreateAccountAttestationCollectionElement.jsonName,
        sfXChainCreateAccountAttestationCollectionElement.getCode(),
        {
            {sfAttestationSignerAccount, soeREQUIRED, soeCONSTANT},
            {sfPublicKey, soeREQUIRED, soeCONSTANT},
            {sfSignature, soeREQUIRED, soeCONSTANT},
            {sfAmount, soeREQUIRED, soeCONSTANT},
            {sfAccount, soeREQUIRED, soeCONSTANT},
            {sfAttestationRewardAccount, soeREQUIRED, soeCONSTANT},
            {sfWasLockingChainSend, soeREQUIRED, soeCONSTANT},
            {sfXChainAccountCreateCount, soeREQUIRED, soeCONSTANT},
            {sfDestination, soeREQUIRED, soeCONSTANT},
            {sfSignatureReward, soeREQUIRED, soeCONSTANT},
        });

    add(sfXChainClaimProofSig.jsonName,
        sfXChainClaimProofSig.getCode(),
        {
            {sfAttestationSignerAccount, soeREQUIRED, soeCONSTANT},
            {sfPublicKey, soeREQUIRED, soeCONSTANT},
            {sfAmount, soeREQUIRED, soeCONSTANT},
            {sfAttestationRewardAccount, soeREQUIRED, soeCONSTANT},
            {sfWasLockingChainSend, soeREQUIRED, soeCONSTANT},
            {sfDestination, soeOPTIONAL, soeCONSTANT},
        });

    add(sfXChainCreateAccountProofSig.jsonName,
        sfXChainCreateAccountProofSig.getCode(),
        {
            {sfAttestationSignerAccount, soeREQUIRED, soeCONSTANT},
            {sfPublicKey, soeREQUIRED, soeCONSTANT},
            {sfAmount, soeREQUIRED, soeCONSTANT},
            {sfSignatureReward, soeREQUIRED, soeCONSTANT},
            {sfAttestationRewardAccount, soeREQUIRED, soeCONSTANT},
            {sfWasLockingChainSend, soeREQUIRED, soeCONSTANT},
            {sfDestination, soeREQUIRED, soeCONSTANT},
        });

    add(sfAuthAccount.jsonName,
        sfAuthAccount.getCode(),
        {
            {sfAccount, soeREQUIRED, soeCONSTANT},
        });

    add(sfPriceData.jsonName,
        sfPriceData.getCode(),
        {
            {sfBaseAsset, soeREQUIRED, soeCONSTANT},
            {sfQuoteAsset, soeREQUIRED, soeCONSTANT},
            {sfAssetPrice, soeOPTIONAL, soeCONSTANT},
            {sfScale, soeDEFAULT, soeCONSTANT},
        });

    add(sfCredential.jsonName,
        sfCredential.getCode(),
        {
            {sfIssuer, soeREQUIRED, soeCONSTANT},
            {sfCredentialType, soeREQUIRED, soeCONSTANT},
        });

    add(sfPermission.jsonName.c_str(),
        sfPermission.getCode(),
        {{sfPermissionValue, soeREQUIRED, soeCONSTANT}});

    add(sfBatchSigner.jsonName.c_str(),
        sfBatchSigner.getCode(),
        {{sfAccount, soeREQUIRED, soeCONSTANT},
         {sfSigningPubKey, soeOPTIONAL, soeCONSTANT},
         {sfTxnSignature, soeOPTIONAL, soeCONSTANT},
         {sfSigners, soeOPTIONAL, soeCONSTANT}});

    add(sfBook.jsonName,
        sfBook.getCode(),
        {
            {sfBookDirectory, soeREQUIRED, soeCONSTANT},
            {sfBookNode, soeREQUIRED, soeCONSTANT},
        });

    add(sfCounterpartySignature.jsonName,
        sfCounterpartySignature.getCode(),
        {
            {sfSigningPubKey, soeOPTIONAL, soeCONSTANT},
            {sfTxnSignature, soeOPTIONAL, soeCONSTANT},
            {sfSigners, soeOPTIONAL, soeCONSTANT},
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
