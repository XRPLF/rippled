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
UNTYPED_SFIELD(LedgerEntry,            LEDGERENTRY, 257) // sfLedgerEntry
UNTYPED_SFIELD(Transaction,            TRANSACTION, 257) // sfTransaction
UNTYPED_SFIELD(Validation,             VALIDATION,  257) // sfValidation
UNTYPED_SFIELD(Metadata,               METADATA,    257) // sfMetadata

// 8-bit integers (common)
TYPED_SFIELD(CloseResolution,          UINT8,      1) // sfCloseResolution
TYPED_SFIELD(Method,                   UINT8,      2) // sfMethod
TYPED_SFIELD(TransactionResult,        UINT8,      3) // sfTransactionResult
TYPED_SFIELD(Scale,                    UINT8,      4) // sfScale

// 8-bit integers (uncommon)
TYPED_SFIELD(TickSize,                 UINT8,     16) // sfTickSize
TYPED_SFIELD(UNLModifyDisabling,       UINT8,     17) // sfUNLModifyDisabling
TYPED_SFIELD(HookResult,               UINT8,     18) // sfHookResult
TYPED_SFIELD(WasLockingChainSend,      UINT8,     19) // sfWasLockingChainSend

// 16-bit integers (common)
TYPED_SFIELD(LedgerEntryType,          UINT16,     1, SField::sMD_Never) // sfLedgerEntryType
TYPED_SFIELD(TransactionType,          UINT16,     2) // sfTransactionType
TYPED_SFIELD(SignerWeight,             UINT16,     3) // sfSignerWeight
TYPED_SFIELD(TransferFee,              UINT16,     4) // sfTransferFee
TYPED_SFIELD(TradingFee,               UINT16,     5) // sfTradingFee
TYPED_SFIELD(DiscountedFee,            UINT16,     6) // sfDiscountedFee

// 16-bit integers (uncommon)
TYPED_SFIELD(Version,                  UINT16,    16) // sfVersion
TYPED_SFIELD(HookStateChangeCount,     UINT16,    17) // sfHookStateChangeCount
TYPED_SFIELD(HookEmitCount,            UINT16,    18) // sfHookEmitCount
TYPED_SFIELD(HookExecutionIndex,       UINT16,    19) // sfHookExecutionIndex
TYPED_SFIELD(HookApiVersion,           UINT16,    20) // sfHookApiVersion
TYPED_SFIELD(LedgerFixType,            UINT16,    21); // sfLedgerFixType

// 32-bit integers (common)
TYPED_SFIELD(NetworkID,                UINT32,     1) // sfNetworkID
TYPED_SFIELD(Flags,                    UINT32,     2) // sfFlags
TYPED_SFIELD(SourceTag,                UINT32,     3) // sfSourceTag
TYPED_SFIELD(Sequence,                 UINT32,     4) // sfSequence
TYPED_SFIELD(PreviousTxnLgrSeq,        UINT32,     5, SField::sMD_DeleteFinal) // sfPreviousTxnLgrSeq
TYPED_SFIELD(LedgerSequence,           UINT32,     6) // sfLedgerSequence
TYPED_SFIELD(CloseTime,                UINT32,     7) // sfCloseTime
TYPED_SFIELD(ParentCloseTime,          UINT32,     8) // sfParentCloseTime
TYPED_SFIELD(SigningTime,              UINT32,     9) // sfSigningTime
TYPED_SFIELD(Expiration,               UINT32,    10) // sfExpiration
TYPED_SFIELD(TransferRate,             UINT32,    11) // sfTransferRate
TYPED_SFIELD(WalletSize,               UINT32,    12) // sfWalletSize
TYPED_SFIELD(OwnerCount,               UINT32,    13) // sfOwnerCount
TYPED_SFIELD(DestinationTag,           UINT32,    14) // sfDestinationTag
TYPED_SFIELD(LastUpdateTime,           UINT32,    15) // sfLastUpdateTime

