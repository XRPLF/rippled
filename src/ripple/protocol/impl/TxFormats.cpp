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

#include <ripple/protocol/TxFormats.h>

#include <ripple/protocol/SField.h>
#include <ripple/protocol/SOTemplate.h>
#include <ripple/protocol/jss.h>

namespace ripple {

TxFormats::TxFormats()
{
    // Fields shared by all txFormats:
    static const std::initializer_list<SOElement> commonFields{
        {sfTransactionType, soeREQUIRED},
        {sfFlags, soeOPTIONAL},
        {sfSourceTag, soeOPTIONAL},
        {sfAccount, soeREQUIRED},
        {sfSequence, soeREQUIRED},
        {sfPreviousTxnID, soeOPTIONAL},  // emulate027
        {sfLastLedgerSequence, soeOPTIONAL},
        {sfAccountTxnID, soeOPTIONAL},
        {sfFee, soeREQUIRED},
        {sfOperationLimit, soeOPTIONAL},
        {sfMemos, soeOPTIONAL},
        {sfSigningPubKey, soeREQUIRED},
        {sfTicketSequence, soeOPTIONAL},
        {sfTxnSignature, soeOPTIONAL},
        {sfSigners, soeOPTIONAL},  // submit_multisigned
        {sfNetworkID, soeOPTIONAL},
    };

    add(jss::AccountSet,
        ttACCOUNT_SET,
        {
            {sfEmailHash, soeOPTIONAL},
            {sfWalletLocator, soeOPTIONAL},
            {sfWalletSize, soeOPTIONAL},
            {sfMessageKey, soeOPTIONAL},
            {sfDomain, soeOPTIONAL},
            {sfTransferRate, soeOPTIONAL},
            {sfSetFlag, soeOPTIONAL},
            {sfClearFlag, soeOPTIONAL},
            {sfTickSize, soeOPTIONAL},
            {sfNFTokenMinter, soeOPTIONAL},
        },
        commonFields);

    add(jss::TrustSet,
        ttTRUST_SET,
        {
            {sfLimitAmount, soeOPTIONAL},
            {sfQualityIn, soeOPTIONAL},
            {sfQualityOut, soeOPTIONAL},
        },
        commonFields);

    add(jss::OfferCreate,
        ttOFFER_CREATE,
        {
            {sfTakerPays, soeREQUIRED},
            {sfTakerGets, soeREQUIRED},
            {sfExpiration, soeOPTIONAL},
            {sfOfferSequence, soeOPTIONAL},
        },
        commonFields);

    add(jss::AMMCreate,
        ttAMM_CREATE,
        {
            {sfAmount, soeREQUIRED},
            {sfAmount2, soeREQUIRED},
            {sfTradingFee, soeREQUIRED},
        },
        commonFields);

    add(jss::AMMDeposit,
        ttAMM_DEPOSIT,
        {
            {sfAsset, soeREQUIRED},
            {sfAsset2, soeREQUIRED},
            {sfAmount, soeOPTIONAL},
            {sfAmount2, soeOPTIONAL},
            {sfEPrice, soeOPTIONAL},
            {sfLPTokenOut, soeOPTIONAL},
            {sfTradingFee, soeOPTIONAL},
        },
        commonFields);

    add(jss::AMMWithdraw,
        ttAMM_WITHDRAW,
        {
            {sfAsset, soeREQUIRED},
            {sfAsset2, soeREQUIRED},
            {sfAmount, soeOPTIONAL},
            {sfAmount2, soeOPTIONAL},
            {sfEPrice, soeOPTIONAL},
            {sfLPTokenIn, soeOPTIONAL},
        },
        commonFields);

    add(jss::AMMVote,
        ttAMM_VOTE,
        {
            {sfAsset, soeREQUIRED},
            {sfAsset2, soeREQUIRED},
            {sfTradingFee, soeREQUIRED},
        },
        commonFields);

    add(jss::AMMBid,
        ttAMM_BID,
        {
            {sfAsset, soeREQUIRED},
            {sfAsset2, soeREQUIRED},
            {sfBidMin, soeOPTIONAL},
            {sfBidMax, soeOPTIONAL},
            {sfAuthAccounts, soeOPTIONAL},
        },
        commonFields);

    add(jss::AMMDelete,
        ttAMM_DELETE,
        {
            {sfAsset, soeREQUIRED},
            {sfAsset2, soeREQUIRED},
        },
        commonFields);

    add(jss::OfferCancel,
        ttOFFER_CANCEL,
        {
            {sfOfferSequence, soeREQUIRED},
        },
        commonFields);

    add(jss::SetRegularKey,
        ttREGULAR_KEY_SET,
        {
            {sfRegularKey, soeOPTIONAL},
        },
        commonFields);

    add(jss::Payment,
        ttPAYMENT,
        {
            {sfDestination, soeREQUIRED},
            {sfAmount, soeREQUIRED},
            {sfSendMax, soeOPTIONAL},
            {sfPaths, soeDEFAULT},
            {sfInvoiceID, soeOPTIONAL},
            {sfDestinationTag, soeOPTIONAL},
            {sfDeliverMin, soeOPTIONAL},
        },
        commonFields);

    add(jss::EscrowCreate,
        ttESCROW_CREATE,
        {
            {sfDestination, soeREQUIRED},
            {sfAmount, soeREQUIRED},
            {sfCondition, soeOPTIONAL},
            {sfCancelAfter, soeOPTIONAL},
            {sfFinishAfter, soeOPTIONAL},
            {sfDestinationTag, soeOPTIONAL},
        },
        commonFields);

    add(jss::EscrowFinish,
        ttESCROW_FINISH,
        {
            {sfOwner, soeREQUIRED},
            {sfOfferSequence, soeREQUIRED},
            {sfFulfillment, soeOPTIONAL},
            {sfCondition, soeOPTIONAL},
        },
        commonFields);

    add(jss::EscrowCancel,
        ttESCROW_CANCEL,
        {
            {sfOwner, soeREQUIRED},
            {sfOfferSequence, soeREQUIRED},
        },
        commonFields);

    add(jss::EnableAmendment,
        ttAMENDMENT,
        {
            {sfLedgerSequence, soeREQUIRED},
            {sfAmendment, soeREQUIRED},
        },
        commonFields);

    add(jss::SetFee,
        ttFEE,
        {
            {sfLedgerSequence, soeOPTIONAL},
            // Old version uses raw numbers
            {sfBaseFee, soeOPTIONAL},
            {sfReferenceFeeUnits, soeOPTIONAL},
            {sfReserveBase, soeOPTIONAL},
            {sfReserveIncrement, soeOPTIONAL},
            // New version uses Amounts
            {sfBaseFeeDrops, soeOPTIONAL},
            {sfReserveBaseDrops, soeOPTIONAL},
            {sfReserveIncrementDrops, soeOPTIONAL},
        },
        commonFields);

    add(jss::UNLModify,
        ttUNL_MODIFY,
        {
            {sfUNLModifyDisabling, soeREQUIRED},
            {sfLedgerSequence, soeREQUIRED},
            {sfUNLModifyValidator, soeREQUIRED},
        },
        commonFields);

    add(jss::TicketCreate,
        ttTICKET_CREATE,
        {
            {sfTicketCount, soeREQUIRED},
        },
        commonFields);

    // The SignerEntries are optional because a SignerList is deleted by
    // setting the SignerQuorum to zero and omitting SignerEntries.
    add(jss::SignerListSet,
        ttSIGNER_LIST_SET,
        {
            {sfSignerQuorum, soeREQUIRED},
            {sfSignerEntries, soeOPTIONAL},
        },
        commonFields);

    add(jss::PaymentChannelCreate,
        ttPAYCHAN_CREATE,
        {
            {sfDestination, soeREQUIRED},
            {sfAmount, soeREQUIRED},
            {sfSettleDelay, soeREQUIRED},
            {sfPublicKey, soeREQUIRED},
            {sfCancelAfter, soeOPTIONAL},
            {sfDestinationTag, soeOPTIONAL},
        },
        commonFields);

    add(jss::PaymentChannelFund,
        ttPAYCHAN_FUND,
        {
            {sfChannel, soeREQUIRED},
            {sfAmount, soeREQUIRED},
            {sfExpiration, soeOPTIONAL},
        },
        commonFields);

    add(jss::PaymentChannelClaim,
        ttPAYCHAN_CLAIM,
        {
            {sfChannel, soeREQUIRED},
            {sfAmount, soeOPTIONAL},
            {sfBalance, soeOPTIONAL},
            {sfSignature, soeOPTIONAL},
            {sfPublicKey, soeOPTIONAL},
        },
        commonFields);

    add(jss::CheckCreate,
        ttCHECK_CREATE,
        {
            {sfDestination, soeREQUIRED},
            {sfSendMax, soeREQUIRED},
            {sfExpiration, soeOPTIONAL},
            {sfDestinationTag, soeOPTIONAL},
            {sfInvoiceID, soeOPTIONAL},
        },
        commonFields);

    add(jss::CheckCash,
        ttCHECK_CASH,
        {
            {sfCheckID, soeREQUIRED},
            {sfAmount, soeOPTIONAL},
            {sfDeliverMin, soeOPTIONAL},
        },
        commonFields);

    add(jss::CheckCancel,
        ttCHECK_CANCEL,
        {
            {sfCheckID, soeREQUIRED},
        },
        commonFields);

    add(jss::AccountDelete,
        ttACCOUNT_DELETE,
        {
            {sfDestination, soeREQUIRED},
            {sfDestinationTag, soeOPTIONAL},
        },
        commonFields);

    add(jss::DepositPreauth,
        ttDEPOSIT_PREAUTH,
        {
            {sfAuthorize, soeOPTIONAL},
            {sfUnauthorize, soeOPTIONAL},
        },
        commonFields);

    add(jss::NFTokenMint,
        ttNFTOKEN_MINT,
        {
            {sfNFTokenTaxon, soeREQUIRED},
            {sfTransferFee, soeOPTIONAL},
            {sfIssuer, soeOPTIONAL},
            {sfURI, soeOPTIONAL},
            {sfAmount, soeOPTIONAL},
            {sfDestination, soeOPTIONAL},
            {sfExpiration, soeOPTIONAL},
        },
        commonFields);

    add(jss::NFTokenBurn,
        ttNFTOKEN_BURN,
        {
            {sfNFTokenID, soeREQUIRED},
            {sfOwner, soeOPTIONAL},
        },
        commonFields);

    add(jss::NFTokenCreateOffer,
        ttNFTOKEN_CREATE_OFFER,
        {
            {sfNFTokenID, soeREQUIRED},
            {sfAmount, soeREQUIRED},
            {sfDestination, soeOPTIONAL},
            {sfOwner, soeOPTIONAL},
            {sfExpiration, soeOPTIONAL},
        },
        commonFields);

    add(jss::NFTokenCancelOffer,
        ttNFTOKEN_CANCEL_OFFER,
        {
            {sfNFTokenOffers, soeREQUIRED},
        },
        commonFields);

    add(jss::NFTokenAcceptOffer,
        ttNFTOKEN_ACCEPT_OFFER,
        {
            {sfNFTokenBuyOffer, soeOPTIONAL},
            {sfNFTokenSellOffer, soeOPTIONAL},
            {sfNFTokenBrokerFee, soeOPTIONAL},
        },
        commonFields);

    add(jss::Clawback,
        ttCLAWBACK,
        {
            {sfAmount, soeREQUIRED},
        },
        commonFields);

    add(jss::XChainCreateBridge,
        ttXCHAIN_CREATE_BRIDGE,
        {
            {sfXChainBridge, soeREQUIRED},
            {sfSignatureReward, soeREQUIRED},
            {sfMinAccountCreateAmount, soeOPTIONAL},
        },
        commonFields);

    add(jss::XChainModifyBridge,
        ttXCHAIN_MODIFY_BRIDGE,
        {
            {sfXChainBridge, soeREQUIRED},
            {sfSignatureReward, soeOPTIONAL},
            {sfMinAccountCreateAmount, soeOPTIONAL},
        },
        commonFields);

    add(jss::XChainCreateClaimID,
        ttXCHAIN_CREATE_CLAIM_ID,
        {
            {sfXChainBridge, soeREQUIRED},
            {sfSignatureReward, soeREQUIRED},
            {sfOtherChainSource, soeREQUIRED},
        },
        commonFields);

    add(jss::XChainCommit,
        ttXCHAIN_COMMIT,
        {
            {sfXChainBridge, soeREQUIRED},
            {sfXChainClaimID, soeREQUIRED},
            {sfAmount, soeREQUIRED},
            {sfOtherChainDestination, soeOPTIONAL},
        },
        commonFields);

    add(jss::XChainClaim,
        ttXCHAIN_CLAIM,
        {
            {sfXChainBridge, soeREQUIRED},
            {sfXChainClaimID, soeREQUIRED},
            {sfDestination, soeREQUIRED},
            {sfDestinationTag, soeOPTIONAL},
            {sfAmount, soeREQUIRED},
        },
        commonFields);

    add(jss::XChainAddClaimAttestation,
        ttXCHAIN_ADD_CLAIM_ATTESTATION,
        {
            {sfXChainBridge, soeREQUIRED},

            {sfAttestationSignerAccount, soeREQUIRED},
            {sfPublicKey, soeREQUIRED},
            {sfSignature, soeREQUIRED},
            {sfOtherChainSource, soeREQUIRED},
            {sfAmount, soeREQUIRED},
            {sfAttestationRewardAccount, soeREQUIRED},
            {sfWasLockingChainSend, soeREQUIRED},

            {sfXChainClaimID, soeREQUIRED},
            {sfDestination, soeOPTIONAL},
        },
        commonFields);

    add(jss::XChainAddAccountCreateAttestation,
        ttXCHAIN_ADD_ACCOUNT_CREATE_ATTESTATION,
        {
            {sfXChainBridge, soeREQUIRED},

            {sfAttestationSignerAccount, soeREQUIRED},
            {sfPublicKey, soeREQUIRED},
            {sfSignature, soeREQUIRED},
            {sfOtherChainSource, soeREQUIRED},
            {sfAmount, soeREQUIRED},
            {sfAttestationRewardAccount, soeREQUIRED},
            {sfWasLockingChainSend, soeREQUIRED},

            {sfXChainAccountCreateCount, soeREQUIRED},
            {sfDestination, soeREQUIRED},
            {sfSignatureReward, soeREQUIRED},
        },
        commonFields);

    add(jss::XChainAccountCreateCommit,
        ttXCHAIN_ACCOUNT_CREATE_COMMIT,
        {
            {sfXChainBridge, soeREQUIRED},
            {sfDestination, soeREQUIRED},
            {sfAmount, soeREQUIRED},
            {sfSignatureReward, soeREQUIRED},
        },
        commonFields);

    add(jss::DIDSet,
        ttDID_SET,
        {
            {sfDIDDocument, soeOPTIONAL},
            {sfURI, soeOPTIONAL},
            {sfData, soeOPTIONAL},
        },
        commonFields);

    add(jss::DIDDelete, ttDID_DELETE, {}, commonFields);

    add(jss::OracleSet,
        ttORACLE_SET,
        {
            {sfOracleDocumentID, soeREQUIRED},
            {sfProvider, soeOPTIONAL},
            {sfURI, soeOPTIONAL},
            {sfAssetClass, soeOPTIONAL},
            {sfLastUpdateTime, soeREQUIRED},
            {sfPriceDataSeries, soeREQUIRED},
        },
        commonFields);

    add(jss::OracleDelete,
        ttORACLE_DELETE,
        {
            {sfOracleDocumentID, soeREQUIRED},
        },
        commonFields);
}

TxFormats const&
TxFormats::getInstance()
{
    static TxFormats const instance;
    return instance;
}

}  // namespace ripple
