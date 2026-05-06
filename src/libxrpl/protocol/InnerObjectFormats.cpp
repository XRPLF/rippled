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
            {sfAccount, SoeRequired, SoeConstant},
            {sfSignerWeight, SoeRequired, SoeNotconstant},
            {sfWalletLocator, SoeOptional, SoeNotconstant},
        });

    add(sfSigner.jsonName,
        sfSigner.getCode(),
        {
            {sfAccount, SoeRequired, SoeConstant},
            {sfSigningPubKey, SoeRequired, SoeConstant},
            {sfTxnSignature, SoeRequired, SoeConstant},
        });

    add(sfMajority.jsonName,
        sfMajority.getCode(),
        {
            {sfAmendment, SoeRequired, SoeConstant},
            {sfCloseTime, SoeRequired, SoeConstant},
        });

    add(sfDisabledValidator.jsonName,
        sfDisabledValidator.getCode(),
        {
            {sfPublicKey, SoeRequired, SoeConstant},
            {sfFirstLedgerSequence, SoeRequired, SoeConstant},
        });

    add(sfNFToken.jsonName,
        sfNFToken.getCode(),
        {
            {sfNFTokenID, SoeRequired, SoeConstant},
            {sfURI, SoeOptional, SoeNotconstant},
        });

    add(sfVoteEntry.jsonName,
        sfVoteEntry.getCode(),
        {
            {sfAccount, SoeRequired, SoeConstant},
            {sfTradingFee, SoeDefault, SoeNotconstant},
            {sfVoteWeight, SoeRequired, SoeNotconstant},
        });

    add(sfAuctionSlot.jsonName,
        sfAuctionSlot.getCode(),
        {{sfAccount, SoeRequired, SoeConstant},
         {sfExpiration, SoeRequired, SoeConstant},
         {sfDiscountedFee, SoeDefault, SoeConstant},
         {sfPrice, SoeRequired, SoeConstant},
         {sfAuthAccounts, SoeOptional, SoeConstant}});

    add(sfXChainClaimAttestationCollectionElement.jsonName,
        sfXChainClaimAttestationCollectionElement.getCode(),
        {
            {sfAttestationSignerAccount, SoeRequired, SoeConstant},
            {sfPublicKey, SoeRequired, SoeConstant},
            {sfSignature, SoeRequired, SoeConstant},
            {sfAmount, SoeRequired, SoeConstant},
            {sfAccount, SoeRequired, SoeConstant},
            {sfAttestationRewardAccount, SoeRequired, SoeConstant},
            {sfWasLockingChainSend, SoeRequired, SoeConstant},
            {sfXChainClaimID, SoeRequired, SoeConstant},
            {sfDestination, SoeOptional, SoeConstant},
        });

    add(sfXChainCreateAccountAttestationCollectionElement.jsonName,
        sfXChainCreateAccountAttestationCollectionElement.getCode(),
        {
            {sfAttestationSignerAccount, SoeRequired, SoeConstant},
            {sfPublicKey, SoeRequired, SoeConstant},
            {sfSignature, SoeRequired, SoeConstant},
            {sfAmount, SoeRequired, SoeConstant},
            {sfAccount, SoeRequired, SoeConstant},
            {sfAttestationRewardAccount, SoeRequired, SoeConstant},
            {sfWasLockingChainSend, SoeRequired, SoeConstant},
            {sfXChainAccountCreateCount, SoeRequired, SoeConstant},
            {sfDestination, SoeRequired, SoeConstant},
            {sfSignatureReward, SoeRequired, SoeConstant},
        });

    add(sfXChainClaimProofSig.jsonName,
        sfXChainClaimProofSig.getCode(),
        {
            {sfAttestationSignerAccount, SoeRequired, SoeConstant},
            {sfPublicKey, SoeRequired, SoeConstant},
            {sfAmount, SoeRequired, SoeConstant},
            {sfAttestationRewardAccount, SoeRequired, SoeConstant},
            {sfWasLockingChainSend, SoeRequired, SoeConstant},
            {sfDestination, SoeOptional, SoeConstant},
        });

    add(sfXChainCreateAccountProofSig.jsonName,
        sfXChainCreateAccountProofSig.getCode(),
        {
            {sfAttestationSignerAccount, SoeRequired, SoeConstant},
            {sfPublicKey, SoeRequired, SoeConstant},
            {sfAmount, SoeRequired, SoeConstant},
            {sfSignatureReward, SoeRequired, SoeConstant},
            {sfAttestationRewardAccount, SoeRequired, SoeConstant},
            {sfWasLockingChainSend, SoeRequired, SoeConstant},
            {sfDestination, SoeRequired, SoeConstant},
        });

    add(sfAuthAccount.jsonName,
        sfAuthAccount.getCode(),
        {
            {sfAccount, SoeRequired, SoeConstant},
        });

    add(sfPriceData.jsonName,
        sfPriceData.getCode(),
        {
            {sfBaseAsset, SoeRequired, SoeConstant},
            {sfQuoteAsset, SoeRequired, SoeConstant},
            {sfAssetPrice, SoeOptional, SoeConstant},
            {sfScale, SoeDefault, SoeConstant},
        });

    add(sfCredential.jsonName,
        sfCredential.getCode(),
        {
            {sfIssuer, SoeRequired, SoeConstant},
            {sfCredentialType, SoeRequired, SoeConstant},
        });

    add(sfPermission.jsonName.c_str(),
        sfPermission.getCode(),
        {{sfPermissionValue, SoeRequired, SoeConstant}});

    add(sfBatchSigner.jsonName.c_str(),
        sfBatchSigner.getCode(),
        {{sfAccount, SoeRequired, SoeConstant},
         {sfSigningPubKey, SoeOptional, SoeConstant},
         {sfTxnSignature, SoeOptional, SoeConstant},
         {sfSigners, SoeOptional, SoeConstant}});

    add(sfBook.jsonName,
        sfBook.getCode(),
        {
            {sfBookDirectory, SoeRequired, SoeConstant},
            {sfBookNode, SoeRequired, SoeConstant},
        });

    add(sfCounterpartySignature.jsonName,
        sfCounterpartySignature.getCode(),
        {
            {sfSigningPubKey, SoeOptional, SoeConstant},
            {sfTxnSignature, SoeOptional, SoeConstant},
            {sfSigners, SoeOptional, SoeConstant},
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