// 32-bit integers (uncommon)
TYPED_SFIELD(HighQualityIn,            UINT32,    16) // sfHighQualityIn
TYPED_SFIELD(HighQualityOut,           UINT32,    17) // sfHighQualityOut
TYPED_SFIELD(LowQualityIn,             UINT32,    18) // sfLowQualityIn
TYPED_SFIELD(LowQualityOut,            UINT32,    19) // sfLowQualityOut
TYPED_SFIELD(QualityIn,                UINT32,    20) // sfQualityIn
TYPED_SFIELD(QualityOut,               UINT32,    21) // sfQualityOut
TYPED_SFIELD(StampEscrow,              UINT32,    22) // sfStampEscrow
TYPED_SFIELD(BondAmount,               UINT32,    23) // sfBondAmount
TYPED_SFIELD(LoadFee,                  UINT32,    24) // sfLoadFee
TYPED_SFIELD(OfferSequence,            UINT32,    25) // sfOfferSequence
TYPED_SFIELD(FirstLedgerSequence,      UINT32,    26) // sfFirstLedgerSequence
TYPED_SFIELD(LastLedgerSequence,       UINT32,    27) // sfLastLedgerSequence
TYPED_SFIELD(TransactionIndex,         UINT32,    28) // sfTransactionIndex
TYPED_SFIELD(OperationLimit,           UINT32,    29) // sfOperationLimit
TYPED_SFIELD(ReferenceFeeUnits,        UINT32,    30) // sfReferenceFeeUnits
TYPED_SFIELD(ReserveBase,              UINT32,    31) // sfReserveBase
TYPED_SFIELD(ReserveIncrement,         UINT32,    32) // sfReserveIncrement
TYPED_SFIELD(SetFlag,                  UINT32,    33) // sfSetFlag
TYPED_SFIELD(ClearFlag,                UINT32,    34) // sfClearFlag
TYPED_SFIELD(SignerQuorum,             UINT32,    35) // sfSignerQuorum
TYPED_SFIELD(CancelAfter,              UINT32,    36) // sfCancelAfter
TYPED_SFIELD(FinishAfter,              UINT32,    37) // sfFinishAfter
TYPED_SFIELD(SignerListID,             UINT32,    38) // sfSignerListID
TYPED_SFIELD(SettleDelay,              UINT32,    39) // sfSettleDelay
TYPED_SFIELD(TicketCount,              UINT32,    40) // sfTicketCount
TYPED_SFIELD(TicketSequence,           UINT32,    41) // sfTicketSequence
TYPED_SFIELD(NFTokenTaxon,             UINT32,    42) // sfNFTokenTaxon
TYPED_SFIELD(MintedNFTokens,           UINT32,    43) // sfMintedNFTokens
TYPED_SFIELD(BurnedNFTokens,           UINT32,    44) // sfBurnedNFTokens
TYPED_SFIELD(HookStateCount,           UINT32,    45) // sfHookStateCount
TYPED_SFIELD(EmitGeneration,           UINT32,    46) // sfEmitGeneration
//                                                47 reserved for Hooks
TYPED_SFIELD(VoteWeight,               UINT32,    48) // sfVoteWeight
TYPED_SFIELD(FirstNFTokenSequence,     UINT32,    50) // sfFirstNFTokenSequence
TYPED_SFIELD(OracleDocumentID,         UINT32,    51) // sfOracleDocumentID

// 64-bit integers (common)
TYPED_SFIELD(IndexNext,                UINT64,     1) // sfIndexNext
TYPED_SFIELD(IndexPrevious,            UINT64,     2) // sfIndexPrevious
TYPED_SFIELD(BookNode,                 UINT64,     3) // sfBookNode
TYPED_SFIELD(OwnerNode,                UINT64,     4) // sfOwnerNode
TYPED_SFIELD(BaseFee,                  UINT64,     5) // sfBaseFee
TYPED_SFIELD(ExchangeRate,             UINT64,     6) // sfExchangeRate
TYPED_SFIELD(LowNode,                  UINT64,     7) // sfLowNode
TYPED_SFIELD(HighNode,                 UINT64,     8) // sfHighNode
TYPED_SFIELD(DestinationNode,          UINT64,     9) // sfDestinationNode
TYPED_SFIELD(Cookie,                   UINT64,    10) // sfCookie
TYPED_SFIELD(ServerVersion,            UINT64,    11) // sfServerVersion
TYPED_SFIELD(NFTokenOfferNode,         UINT64,    12) // sfNFTokenOfferNode
TYPED_SFIELD(EmitBurden,               UINT64,    13) // sfEmitBurden

