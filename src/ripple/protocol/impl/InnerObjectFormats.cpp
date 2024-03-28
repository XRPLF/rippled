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

#include <ripple/protocol/InnerObjectFormats.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/SOTemplate.h>

namespace ripple {

std::map<std::uint16_t, PluginInnerObjectFormat>* pluginInnerObjectFormatsPtr;

void
registerPluginInnerObjectFormats(
    std::map<std::uint16_t, PluginInnerObjectFormat>* pluginInnerObjectFormats)
{
    pluginInnerObjectFormatsPtr = pluginInnerObjectFormats;
    InnerObjectFormats::reset();
}

void
InnerObjectFormats::initialize()
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

    if (pluginInnerObjectFormatsPtr != nullptr)
    {
        for (auto& e : *pluginInnerObjectFormatsPtr)
        {
            add(e.second.name.c_str(), e.first, e.second.uniqueFields);
        }
    }
}

InnerObjectFormats&
InnerObjectFormats::getInstanceHelper()
{
    static InnerObjectFormats instance;
    if (instance.cleared)
    {
        try
        {
            instance.initialize();
        }
        catch (...)
        {
            // If initialization errors, it shouldn't reset
            instance.cleared = false;
            throw;
        }
        instance.cleared = false;
    }
    return instance;
}

void
InnerObjectFormats::reset()
{
    auto& instance = getInstanceHelper();
    instance.clear();
    instance.cleared = true;
}

InnerObjectFormats const&
InnerObjectFormats::getInstance()
{
    return getInstanceHelper();
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
