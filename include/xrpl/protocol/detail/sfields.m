//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED AS IS AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#if !defined(UNTYPED_SFIELD)
#error "undefined macro: UNTYPED_SFIELD"
#endif
#if !defined(TYPED_SFIELD)
#error "undefined macro: TYPED_SFIELD"
#endif

// untyped
UNTYPED_SFIELD(sfLedgerEntry,            LEDGERENTRY, 257)
UNTYPED_SFIELD(sfTransaction,            TRANSACTION, 257)
UNTYPED_SFIELD(sfValidation,             VALIDATION,  257)
UNTYPED_SFIELD(sfMetadata,               METADATA,    257)

// 8-bit integers (common)
TYPED_SFIELD(sfCloseResolution,          UINT8,      1)
TYPED_SFIELD(sfMethod,                   UINT8,      2)
TYPED_SFIELD(sfTransactionResult,        UINT8,      3)
TYPED_SFIELD(sfScale,                    UINT8,      4)

// 8-bit integers (uncommon)
TYPED_SFIELD(sfTickSize,                 UINT8,     16)
TYPED_SFIELD(sfUNLModifyDisabling,       UINT8,     17)
TYPED_SFIELD(sfHookResult,               UINT8,     18)
TYPED_SFIELD(sfWasLockingChainSend,      UINT8,     19)

// 16-bit integers (common)
TYPED_SFIELD(sfLedgerEntryType,          UINT16,     1, SField::sMD_Never)
TYPED_SFIELD(sfTransactionType,          UINT16,     2)
TYPED_SFIELD(sfSignerWeight,             UINT16,     3)
TYPED_SFIELD(sfTransferFee,              UINT16,     4)
TYPED_SFIELD(sfTradingFee,               UINT16,     5)
TYPED_SFIELD(sfDiscountedFee,            UINT16,     6)

// 16-bit integers (uncommon)
TYPED_SFIELD(sfVersion,                  UINT16,    16)
TYPED_SFIELD(sfHookStateChangeCount,     UINT16,    17)
TYPED_SFIELD(sfHookEmitCount,            UINT16,    18)
TYPED_SFIELD(sfHookExecutionIndex,       UINT16,    19)
TYPED_SFIELD(sfHookApiVersion,           UINT16,    20)
TYPED_SFIELD(sfLedgerFixType,            UINT16,    21)

// 32-bit integers (common)
TYPED_SFIELD(sfNetworkID,                UINT32,     1)
TYPED_SFIELD(sfFlags,                    UINT32,     2)
TYPED_SFIELD(sfSourceTag,                UINT32,     3)
TYPED_SFIELD(sfSequence,                 UINT32,     4)
TYPED_SFIELD(sfPreviousTxnLgrSeq,        UINT32,     5, SField::sMD_DeleteFinal)
TYPED_SFIELD(sfLedgerSequence,           UINT32,     6)
TYPED_SFIELD(sfCloseTime,                UINT32,     7)
TYPED_SFIELD(sfParentCloseTime,          UINT32,     8)
TYPED_SFIELD(sfSigningTime,              UINT32,     9)
TYPED_SFIELD(sfExpiration,               UINT32,    10)
TYPED_SFIELD(sfTransferRate,             UINT32,    11)
TYPED_SFIELD(sfWalletSize,               UINT32,    12)
TYPED_SFIELD(sfOwnerCount,               UINT32,    13)
TYPED_SFIELD(sfDestinationTag,           UINT32,    14)
TYPED_SFIELD(sfLastUpdateTime,           UINT32,    15)