// 64-bit integers (uncommon)
TYPED_SFIELD(HookOn,                   UINT64,    16) // sfHookOn
TYPED_SFIELD(HookInstructionCount,     UINT64,    17) // sfHookInstructionCount
TYPED_SFIELD(HookReturnCode,           UINT64,    18) // sfHookReturnCode
TYPED_SFIELD(ReferenceCount,           UINT64,    19) // sfReferenceCount
TYPED_SFIELD(XChainClaimID,            UINT64,    20) // sfXChainClaimID
TYPED_SFIELD(XChainAccountCreateCount, UINT64,    21) // sfXChainAccountCreateCount
TYPED_SFIELD(XChainAccountClaimCount,  UINT64,    22) // sfXChainAccountClaimCount
TYPED_SFIELD(AssetPrice,               UINT64,    23) // sfAssetPrice

// 128-bit
TYPED_SFIELD(EmailHash,                UINT128,    1) // sfEmailHash

// 160-bit (common)
TYPED_SFIELD(TakerPaysCurrency,        UINT160,    1) // sfTakerPaysCurrency
TYPED_SFIELD(TakerPaysIssuer,          UINT160,    2) // sfTakerPaysIssuer
TYPED_SFIELD(TakerGetsCurrency,        UINT160,    3) // sfTakerGetsCurrency
TYPED_SFIELD(TakerGetsIssuer,          UINT160,    4) // sfTakerGetsIssuer

// 256-bit (common)
TYPED_SFIELD(LedgerHash,               UINT256,    1) // sfLedgerHash
TYPED_SFIELD(ParentHash,               UINT256,    2) // sfParentHash
TYPED_SFIELD(TransactionHash,          UINT256,    3) // sfTransactionHash
TYPED_SFIELD(AccountHash,              UINT256,    4) // sfAccountHash
TYPED_SFIELD(PreviousTxnID,            UINT256,    5, SField::sMD_DeleteFinal) // sfPreviousTxnID
TYPED_SFIELD(LedgerIndex,              UINT256,    6) // sfLedgerIndex
TYPED_SFIELD(WalletLocator,            UINT256,    7) // sfWalletLocator
TYPED_SFIELD(RootIndex,                UINT256,    8, SField::sMD_Always) // sfRootIndex
TYPED_SFIELD(AccountTxnID,             UINT256,    9) // sfAccountTxnID
TYPED_SFIELD(NFTokenID,                UINT256,   10) // sfNFTokenID
TYPED_SFIELD(EmitParentTxnID,          UINT256,   11) // sfEmitParentTxnID
TYPED_SFIELD(EmitNonce,                UINT256,   12) // sfEmitNonce
TYPED_SFIELD(EmitHookHash,             UINT256,   13) // sfEmitHookHash
TYPED_SFIELD(AMMID,                    UINT256,   14) // sfAMMID

// 256-bit (uncommon)
TYPED_SFIELD(BookDirectory,            UINT256,   16) // sfBookDirectory
TYPED_SFIELD(InvoiceID,                UINT256,   17) // sfInvoiceID
TYPED_SFIELD(Nickname,                 UINT256,   18) // sfNickname
TYPED_SFIELD(Amendment,                UINT256,   19) // sfAmendment
//                                                20 unused
TYPED_SFIELD(Digest,                   UINT256,   21) // sfDigest
TYPED_SFIELD(Channel,                  UINT256,   22) // sfChannel
TYPED_SFIELD(ConsensusHash,            UINT256,   23) // sfConsensusHash
TYPED_SFIELD(CheckID,                  UINT256,   24) // sfCheckID
TYPED_SFIELD(ValidatedHash,            UINT256,   25) // sfValidatedHash
TYPED_SFIELD(PreviousPageMin,          UINT256,   26) // sfPreviousPageMin
TYPED_SFIELD(NextPageMin,              UINT256,   27) // sfNextPageMin
TYPED_SFIELD(NFTokenBuyOffer,          UINT256,   28) // sfNFTokenBuyOffer
TYPED_SFIELD(NFTokenSellOffer,         UINT256,   29) // sfNFTokenSellOffer
TYPED_SFIELD(HookStateKey,             UINT256,   30) // sfHookStateKey
TYPED_SFIELD(HookHash,                 UINT256,   31) // sfHookHash
TYPED_SFIELD(HookNamespace,            UINT256,   32) // sfHookNamespace
TYPED_SFIELD(HookSetTxnID,             UINT256,   33) // sfHookSetTxnID

