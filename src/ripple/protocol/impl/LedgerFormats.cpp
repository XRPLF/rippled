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

#include <ripple/protocol/LedgerFormats.h>
#include <ripple/protocol/jss.h>
#include <utility>

namespace ripple {

LedgerFormats::LedgerFormats()
{
    // clang-format off
    // Fields shared by all ledger formats:
    static const std::initializer_list<SOElement> commonFields{
        {sfLedgerIndex,              soeOPTIONAL},
        {sfLedgerEntryType,          soeREQUIRED},
        {sfFlags,                    soeREQUIRED},
    };

    add(jss::AccountRoot,
        ltACCOUNT_ROOT,
        {
            {sfAccount,              soeREQUIRED},
            {sfSequence,             soeREQUIRED},
            {sfBalance,              soeREQUIRED},
            {sfOwnerCount,           soeREQUIRED},
            {sfPreviousTxnID,        soeREQUIRED},
            {sfPreviousTxnLgrSeq,    soeREQUIRED},
            {sfAccountTxnID,         soeOPTIONAL},
            {sfRegularKey,           soeOPTIONAL},
            {sfEmailHash,            soeOPTIONAL},
            {sfWalletLocator,        soeOPTIONAL},
            {sfWalletSize,           soeOPTIONAL},
            {sfMessageKey,           soeOPTIONAL},
            {sfTransferRate,         soeOPTIONAL},
            {sfDomain,               soeOPTIONAL},
            {sfTickSize,             soeOPTIONAL},
            {sfTicketCount,          soeOPTIONAL},
            {sfNFTokenMinter,        soeOPTIONAL},
            {sfMintedNFTokens,       soeDEFAULT},
            {sfBurnedNFTokens,       soeDEFAULT},
            {sfFirstNFTokenSequence, soeOPTIONAL},
            {sfAMMID,                soeOPTIONAL},
        },
        commonFields);

    add(jss::DirectoryNode,
        ltDIR_NODE,
        {
            {sfOwner,                soeOPTIONAL},  // for owner directories
            {sfTakerPaysCurrency,    soeOPTIONAL},  // order book directories
            {sfTakerPaysIssuer,      soeOPTIONAL},  // order book directories
            {sfTakerGetsCurrency,    soeOPTIONAL},  // order book directories
            {sfTakerGetsIssuer,      soeOPTIONAL},  // order book directories
            {sfExchangeRate,         soeOPTIONAL},  // order book directories
            {sfIndexes,              soeREQUIRED},
            {sfRootIndex,            soeREQUIRED},
            {sfIndexNext,            soeOPTIONAL},
            {sfIndexPrevious,        soeOPTIONAL},
            {sfNFTokenID,            soeOPTIONAL},
            {sfPreviousTxnID,        soeOPTIONAL},
            {sfPreviousTxnLgrSeq,    soeOPTIONAL},
        },
        commonFields);

    add(jss::Offer,
        ltOFFER,
        {
            {sfAccount,              soeREQUIRED},
            {sfSequence,             soeREQUIRED},
            {sfTakerPays,            soeREQUIRED},
            {sfTakerGets,            soeREQUIRED},
            {sfBookDirectory,        soeREQUIRED},
            {sfBookNode,             soeREQUIRED},
            {sfOwnerNode,            soeREQUIRED},
            {sfPreviousTxnID,        soeREQUIRED},
            {sfPreviousTxnLgrSeq,    soeREQUIRED},
            {sfExpiration,           soeOPTIONAL},
        },
        commonFields);

    add(jss::RippleState,
        ltRIPPLE_STATE,
        {
            {sfBalance,              soeREQUIRED},
            {sfLowLimit,             soeREQUIRED},
            {sfHighLimit,            soeREQUIRED},
            {sfPreviousTxnID,        soeREQUIRED},
            {sfPreviousTxnLgrSeq,    soeREQUIRED},
            {sfLowNode,              soeOPTIONAL},
            {sfLowQualityIn,         soeOPTIONAL},
            {sfLowQualityOut,        soeOPTIONAL},
            {sfHighNode,             soeOPTIONAL},
            {sfHighQualityIn,        soeOPTIONAL},
            {sfHighQualityOut,       soeOPTIONAL},
        },
        commonFields);

    add(jss::Escrow,
        ltESCROW,
        {
            {sfAccount,              soeREQUIRED},
            {sfDestination,          soeREQUIRED},
            {sfAmount,               soeREQUIRED},
            {sfCondition,            soeOPTIONAL},
            {sfCancelAfter,          soeOPTIONAL},
            {sfFinishAfter,          soeOPTIONAL},
            {sfSourceTag,            soeOPTIONAL},
            {sfDestinationTag,       soeOPTIONAL},
            {sfOwnerNode,            soeREQUIRED},
            {sfPreviousTxnID,        soeREQUIRED},
            {sfPreviousTxnLgrSeq,    soeREQUIRED},
            {sfDestinationNode,      soeOPTIONAL},
        },
        commonFields);

    add(jss::LedgerHashes,
        ltLEDGER_HASHES,
        {
            {sfFirstLedgerSequence,  soeOPTIONAL},
            {sfLastLedgerSequence,   soeOPTIONAL},
            {sfHashes,               soeREQUIRED},
        },
        commonFields);

    add(jss::Amendments,
        ltAMENDMENTS,
        {
            {sfAmendments,           soeOPTIONAL},  // Enabled
            {sfMajorities,           soeOPTIONAL},
            {sfPreviousTxnID,        soeOPTIONAL},
            {sfPreviousTxnLgrSeq,    soeOPTIONAL},
        },
        commonFields);

    add(jss::FeeSettings,
        ltFEE_SETTINGS,
        {
            // Old version uses raw numbers
            {sfBaseFee,                soeOPTIONAL},
            {sfReferenceFeeUnits,      soeOPTIONAL},
            {sfReserveBase,            soeOPTIONAL},
            {sfReserveIncrement,       soeOPTIONAL},
            // New version uses Amounts
            {sfBaseFeeDrops,           soeOPTIONAL},
            {sfReserveBaseDrops,       soeOPTIONAL},
            {sfReserveIncrementDrops,  soeOPTIONAL},
            {sfPreviousTxnID,          soeOPTIONAL},
            {sfPreviousTxnLgrSeq,      soeOPTIONAL},
        },
        commonFields);

    add(jss::Ticket,
        ltTICKET,
        {
            {sfAccount,              soeREQUIRED},
            {sfOwnerNode,            soeREQUIRED},
            {sfTicketSequence,       soeREQUIRED},
            {sfPreviousTxnID,        soeREQUIRED},
            {sfPreviousTxnLgrSeq,    soeREQUIRED},
        },
        commonFields);

    // All fields are soeREQUIRED because there is always a
    // SignerEntries.  If there are no SignerEntries the node is deleted.
    add(jss::SignerList,
        ltSIGNER_LIST,
        {
            {sfOwnerNode,            soeREQUIRED},
            {sfSignerQuorum,         soeREQUIRED},
            {sfSignerEntries,        soeREQUIRED},
            {sfSignerListID,         soeREQUIRED},
            {sfPreviousTxnID,        soeREQUIRED},
            {sfPreviousTxnLgrSeq,    soeREQUIRED},
        },
        commonFields);

    add(jss::PayChannel,
        ltPAYCHAN,
        {
            {sfAccount,              soeREQUIRED},
            {sfDestination,          soeREQUIRED},
            {sfAmount,               soeREQUIRED},
            {sfBalance,              soeREQUIRED},
            {sfPublicKey,            soeREQUIRED},
            {sfSettleDelay,          soeREQUIRED},
            {sfExpiration,           soeOPTIONAL},
            {sfCancelAfter,          soeOPTIONAL},
            {sfSourceTag,            soeOPTIONAL},
            {sfDestinationTag,       soeOPTIONAL},
            {sfOwnerNode,            soeREQUIRED},
            {sfPreviousTxnID,        soeREQUIRED},
            {sfPreviousTxnLgrSeq,    soeREQUIRED},
            {sfDestinationNode,      soeOPTIONAL},
        },
        commonFields);

    add(jss::Check,
        ltCHECK,
        {
            {sfAccount,              soeREQUIRED},
            {sfDestination,          soeREQUIRED},
            {sfSendMax,              soeREQUIRED},
            {sfSequence,             soeREQUIRED},
            {sfOwnerNode,            soeREQUIRED},
            {sfDestinationNode,      soeREQUIRED},
            {sfExpiration,           soeOPTIONAL},
            {sfInvoiceID,            soeOPTIONAL},
            {sfSourceTag,            soeOPTIONAL},
            {sfDestinationTag,       soeOPTIONAL},
            {sfPreviousTxnID,        soeREQUIRED},
            {sfPreviousTxnLgrSeq,    soeREQUIRED},
        },
        commonFields);

    add(jss::DepositPreauth,
        ltDEPOSIT_PREAUTH,
        {
            {sfAccount,              soeREQUIRED},
            {sfAuthorize,            soeREQUIRED},
            {sfOwnerNode,            soeREQUIRED},
            {sfPreviousTxnID,        soeREQUIRED},
            {sfPreviousTxnLgrSeq,    soeREQUIRED},
        },
        commonFields);

    add(jss::NegativeUNL,
        ltNEGATIVE_UNL,
        {
            {sfDisabledValidators,   soeOPTIONAL},
            {sfValidatorToDisable,   soeOPTIONAL},
            {sfValidatorToReEnable,  soeOPTIONAL},
            {sfPreviousTxnID,        soeOPTIONAL},
            {sfPreviousTxnLgrSeq,    soeOPTIONAL},
        },
        commonFields);

    add(jss::NFTokenPage,
        ltNFTOKEN_PAGE,
        {
            {sfPreviousPageMin,      soeOPTIONAL},
            {sfNextPageMin,          soeOPTIONAL},
            {sfNFTokens,             soeREQUIRED},
            {sfPreviousTxnID,        soeREQUIRED},
            {sfPreviousTxnLgrSeq,    soeREQUIRED}
        },
        commonFields);

    add(jss::NFTokenOffer,
        ltNFTOKEN_OFFER,
        {
            {sfOwner,                soeREQUIRED},
            {sfNFTokenID,            soeREQUIRED},
            {sfAmount,               soeREQUIRED},
            {sfOwnerNode,            soeREQUIRED},
            {sfNFTokenOfferNode,     soeREQUIRED},
            {sfDestination,          soeOPTIONAL},
            {sfExpiration,           soeOPTIONAL},
            {sfPreviousTxnID,        soeREQUIRED},
            {sfPreviousTxnLgrSeq,    soeREQUIRED}
        },
        commonFields);

    add(jss::AMM,
        ltAMM,
        {
            {sfAccount,              soeREQUIRED},
            {sfTradingFee,           soeDEFAULT},
            {sfVoteSlots,            soeOPTIONAL},
            {sfAuctionSlot,          soeOPTIONAL},
            {sfLPTokenBalance,       soeREQUIRED},
            {sfAsset,                soeREQUIRED},
            {sfAsset2,               soeREQUIRED},
            {sfOwnerNode,            soeREQUIRED},
            {sfPreviousTxnID,        soeOPTIONAL},
            {sfPreviousTxnLgrSeq,    soeOPTIONAL},
        },
        commonFields);

    add(jss::Bridge,
        ltBRIDGE,
        {
            {sfAccount,                  soeREQUIRED},
            {sfSignatureReward,          soeREQUIRED},
            {sfMinAccountCreateAmount,   soeOPTIONAL},
            {sfXChainBridge,             soeREQUIRED},
            {sfXChainClaimID,            soeREQUIRED},
            {sfXChainAccountCreateCount, soeREQUIRED},
            {sfXChainAccountClaimCount,  soeREQUIRED},
            {sfOwnerNode,                soeREQUIRED},
            {sfPreviousTxnID,            soeREQUIRED},
            {sfPreviousTxnLgrSeq,        soeREQUIRED}
        },
        commonFields);

    add(jss::XChainOwnedClaimID,
        ltXCHAIN_OWNED_CLAIM_ID,
        {
            {sfAccount,                 soeREQUIRED},
            {sfXChainBridge,            soeREQUIRED},
            {sfXChainClaimID,           soeREQUIRED},
            {sfOtherChainSource,        soeREQUIRED},
            {sfXChainClaimAttestations, soeREQUIRED},
            {sfSignatureReward,         soeREQUIRED},
            {sfOwnerNode,               soeREQUIRED},
            {sfPreviousTxnID,           soeREQUIRED},
            {sfPreviousTxnLgrSeq,       soeREQUIRED}
        },
        commonFields);

    add(jss::XChainOwnedCreateAccountClaimID,
        ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID,
        {
            {sfAccount,                         soeREQUIRED},
            {sfXChainBridge,                    soeREQUIRED},
            {sfXChainAccountCreateCount,        soeREQUIRED},
            {sfXChainCreateAccountAttestations, soeREQUIRED},
            {sfOwnerNode,                       soeREQUIRED},
            {sfPreviousTxnID,                   soeREQUIRED},
            {sfPreviousTxnLgrSeq,               soeREQUIRED}
        },
        commonFields);

    add(jss::DID,
        ltDID,
        {
            {sfAccount,              soeREQUIRED},
            {sfDIDDocument,          soeOPTIONAL},
            {sfURI,                  soeOPTIONAL},
            {sfData,          soeOPTIONAL},
            {sfOwnerNode,            soeREQUIRED},
            {sfPreviousTxnID,        soeREQUIRED},
            {sfPreviousTxnLgrSeq,    soeREQUIRED}
        },
        commonFields);

    add(jss::Oracle,
        ltORACLE,
        {
            {sfOwner,               soeREQUIRED},
            {sfProvider,            soeREQUIRED},
            {sfPriceDataSeries,     soeREQUIRED},
            {sfAssetClass,          soeREQUIRED},
            {sfLastUpdateTime,      soeREQUIRED},
            {sfURI,                 soeOPTIONAL},
            {sfOwnerNode,           soeREQUIRED},
            {sfPreviousTxnID,       soeREQUIRED},
            {sfPreviousTxnLgrSeq,   soeREQUIRED}
        },
        commonFields);

    // clang-format on
}

LedgerFormats const&
LedgerFormats::getInstance()
{
    static LedgerFormats instance;
    return instance;
}

}  // namespace ripple