// 32-bit integers (uncommon)
TYPED_SFIELD(sfHighQualityIn,            UINT32,    16)
TYPED_SFIELD(sfHighQualityOut,           UINT32,    17)
TYPED_SFIELD(sfLowQualityIn,             UINT32,    18)
TYPED_SFIELD(sfLowQualityOut,            UINT32,    19)
TYPED_SFIELD(sfQualityIn,                UINT32,    20)
TYPED_SFIELD(sfQualityOut,               UINT32,    21)
TYPED_SFIELD(sfStampEscrow,              UINT32,    22)
TYPED_SFIELD(sfBondAmount,               UINT32,    23)
TYPED_SFIELD(sfLoadFee,                  UINT32,    24)
TYPED_SFIELD(sfOfferSequence,            UINT32,    25)
TYPED_SFIELD(sfFirstLedgerSequence,      UINT32,    26)
TYPED_SFIELD(sfLastLedgerSequence,       UINT32,    27)
TYPED_SFIELD(sfTransactionIndex,         UINT32,    28)
TYPED_SFIELD(sfOperationLimit,           UINT32,    29)
TYPED_SFIELD(sfReferenceFeeUnits,        UINT32,    30)
TYPED_SFIELD(sfReserveBase,              UINT32,    31)
TYPED_SFIELD(sfReserveIncrement,         UINT32,    32)
TYPED_SFIELD(sfSetFlag,                  UINT32,    33)
TYPED_SFIELD(sfClearFlag,                UINT32,    34)
TYPED_SFIELD(sfSignerQuorum,             UINT32,    35)
TYPED_SFIELD(sfCancelAfter,              UINT32,    36)
TYPED_SFIELD(sfFinishAfter,              UINT32,    37)
TYPED_SFIELD(sfSignerListID,             UINT32,    38)
TYPED_SFIELD(sfSettleDelay,              UINT32,    39)
TYPED_SFIELD(sfTicketCount,              UINT32,    40)
TYPED_SFIELD(sfTicketSequence,           UINT32,    41)
TYPED_SFIELD(sfNFTokenTaxon,             UINT32,    42)
TYPED_SFIELD(sfMintedNFTokens,           UINT32,    43)
TYPED_SFIELD(sfBurnedNFTokens,           UINT32,    44)
TYPED_SFIELD(sfHookStateCount,           UINT32,    45)
TYPED_SFIELD(sfEmitGeneration,           UINT32,    46)
//                                                  47 reserved for Hooks
TYPED_SFIELD(sfVoteWeight,               UINT32,    48)
TYPED_SFIELD(sfFirstNFTokenSequence,     UINT32,    50)
TYPED_SFIELD(sfOracleDocumentID,         UINT32,    51)

// 64-bit integers (common)
TYPED_SFIELD(sfIndexNext,                UINT64,     1)
TYPED_SFIELD(sfIndexPrevious,            UINT64,     2)
TYPED_SFIELD(sfBookNode,                 UINT64,     3)
TYPED_SFIELD(sfOwnerNode,                UINT64,     4)
TYPED_SFIELD(sfBaseFee,                  UINT64,     5)
TYPED_SFIELD(sfExchangeRate,             UINT64,     6)
TYPED_SFIELD(sfLowNode,                  UINT64,     7)
TYPED_SFIELD(sfHighNode,                 UINT64,     8)
TYPED_SFIELD(sfDestinationNode,          UINT64,     9)
TYPED_SFIELD(sfCookie,                   UINT64,    10)
TYPED_SFIELD(sfServerVersion,            UINT64,    11)
TYPED_SFIELD(sfNFTokenOfferNode,         UINT64,    12)
TYPED_SFIELD(sfEmitBurden,               UINT64,    13)

// 64-bit integers (uncommon)
TYPED_SFIELD(sfHookOn,                   UINT64,    16)
TYPED_SFIELD(sfHookInstructionCount,     UINT64,    17)
TYPED_SFIELD(sfHookReturnCode,           UINT64,    18)
TYPED_SFIELD(sfReferenceCount,           UINT64,    19)
TYPED_SFIELD(sfXChainClaimID,            UINT64,    20)
TYPED_SFIELD(sfXChainAccountCreateCount, UINT64,    21)
TYPED_SFIELD(sfXChainAccountClaimCount,  UINT64,    22)
TYPED_SFIELD(sfAssetPrice,               UINT64,    23)

// 128-bit
TYPED_SFIELD(sfEmailHash,                UINT128,    1)

// 160-bit (common)
TYPED_SFIELD(sfTakerPaysCurrency,        UINT160,    1)
TYPED_SFIELD(sfTakerPaysIssuer,          UINT160,    2)
TYPED_SFIELD(sfTakerGetsCurrency,        UINT160,    3)
TYPED_SFIELD(sfTakerGetsIssuer,          UINT160,    4)