// currency amount (common)
TYPED_SFIELD(Amount,                   AMOUNT,     1) // sfAmount
TYPED_SFIELD(Balance,                  AMOUNT,     2) // sfBalance
TYPED_SFIELD(LimitAmount,              AMOUNT,     3) // sfLimitAmount
TYPED_SFIELD(TakerPays,                AMOUNT,     4) // sfTakerPays
TYPED_SFIELD(TakerGets,                AMOUNT,     5) // sfTakerGets
TYPED_SFIELD(LowLimit,                 AMOUNT,     6) // sfLowLimit
TYPED_SFIELD(HighLimit,                AMOUNT,     7) // sfHighLimit
TYPED_SFIELD(Fee,                      AMOUNT,     8) // sfFee
TYPED_SFIELD(SendMax,                  AMOUNT,     9) // sfSendMax
TYPED_SFIELD(DeliverMin,               AMOUNT,    10) // sfDeliverMin
TYPED_SFIELD(Amount2,                  AMOUNT,    11) // sfAmount2
TYPED_SFIELD(BidMin,                   AMOUNT,    12) // sfBidMin
TYPED_SFIELD(BidMax,                   AMOUNT,    13) // sfBidMax

// currency amount (uncommon)
TYPED_SFIELD(MinimumOffer,             AMOUNT,    16) // sfMinimumOffer
TYPED_SFIELD(RippleEscrow,             AMOUNT,    17) // sfRippleEscrow
TYPED_SFIELD(DeliveredAmount,          AMOUNT,    18) // sfDeliveredAmount
TYPED_SFIELD(NFTokenBrokerFee,         AMOUNT,    19) // sfNFTokenBrokerFee

// Reserve 20 & 21 for Hooks.

// currency amount (fees)
TYPED_SFIELD(BaseFeeDrops,             AMOUNT,    22) // sfBaseFeeDrops
TYPED_SFIELD(ReserveBaseDrops,         AMOUNT,    23) // sfReserveBaseDrops
TYPED_SFIELD(ReserveIncrementDrops,    AMOUNT,    24) // sfReserveIncrementDrops

// currency amount (AMM)
TYPED_SFIELD(LPTokenOut,               AMOUNT,    25) // sfLPTokenOut
TYPED_SFIELD(LPTokenIn,                AMOUNT,    26) // sfLPTokenIn
TYPED_SFIELD(EPrice,                   AMOUNT,    27) // sfEPrice
TYPED_SFIELD(Price,                    AMOUNT,    28) // sfPrice
TYPED_SFIELD(SignatureReward,          AMOUNT,    29) // sfSignatureReward
TYPED_SFIELD(MinAccountCreateAmount,   AMOUNT,    30) // sfMinAccountCreateAmount
TYPED_SFIELD(LPTokenBalance,           AMOUNT,    31) // sfLPTokenBalance

// variable length (common)
TYPED_SFIELD(PublicKey,                VL,         1) // sfPublicKey
TYPED_SFIELD(MessageKey,               VL,         2) // sfMessageKey
TYPED_SFIELD(SigningPubKey,            VL,         3) // sfSigningPubKey
TYPED_SFIELD(TxnSignature,             VL,         4, SField::sMD_Default, SField::notSigning) // sfTxnSignature
TYPED_SFIELD(URI,                      VL,         5) // sfURI
TYPED_SFIELD(Signature,                VL,         6, SField::sMD_Default, SField::notSigning) // sfSignature
TYPED_SFIELD(Domain,                   VL,         7) // sfDomain
TYPED_SFIELD(FundCode,                 VL,         8) // sfFundCode
TYPED_SFIELD(RemoveCode,               VL,         9) // sfRemoveCode
TYPED_SFIELD(ExpireCode,               VL,        10) // sfExpireCode
TYPED_SFIELD(CreateCode,               VL,        11) // sfCreateCode
TYPED_SFIELD(MemoType,                 VL,        12) // sfMemoType
TYPED_SFIELD(MemoData,                 VL,        13) // sfMemoData
TYPED_SFIELD(MemoFormat,               VL,        14) // sfMemoFormat

// variable length (uncommon)
TYPED_SFIELD(Fulfillment,              VL,        16) // sfFulfillment
TYPED_SFIELD(Condition,                VL,        17) // sfCondition
TYPED_SFIELD(MasterSignature,          VL,        18, SField::sMD_Default, SField::notSigning) // sfMasterSignature
TYPED_SFIELD(UNLModifyValidator,       VL,        19) // sfUNLModifyValidator
TYPED_SFIELD(ValidatorToDisable,       VL,        20) // sfValidatorToDisable
TYPED_SFIELD(ValidatorToReEnable,      VL,        21) // sfValidatorToReEnable
TYPED_SFIELD(HookStateData,            VL,        22) // sfHookStateData
TYPED_SFIELD(HookReturnString,         VL,        23) // sfHookReturnString
TYPED_SFIELD(HookParameterName,        VL,        24) // sfHookParameterName
TYPED_SFIELD(HookParameterValue,       VL,        25) // sfHookParameterValue
TYPED_SFIELD(DIDDocument,              VL,        26) // sfDIDDocument
TYPED_SFIELD(Data,                     VL,        27) // sfData
TYPED_SFIELD(AssetClass,               VL,        28) // sfAssetClass
TYPED_SFIELD(Provider,                 VL,        29) // sfProvider

