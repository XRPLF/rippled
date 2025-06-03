//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpl/protocol/InnerObjectFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>

namespace ripple {

InnerObjectFormats::InnerObjectFormats()
{
    // inner objects with the default fields have to be
    // constructed with STObject::makeInnerObject()

    add(sfSignerEntry.jsonName,
        sfSignerEntry.getCode(),
        {
            {sfAccount, soeREQUIRED},
            {sfSignerWeight, soeREQUIRED},
            {sfWalletLocator, soeOPTIONAL},
        });

    add(sfSigner.jsonName,
        sfSigner.getCode(),
        {
            {sfAccount, soeREQUIRED},
            {sfSigningPubKey, soeREQUIRED},
            {sfTxnSignature, soeREQUIRED},
        });

    add(sfMajority.jsonName,
        sfMajority.getCode(),
        {
            {sfAmendment, soeREQUIRED},
            {sfCloseTime, soeREQUIRED},
        });

    add(sfDisabledValidator.jsonName,
        sfDisabledValidator.getCode(),
        {
            {sfPublicKey, soeREQUIRED},
            {sfFirstLedgerSequence, soeREQUIRED},
        });

    add(sfNFToken.jsonName,
        sfNFToken.getCode(),
        {
            {sfNFTokenID, soeREQUIRED},
            {sfURI, soeOPTIONAL},
        });

    add(sfVoteEntry.jsonName,
        sfVoteEntry.getCode(),
        {
            {sfAccount, soeREQUIRED},
            {sfTradingFee, soeDEFAULT},
            {sfVoteWeight, soeREQUIRED},
        });

    add(sfAuctionSlot.jsonName,
        sfAuctionSlot.getCode(),
        {{sfAccount, soeREQUIRED},
         {sfExpiration, soeREQUIRED},
         {sfDiscountedFee, soeDEFAULT},
         {sfPrice, soeREQUIRED},
         {sfAuthAccounts, soeOPTIONAL}});

    add(sfXChainClaimAttestationCollectionElement.jsonName,
        sfXChainClaimAttestationCollectionElement.getCode(),
        {
            {sfAttestationSignerAccount, soeREQUIRED},
            {sfPublicKey, soeREQUIRED},
            {sfSignature, soeREQUIRED},
            {sfAmount, soeREQUIRED},
            {sfAccount, soeREQUIRED},
            {sfAttestationRewardAccount, soeREQUIRED},
            {sfWasLockingChainSend, soeREQUIRED},
            {sfXChainClaimID, soeREQUIRED},
            {sfDestination, soeOPTIONAL},
        });

    add(sfXChainCreateAccountAttestationCollectionElement.jsonName,
        sfXChainCreateAccountAttestationCollectionElement.getCode(),
        {
            {sfAttestationSignerAccount, soeREQUIRED},
            {sfPublicKey, soeREQUIRED},
            {sfSignature, soeREQUIRED},
            {sfAmount, soeREQUIRED},
            {sfAccount, soeREQUIRED},
            {sfAttestationRewardAccount, soeREQUIRED},
            {sfWasLockingChainSend, soeREQUIRED},
            {sfXChainAccountCreateCount, soeREQUIRED},
            {sfDestination, soeREQUIRED},
            {sfSignatureReward, soeREQUIRED},
        });

    add(sfXChainClaimProofSig.jsonName,
        sfXChainClaimProofSig.getCode(),
        {
            {sfAttestationSignerAccount, soeREQUIRED},
            {sfPublicKey, soeREQUIRED},
            {sfAmount, soeREQUIRED},
            {sfAttestationRewardAccount, soeREQUIRED},
            {sfWasLockingChainSend, soeREQUIRED},
            {sfDestination, soeOPTIONAL},
        });

    add(sfXChainCreateAccountProofSig.jsonName,
        sfXChainCreateAccountProofSig.getCode(),
        {
            {sfAttestationSignerAccount, soeREQUIRED},
            {sfPublicKey, soeREQUIRED},
            {sfAmount, soeREQUIRED},
            {sfSignatureReward, soeREQUIRED},
            {sfAttestationRewardAccount, soeREQUIRED},
            {sfWasLockingChainSend, soeREQUIRED},
            {sfDestination, soeREQUIRED},
        });

    add(sfAuthAccount.jsonName,
        sfAuthAccount.getCode(),
        {
            {sfAccount, soeREQUIRED},
        });

    add(sfPriceData.jsonName,
        sfPriceData.getCode(),
        {
            {sfBaseAsset, soeREQUIRED},
            {sfQuoteAsset, soeREQUIRED},
            {sfAssetPrice, soeOPTIONAL},
            {sfScale, soeDEFAULT},
        });

    add(sfCredential.jsonName,
        sfCredential.getCode(),
        {
            {sfIssuer, soeREQUIRED},
            {sfCredentialType, soeREQUIRED},
        });

    add(sfPermission.jsonName.c_str(),
        sfPermission.getCode(),
        {{sfPermissionValue, soeREQUIRED}});

    add(sfBatchSigner.jsonName.c_str(),
        sfBatchSigner.getCode(),
        {{sfAccount, soeREQUIRED},
         {sfSigningPubKey, soeOPTIONAL},
         {sfTxnSignature, soeOPTIONAL},
         {sfSigners, soeOPTIONAL}});

    add(sfBook.jsonName,
        sfBook.getCode(),
        {
            {sfBookDirectory, soeREQUIRED},
            {sfBookNode, soeREQUIRED},
        });

    add(sfCounterpartySignature.jsonName,
        sfCounterpartySignature.getCode(),
        {
            {sfSigningPubKey, soeOPTIONAL},
            {sfTxnSignature, soeOPTIONAL},
            {sfSigners, soeOPTIONAL},
        });
}

InnerObjectFormats const&
InnerObjectFormats::getInstance()
{
    static InnerObjectFormats instance;
    return instance;
}

SOTemplate const*
InnerObjectFormats::findSOTemplateBySField(SField const& sField) const
{
    auto itemPtr = findByType(sField.getCode());
    if (itemPtr)
        return &(itemPtr->getSOTemplate());

    return nullptr;
}

}  // namespace ripple