// 256-bit (common)
TYPED_SFIELD(sfLedgerHash,               UINT256,    1)
TYPED_SFIELD(sfParentHash,               UINT256,    2)
TYPED_SFIELD(sfTransactionHash,          UINT256,    3)
TYPED_SFIELD(sfAccountHash,              UINT256,    4)
TYPED_SFIELD(sfPreviousTxnID,            UINT256,    5, SField::sMD_DeleteFinal)
TYPED_SFIELD(sfLedgerIndex,              UINT256,    6)
TYPED_SFIELD(sfWalletLocator,            UINT256,    7)
TYPED_SFIELD(sfRootIndex,                UINT256,    8, SField::sMD_Always)
TYPED_SFIELD(sfAccountTxnID,             UINT256,    9)
TYPED_SFIELD(sfNFTokenID,                UINT256,   10)
TYPED_SFIELD(sfEmitParentTxnID,          UINT256,   11)
TYPED_SFIELD(sfEmitNonce,                UINT256,   12)
TYPED_SFIELD(sfEmitHookHash,             UINT256,   13)
TYPED_SFIELD(sfAMMID,                    UINT256,   14)

// 256-bit (uncommon)
TYPED_SFIELD(sfBookDirectory,            UINT256,   16)
TYPED_SFIELD(sfInvoiceID,                UINT256,   17)
TYPED_SFIELD(sfNickname,                 UINT256,   18)
TYPED_SFIELD(sfAmendment,                UINT256,   19)
//                                                  20 unused
TYPED_SFIELD(sfDigest,                   UINT256,   21)
TYPED_SFIELD(sfChannel,                  UINT256,   22)
TYPED_SFIELD(sfConsensusHash,            UINT256,   23)
TYPED_SFIELD(sfCheckID,                  UINT256,   24)
TYPED_SFIELD(sfValidatedHash,            UINT256,   25)
TYPED_SFIELD(sfPreviousPageMin,          UINT256,   26)
TYPED_SFIELD(sfNextPageMin,              UINT256,   27)
TYPED_SFIELD(sfNFTokenBuyOffer,          UINT256,   28)
TYPED_SFIELD(sfNFTokenSellOffer,         UINT256,   29)
TYPED_SFIELD(sfHookStateKey,             UINT256,   30)
TYPED_SFIELD(sfHookHash,                 UINT256,   31)
TYPED_SFIELD(sfHookNamespace,            UINT256,   32)
TYPED_SFIELD(sfHookSetTxnID,             UINT256,   33)

// currency amount (common)
TYPED_SFIELD(sfAmount,                   AMOUNT,     1)
TYPED_SFIELD(sfBalance,                  AMOUNT,     2)
TYPED_SFIELD(sfLimitAmount,              AMOUNT,     3)
TYPED_SFIELD(sfTakerPays,                AMOUNT,     4)
TYPED_SFIELD(sfTakerGets,                AMOUNT,     5)
TYPED_SFIELD(sfLowLimit,                 AMOUNT,     6)
TYPED_SFIELD(sfHighLimit,                AMOUNT,     7)
TYPED_SFIELD(sfFee,                      AMOUNT,     8)
TYPED_SFIELD(sfSendMax,                  AMOUNT,     9)
TYPED_SFIELD(sfDeliverMin,               AMOUNT,    10)
TYPED_SFIELD(sfAmount2,                  AMOUNT,    11)
TYPED_SFIELD(sfBidMin,                   AMOUNT,    12)
TYPED_SFIELD(sfBidMax,                   AMOUNT,    13)

// currency amount (uncommon)
TYPED_SFIELD(sfMinimumOffer,             AMOUNT,    16)
TYPED_SFIELD(sfRippleEscrow,             AMOUNT,    17)
TYPED_SFIELD(sfDeliveredAmount,          AMOUNT,    18)
TYPED_SFIELD(sfNFTokenBrokerFee,         AMOUNT,    19)

// Reserve 20 & 21 for Hooks.

// currency amount (fees)
TYPED_SFIELD(sfBaseFeeDrops,             AMOUNT,    22)
TYPED_SFIELD(sfReserveBaseDrops,         AMOUNT,    23)
TYPED_SFIELD(sfReserveIncrementDrops,    AMOUNT,    24)

// currency amount (AMM)
TYPED_SFIELD(sfLPTokenOut,               AMOUNT,    25)
TYPED_SFIELD(sfLPTokenIn,                AMOUNT,    26)
TYPED_SFIELD(sfEPrice,                   AMOUNT,    27)
TYPED_SFIELD(sfPrice,                    AMOUNT,    28)
TYPED_SFIELD(sfSignatureReward,          AMOUNT,    29)
TYPED_SFIELD(sfMinAccountCreateAmount,   AMOUNT,    30)
TYPED_SFIELD(sfLPTokenBalance,           AMOUNT,    31)