// account (common)
TYPED_SFIELD(Account,                  ACCOUNT,    1) // sfAccount
TYPED_SFIELD(Owner,                    ACCOUNT,    2) // sfOwner
TYPED_SFIELD(Destination,              ACCOUNT,    3) // sfDestination
TYPED_SFIELD(Issuer,                   ACCOUNT,    4) // sfIssuer
TYPED_SFIELD(Authorize,                ACCOUNT,    5) // sfAuthorize
TYPED_SFIELD(Unauthorize,              ACCOUNT,    6) // sfUnauthorize
//                                                 7 unused
TYPED_SFIELD(RegularKey,               ACCOUNT,    8) // sfRegularKey
TYPED_SFIELD(NFTokenMinter,            ACCOUNT,    9) // sfNFTokenMinter
TYPED_SFIELD(EmitCallback,             ACCOUNT,   10) // sfEmitCallback

// account (uncommon)
TYPED_SFIELD(HookAccount,              ACCOUNT,   16) // sfHookAccount
TYPED_SFIELD(OtherChainSource,         ACCOUNT,   18) // sfOtherChainSource
TYPED_SFIELD(OtherChainDestination,    ACCOUNT,   19) // sfOtherChainDestination
TYPED_SFIELD(AttestationSignerAccount, ACCOUNT,   20) // sfAttestationSignerAccount
TYPED_SFIELD(AttestationRewardAccount, ACCOUNT,   21) // sfAttestationRewardAccount
TYPED_SFIELD(LockingChainDoor,         ACCOUNT,   22) // sfLockingChainDoor
TYPED_SFIELD(IssuingChainDoor,         ACCOUNT,   23) // sfIssuingChainDoor

// vector of 256-bit
TYPED_SFIELD(Indexes,                  VECTOR256,  1, SField::sMD_Never) // sfIndexes
TYPED_SFIELD(Hashes,                   VECTOR256,  2) // sfHashes
TYPED_SFIELD(Amendments,               VECTOR256,  3) // sfAmendments
TYPED_SFIELD(NFTokenOffers,            VECTOR256,  4) // sfNFTokenOffers

// path set
UNTYPED_SFIELD(Paths,                  PATHSET,    1) // sfPaths

// currency
TYPED_SFIELD(BaseAsset,                CURRENCY,   1) // sfBaseAsset
TYPED_SFIELD(QuoteAsset,               CURRENCY,   2) // sfQuoteAsset

// issue
TYPED_SFIELD(LockingChainIssue,        ISSUE,      1) // sfLockingChainIssue
TYPED_SFIELD(IssuingChainIssue,        ISSUE,      2) // sfIssuingChainIssue
TYPED_SFIELD(Asset,                    ISSUE,      3) // sfAsset
TYPED_SFIELD(Asset2,                   ISSUE,      4) // sfAsset2

// bridge
TYPED_SFIELD(XChainBridge,             XCHAIN_BRIDGE, 1) // sfXChainBridge

// inner object
// OBJECT/1 is reserved for end of object
UNTYPED_SFIELD(TransactionMetaData,    OBJECT,     2) // sfTransactionMetaData
UNTYPED_SFIELD(CreatedNode,            OBJECT,     3) // sfCreatedNode
UNTYPED_SFIELD(DeletedNode,            OBJECT,     4) // sfDeletedNode
UNTYPED_SFIELD(ModifiedNode,           OBJECT,     5) // sfModifiedNode
UNTYPED_SFIELD(PreviousFields,         OBJECT,     6) // sfPreviousFields
UNTYPED_SFIELD(FinalFields,            OBJECT,     7) // sfFinalFields
UNTYPED_SFIELD(NewFields,              OBJECT,     8) // sfNewFields
UNTYPED_SFIELD(TemplateEntry,          OBJECT,     9) // sfTemplateEntry
UNTYPED_SFIELD(Memo,                   OBJECT,    10) // sfMemo
UNTYPED_SFIELD(SignerEntry,            OBJECT,    11) // sfSignerEntry
UNTYPED_SFIELD(NFToken,                OBJECT,    12) // sfNFToken
UNTYPED_SFIELD(EmitDetails,            OBJECT,    13) // sfEmitDetails
UNTYPED_SFIELD(Hook,                   OBJECT,    14) // sfHook

