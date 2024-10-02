//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#if !defined(OBJECT)
#error "undefined macro: OBJECT"
#endif

// clang-format off

/** A ledger object which describes an account.

    \sa keylet::account
 */
OBJECT(ltACCOUNT_ROOT, 0x0061, AccountRoot, ({
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
}))

/** A ledger object which contains a list of object identifiers.

    \sa keylet::page, keylet::quality, keylet::book, keylet::next and
        keylet::ownerDir
 */
OBJECT(ltDIR_NODE, 0x0064, DirectoryNode, ({
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
}))

/** A ledger object which describes an offer on the DEX.

    \sa keylet::offer
 */
OBJECT(ltOFFER, 0x006f, Offer, ({
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
}))

/** A ledger object which describes a bidirectional trust line.

    @note Per Vinnie Falco this should be renamed to ltTRUST_LINE

    \sa keylet::line
 */
OBJECT(ltRIPPLE_STATE, 0x0072, RippleState, ({
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
}))

/** A ledger object describing a single escrow.

    \sa keylet::escrow
 */
OBJECT(ltESCROW, 0x0075, Escrow, ({
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
}))

/** A ledger object that contains a list of ledger hashes.

    This type is used to store the ledger hashes which the protocol uses
    to implement skip lists that allow for efficient backwards (and, in
    theory, forward) forward iteration across large ledger ranges.

    \sa keylet::skip
 */
OBJECT(ltLEDGER_HASHES, 0x0068, LedgerHashes, ({
    {sfFirstLedgerSequence,  soeOPTIONAL},
    {sfLastLedgerSequence,   soeOPTIONAL},
    {sfHashes,               soeREQUIRED},
}))

/** The ledger object which lists details about amendments on the network.

    \note This is a singleton: only one such object exists in the ledger.

    \sa keylet::amendments
 */
OBJECT(ltAMENDMENTS, 0x0066, Amendments, ({
    {sfAmendments,           soeOPTIONAL},  // Enabled
    {sfMajorities,           soeOPTIONAL},
    {sfPreviousTxnID,        soeOPTIONAL},
    {sfPreviousTxnLgrSeq,    soeOPTIONAL},
}))

/** The ledger object which lists the network's fee settings.

    \note This is a singleton: only one such object exists in the ledger.

    \sa keylet::fees
 */
OBJECT(ltFEE_SETTINGS, 0x0073, FeeSettings, ({
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
}))

/** A ledger object which describes a ticket.

    \sa keylet::ticket
 */
OBJECT(ltTICKET, 0x0054, Ticket, ({
    {sfAccount,              soeREQUIRED},
    {sfOwnerNode,            soeREQUIRED},
    {sfTicketSequence,       soeREQUIRED},
    {sfPreviousTxnID,        soeREQUIRED},
    {sfPreviousTxnLgrSeq,    soeREQUIRED},
}))

/** A ledger object which contains a signer list for an account.

    \sa keylet::signers
 */
// All fields are soeREQUIRED because there is always a SignerEntries.
// If there are no SignerEntries the node is deleted.
OBJECT(ltSIGNER_LIST, 0x0053, SignerList, ({
    {sfOwnerNode,            soeREQUIRED},
    {sfSignerQuorum,         soeREQUIRED},
    {sfSignerEntries,        soeREQUIRED},
    {sfSignerListID,         soeREQUIRED},
    {sfPreviousTxnID,        soeREQUIRED},
    {sfPreviousTxnLgrSeq,    soeREQUIRED},
}))

/** A ledger object describing a single unidirectional XRP payment channel.

    \sa keylet::payChan
 */
OBJECT(ltPAYCHAN, 0x0078, PayChannel, ({
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
}))

/** A ledger object which describes a check.

    \sa keylet::check
 */
OBJECT(ltCHECK, 0x0043, Check, ({
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
}))

/** A ledger object which describes a deposit preauthorization.

    \sa keylet::depositPreauth
 */
OBJECT(ltDEPOSIT_PREAUTH, 0x0070, DepositPreauth, ({
    {sfAccount,              soeREQUIRED},
    {sfAuthorize,            soeREQUIRED},
    {sfOwnerNode,            soeREQUIRED},
    {sfPreviousTxnID,        soeREQUIRED},
    {sfPreviousTxnLgrSeq,    soeREQUIRED},
}))

/** The ledger object which tracks the current negative UNL state.

    \note This is a singleton: only one such object exists in the ledger.

    \sa keylet::negativeUNL
 */