// variable length (common)
TYPED_SFIELD(sfPublicKey,                VL,         1)
TYPED_SFIELD(sfMessageKey,               VL,         2)
TYPED_SFIELD(sfSigningPubKey,            VL,         3)
TYPED_SFIELD(sfTxnSignature,             VL,         4, SField::sMD_Default, SField::notSigning)
TYPED_SFIELD(sfURI,                      VL,         5)
TYPED_SFIELD(sfSignature,                VL,         6, SField::sMD_Default, SField::notSigning)
TYPED_SFIELD(sfDomain,                   VL,         7)
TYPED_SFIELD(sfFundCode,                 VL,         8)
TYPED_SFIELD(sfRemoveCode,               VL,         9)
TYPED_SFIELD(sfExpireCode,               VL,        10)
TYPED_SFIELD(sfCreateCode,               VL,        11)
TYPED_SFIELD(sfMemoType,                 VL,        12)
TYPED_SFIELD(sfMemoData,                 VL,        13)
TYPED_SFIELD(sfMemoFormat,               VL,        14)

// variable length (uncommon)
TYPED_SFIELD(sfFulfillment,              VL,        16)
TYPED_SFIELD(sfCondition,                VL,        17)
TYPED_SFIELD(sfMasterSignature,          VL,        18, SField::sMD_Default, SField::notSigning)
TYPED_SFIELD(sfUNLModifyValidator,       VL,        19)
TYPED_SFIELD(sfValidatorToDisable,       VL,        20)
TYPED_SFIELD(sfValidatorToReEnable,      VL,        21)
TYPED_SFIELD(sfHookStateData,            VL,        22)
TYPED_SFIELD(sfHookReturnString,         VL,        23)
TYPED_SFIELD(sfHookParameterName,        VL,        24)
TYPED_SFIELD(sfHookParameterValue,       VL,        25)
TYPED_SFIELD(sfDIDDocument,              VL,        26)
TYPED_SFIELD(sfData,                     VL,        27)
TYPED_SFIELD(sfAssetClass,               VL,        28)
TYPED_SFIELD(sfProvider,                 VL,        29)

// account (common)
TYPED_SFIELD(sfAccount,                  ACCOUNT,    1)
TYPED_SFIELD(sfOwner,                    ACCOUNT,    2)
TYPED_SFIELD(sfDestination,              ACCOUNT,    3)
TYPED_SFIELD(sfIssuer,                   ACCOUNT,    4)
TYPED_SFIELD(sfAuthorize,                ACCOUNT,    5)
TYPED_SFIELD(sfUnauthorize,              ACCOUNT,    6)
//                                                   7 unused
TYPED_SFIELD(sfRegularKey,               ACCOUNT,    8)
TYPED_SFIELD(sfNFTokenMinter,            ACCOUNT,    9)
TYPED_SFIELD(sfEmitCallback,             ACCOUNT,   10)

// account (uncommon)
TYPED_SFIELD(sfHookAccount,              ACCOUNT,   16)
TYPED_SFIELD(sfOtherChainSource,         ACCOUNT,   18)
TYPED_SFIELD(sfOtherChainDestination,    ACCOUNT,   19)
TYPED_SFIELD(sfAttestationSignerAccount, ACCOUNT,   20)
TYPED_SFIELD(sfAttestationRewardAccount, ACCOUNT,   21)
TYPED_SFIELD(sfLockingChainDoor,         ACCOUNT,   22)
TYPED_SFIELD(sfIssuingChainDoor,         ACCOUNT,   23)

// vector of 256-bit
TYPED_SFIELD(sfIndexes,                  VECTOR256,  1, SField::sMD_Never)
TYPED_SFIELD(sfHashes,                   VECTOR256,  2)
TYPED_SFIELD(sfAmendments,               VECTOR256,  3)
TYPED_SFIELD(sfNFTokenOffers,            VECTOR256,  4)

// path set
UNTYPED_SFIELD(sfPaths,                  PATHSET,    1)

// currency
TYPED_SFIELD(sfBaseAsset,                CURRENCY,   1)
TYPED_SFIELD(sfQuoteAsset,               CURRENCY,   2)

// issue
TYPED_SFIELD(sfLockingChainIssue,        ISSUE,      1)
TYPED_SFIELD(sfIssuingChainIssue,        ISSUE,      2)
TYPED_SFIELD(sfAsset,                    ISSUE,      3)
TYPED_SFIELD(sfAsset2,                   ISSUE,      4)

// bridge
TYPED_SFIELD(sfXChainBridge,             XCHAIN_BRIDGE, 1)

