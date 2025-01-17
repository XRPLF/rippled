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

    add(sfSignerEntry.jsonName.c_str(),
        sfSignerEntry.getCode(),
        {
            {sfAccount, soeREQUIRED},
            {sfSignerWeight, soeREQUIRED},
            {sfWalletLocator, soeOPTIONAL},
        });

    add(sfSigner.jsonName.c_str(),
        sfSigner.getCode(),
        {
            {sfAccount, soeREQUIRED},
            {sfSigningPubKey, soeREQUIRED},
            {sfTxnSignature, soeREQUIRED},
        });

    add(sfMajority.jsonName.c_str(),
        sfMajority.getCode(),
        {
            {sfAmendment, soeREQUIRED},
            {sfCloseTime, soeREQUIRED},
        });

    add(sfDisabledValidator.jsonName.c_str(),
        sfDisabledValidator.getCode(),
        {
            {sfPublicKey, soeREQUIRED},
            {sfFirstLedgerSequence, soeREQUIRED},
        });

    add(sfNFToken.jsonName.c_str(),
        sfNFToken.getCode(),
        {
            {sfNFTokenID, soeREQUIRED},
            {sfURI, soeOPTIONAL},
        });

    add(sfVoteEntry.jsonName.c_str(),
        sfVoteEntry.getCode(),
        {
            {sfAccount, soeREQUIRED},
            {sfTradingFee, soeDEFAULT},
            {sfVoteWeight, soeREQUIRED},
        });

    add(sfAuctionSlot.jsonName.c_str(),
        sfAuctionSlot.getCode(),
        {{sfAccount, soeREQUIRED},
         {sfExpiration, soeREQUIRED},
         {sfDiscountedFee, soeDEFAULT},
         {sfPrice, soeREQUIRED},
         {sfAuthAccounts, soeOPTIONAL}});

    add(sfXChainClaimAttestationCollectionElement.jsonName.c_str(),
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

    add(sfXChainCreateAccountAttestationCollectionElement.jsonName.c_str(),
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

    add(sfXChainClaimProofSig.jsonName.c_str(),
        sfXChainClaimProofSig.getCode(),
        {
            {sfAttestationSignerAccount, soeREQUIRED},
            {sfPublicKey, soeREQUIRED},
            {sfAmount, soeREQUIRED},
            {sfAttestationRewardAccount, soeREQUIRED},
            {sfWasLockingChainSend, soeREQUIRED},
            {sfDestination, soeOPTIONAL},
        });

    add(sfXChainCreateAccountProofSig.jsonName.c_str(),
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

    add(sfAuthAccount.jsonName.c_str(),
        sfAuthAccount.getCode(),
        {
            {sfAccount, soeREQUIRED},
        });

    add(sfPriceData.jsonName.c_str(),
        sfPriceData.getCode(),
        {
            {sfBaseAsset, soeREQUIRED},
            {sfQuoteAsset, soeREQUIRED},
            {sfAssetPrice, soeOPTIONAL},
            {sfScale, soeDEFAULT},
        });

    add(sfCredential.jsonName.c_str(),
        sfCredential.getCode(),
        {
            {sfIssuer, soeREQUIRED},
            {sfCredentialType, soeREQUIRED},
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