// inner object (uncommon)
UNTYPED_SFIELD(Signer,                 OBJECT,    16) // sfSigner
//                                                17 unused
UNTYPED_SFIELD(Majority,               OBJECT,    18) // sfMajority
UNTYPED_SFIELD(DisabledValidator,      OBJECT,    19) // sfDisabledValidator
UNTYPED_SFIELD(EmittedTxn,             OBJECT,    20) // sfEmittedTxn
UNTYPED_SFIELD(HookExecution,          OBJECT,    21) // sfHookExecution
UNTYPED_SFIELD(HookDefinition,         OBJECT,    22) // sfHookDefinition
UNTYPED_SFIELD(HookParameter,          OBJECT,    23) // sfHookParameter
UNTYPED_SFIELD(HookGrant,              OBJECT,    24) // sfHookGrant
UNTYPED_SFIELD(VoteEntry,              OBJECT,    25) // sfVoteEntry
UNTYPED_SFIELD(AuctionSlot,            OBJECT,    26) // sfAuctionSlot
UNTYPED_SFIELD(AuthAccount,            OBJECT,    27) // sfAuthAccount
UNTYPED_SFIELD(XChainClaimProofSig,    OBJECT,    28) // sfXChainClaimProofSig
UNTYPED_SFIELD(XChainCreateAccountProofSig, OBJECT, 29) // sfXChainCreateAccountProofSig
UNTYPED_SFIELD(XChainClaimAttestationCollectionElement, OBJECT, 30) // sfXChainClaimAttestationCollectionElement
UNTYPED_SFIELD(XChainCreateAccountAttestationCollectionElement, OBJECT, 31) // sfXChainCreateAccountAttestationCollectionElement
UNTYPED_SFIELD(PriceData,              OBJECT,    32) // sfPriceData

// array of objects (common)
// ARRAY/1 is reserved for end of array
// sfSigningAccounts has never been used.
//UNTYPED_SFIELD(SigningAccounts,      ARRAY,      2) // sfSigningAccounts
UNTYPED_SFIELD(Signers,                ARRAY,      3, SField::sMD_Default, SField::notSigning) // sfSigners
UNTYPED_SFIELD(SignerEntries,          ARRAY,      4) // sfSignerEntries
UNTYPED_SFIELD(Template,               ARRAY,      5) // sfTemplate
UNTYPED_SFIELD(Necessary,              ARRAY,      6) // sfNecessary
UNTYPED_SFIELD(Sufficient,             ARRAY,      7) // sfSufficient
UNTYPED_SFIELD(AffectedNodes,          ARRAY,      8) // sfAffectedNodes
UNTYPED_SFIELD(Memos,                  ARRAY,      9) // sfMemos
UNTYPED_SFIELD(NFTokens,               ARRAY,     10) // sfNFTokens
UNTYPED_SFIELD(Hooks,                  ARRAY,     11) // sfHooks
UNTYPED_SFIELD(VoteSlots,              ARRAY,     12) // sfVoteSlots

// array of objects (uncommon)
UNTYPED_SFIELD(Majorities,             ARRAY,     16) // sfMajorities
UNTYPED_SFIELD(DisabledValidators,     ARRAY,     17) // sfDisabledValidators
UNTYPED_SFIELD(HookExecutions,         ARRAY,     18) // sfHookExecutions
UNTYPED_SFIELD(HookParameters,         ARRAY,     19) // sfHookParameters
UNTYPED_SFIELD(HookGrants,             ARRAY,     20) // sfHookGrants
UNTYPED_SFIELD(XChainClaimAttestations, ARRAY,    21) // sfXChainClaimAttestations
UNTYPED_SFIELD(XChainCreateAccountAttestations, ARRAY, 22) // sfXChainCreateAccountAttestations
//                                                23 unused
UNTYPED_SFIELD(PriceDataSeries,        ARRAY,     24) // sfPriceDataSeries
UNTYPED_SFIELD(AuthAccounts,           ARRAY,     25) // sfAuthAccounts