// inner object
// OBJECT/1 is reserved for end of object
UNTYPED_SFIELD(sfTransactionMetaData,    OBJECT,     2)
UNTYPED_SFIELD(sfCreatedNode,            OBJECT,     3)
UNTYPED_SFIELD(sfDeletedNode,            OBJECT,     4)
UNTYPED_SFIELD(sfModifiedNode,           OBJECT,     5)
UNTYPED_SFIELD(sfPreviousFields,         OBJECT,     6)
UNTYPED_SFIELD(sfFinalFields,            OBJECT,     7)
UNTYPED_SFIELD(sfNewFields,              OBJECT,     8)
UNTYPED_SFIELD(sfTemplateEntry,          OBJECT,     9)
UNTYPED_SFIELD(sfMemo,                   OBJECT,    10)
UNTYPED_SFIELD(sfSignerEntry,            OBJECT,    11)
UNTYPED_SFIELD(sfNFToken,                OBJECT,    12)
UNTYPED_SFIELD(sfEmitDetails,            OBJECT,    13)
UNTYPED_SFIELD(sfHook,                   OBJECT,    14)

// inner object (uncommon)
UNTYPED_SFIELD(sfSigner,                 OBJECT,    16)
//                                                  17 unused
UNTYPED_SFIELD(sfMajority,               OBJECT,    18)
UNTYPED_SFIELD(sfDisabledValidator,      OBJECT,    19)
UNTYPED_SFIELD(sfEmittedTxn,             OBJECT,    20)
UNTYPED_SFIELD(sfHookExecution,          OBJECT,    21)
UNTYPED_SFIELD(sfHookDefinition,         OBJECT,    22)
UNTYPED_SFIELD(sfHookParameter,          OBJECT,    23)
UNTYPED_SFIELD(sfHookGrant,              OBJECT,    24)
UNTYPED_SFIELD(sfVoteEntry,              OBJECT,    25)
UNTYPED_SFIELD(sfAuctionSlot,            OBJECT,    26)
UNTYPED_SFIELD(sfAuthAccount,            OBJECT,    27)
UNTYPED_SFIELD(sfXChainClaimProofSig,    OBJECT,    28)
UNTYPED_SFIELD(sfXChainCreateAccountProofSig, OBJECT, 29)
UNTYPED_SFIELD(sfXChainClaimAttestationCollectionElement, OBJECT, 30)
UNTYPED_SFIELD(sfXChainCreateAccountAttestationCollectionElement, OBJECT, 31)
UNTYPED_SFIELD(sfPriceData,              OBJECT,    32)

// array of objects (common)
// ARRAY/1 is reserved for end of array
// sfSigningAccounts has never been used.
//UNTYPED_SFIELD(sfSigningAccounts,        ARRAY,      2)
UNTYPED_SFIELD(sfSigners,                ARRAY,      3, SField::sMD_Default, SField::notSigning)
UNTYPED_SFIELD(sfSignerEntries,          ARRAY,      4)
UNTYPED_SFIELD(sfTemplate,               ARRAY,      5)
UNTYPED_SFIELD(sfNecessary,              ARRAY,      6)
UNTYPED_SFIELD(sfSufficient,             ARRAY,      7)
UNTYPED_SFIELD(sfAffectedNodes,          ARRAY,      8)
UNTYPED_SFIELD(sfMemos,                  ARRAY,      9)
UNTYPED_SFIELD(sfNFTokens,               ARRAY,     10)
UNTYPED_SFIELD(sfHooks,                  ARRAY,     11)
UNTYPED_SFIELD(sfVoteSlots,              ARRAY,     12)

// array of objects (uncommon)
UNTYPED_SFIELD(sfMajorities,             ARRAY,     16)
UNTYPED_SFIELD(sfDisabledValidators,     ARRAY,     17)
UNTYPED_SFIELD(sfHookExecutions,         ARRAY,     18)
UNTYPED_SFIELD(sfHookParameters,         ARRAY,     19)
UNTYPED_SFIELD(sfHookGrants,             ARRAY,     20)
UNTYPED_SFIELD(sfXChainClaimAttestations, ARRAY,    21)
UNTYPED_SFIELD(sfXChainCreateAccountAttestations, ARRAY, 22)
//                                                  23 unused
UNTYPED_SFIELD(sfPriceDataSeries,        ARRAY,     24)
UNTYPED_SFIELD(sfAuthAccounts,           ARRAY,     25)