OBJECT(ltNEGATIVE_UNL, 0x004e, NegativeUNL, ({
    {sfDisabledValidators,   soeOPTIONAL},
    {sfValidatorToDisable,   soeOPTIONAL},
    {sfValidatorToReEnable,  soeOPTIONAL},
    {sfPreviousTxnID,        soeOPTIONAL},
    {sfPreviousTxnLgrSeq,    soeOPTIONAL},
}))

/** A ledger object which contains a list of NFTs

    \sa keylet::nftpage_min, keylet::nftpage_max, keylet::nftpage
 */
OBJECT(ltNFTOKEN_PAGE, 0x0050, NFTokenPage, ({
    {sfPreviousPageMin,      soeOPTIONAL},
    {sfNextPageMin,          soeOPTIONAL},
    {sfNFTokens,             soeREQUIRED},
    {sfPreviousTxnID,        soeREQUIRED},
    {sfPreviousTxnLgrSeq,    soeREQUIRED}
}))

/** A ledger object which identifies an offer to buy or sell an NFT.

    \sa keylet::nftoffer
 */
OBJECT(ltNFTOKEN_OFFER, 0x0037, NFTokenOffer, ({
    {sfOwner,                soeREQUIRED},
    {sfNFTokenID,            soeREQUIRED},
    {sfAmount,               soeREQUIRED},
    {sfOwnerNode,            soeREQUIRED},
    {sfNFTokenOfferNode,     soeREQUIRED},
    {sfDestination,          soeOPTIONAL},
    {sfExpiration,           soeOPTIONAL},
    {sfPreviousTxnID,        soeREQUIRED},
    {sfPreviousTxnLgrSeq,    soeREQUIRED}
}))

/** The ledger object which tracks the AMM.

   \sa keylet::amm
*/
OBJECT(ltAMM, 0x0079, AMM, ({
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
}))

/** The ledger object which lists details about sidechains.

    \sa keylet::bridge
*/
OBJECT(ltBRIDGE, 0x0069, Bridge, ({
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
}))

/** A claim id for a cross chain transaction.

    \sa keylet::xChainClaimID
*/
OBJECT(ltXCHAIN_OWNED_CLAIM_ID, 0x0071, XChainOwnedClaimID, ({
    {sfAccount,                 soeREQUIRED},
    {sfXChainBridge,            soeREQUIRED},
    {sfXChainClaimID,           soeREQUIRED},
    {sfOtherChainSource,        soeREQUIRED},
    {sfXChainClaimAttestations, soeREQUIRED},
    {sfSignatureReward,         soeREQUIRED},
    {sfOwnerNode,               soeREQUIRED},
    {sfPreviousTxnID,           soeREQUIRED},
    {sfPreviousTxnLgrSeq,       soeREQUIRED}
}))

/** A claim id for a cross chain create account transaction.

    \sa keylet::xChainCreateAccountClaimID
*/
OBJECT(ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID, 0x0074, XChainOwnedCreateAccountClaimID, ({
    {sfAccount,                         soeREQUIRED},
    {sfXChainBridge,                    soeREQUIRED},
    {sfXChainAccountCreateCount,        soeREQUIRED},
    {sfXChainCreateAccountAttestations, soeREQUIRED},
    {sfOwnerNode,                       soeREQUIRED},
    {sfPreviousTxnID,                   soeREQUIRED},
    {sfPreviousTxnLgrSeq,               soeREQUIRED}
}))

/** The ledger object which tracks the DID.

   \sa keylet::did
*/
OBJECT(ltDID, 0x0049, DID, ({
    {sfAccount,              soeREQUIRED},
    {sfDIDDocument,          soeOPTIONAL},
    {sfURI,                  soeOPTIONAL},
    {sfData,                 soeOPTIONAL},
    {sfOwnerNode,            soeREQUIRED},
    {sfPreviousTxnID,        soeREQUIRED},
    {sfPreviousTxnLgrSeq,    soeREQUIRED}
}))

/** A ledger object which tracks Oracle
    \sa keylet::oracle
 */
OBJECT(ltORACLE, 0x0080, Oracle, ({
    {sfOwner,                soeREQUIRED},
    {sfProvider,             soeREQUIRED},
    {sfPriceDataSeries,      soeREQUIRED},
    {sfAssetClass,           soeREQUIRED},
    {sfLastUpdateTime,       soeREQUIRED},
    {sfURI,                  soeOPTIONAL},
    {sfOwnerNode,            soeREQUIRED},
    {sfPreviousTxnID,        soeREQUIRED},
    {sfPreviousTxnLgrSeq,    soeREQUIRED}
}))
