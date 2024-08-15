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

// clang-format off

// untyped
UNTYPED_SFIELD(LedgerEntry,            LEDGERENTRY, 257)
UNTYPED_SFIELD(Transaction,            TRANSACTION, 257)
UNTYPED_SFIELD(Validation,             VALIDATION,  257)
UNTYPED_SFIELD(Metadata,               METADATA,    257)

// 8-bit integers (common)
TYPED_SFIELD(CloseResolution,          UINT8,      1)
TYPED_SFIELD(Method,                   UINT8,      2)
TYPED_SFIELD(TransactionResult,        UINT8,      3)
TYPED_SFIELD(Scale,                    UINT8,      4)

// 8-bit integers (uncommon)
TYPED_SFIELD(TickSize,                 UINT8,     16)
TYPED_SFIELD(UNLModifyDisabling,       UINT8,     17)
TYPED_SFIELD(HookResult,               UINT8,     18)
TYPED_SFIELD(WasLockingChainSend,      UINT8,     19)

// 16-bit integers (common)
TYPED_SFIELD(LedgerEntryType,          UINT16,     1, SField::sMD_Never)
TYPED_SFIELD(TransactionType,          UINT16,     2)
TYPED_SFIELD(SignerWeight,             UINT16,     3)
TYPED_SFIELD(TransferFee,              UINT16,     4)
TYPED_SFIELD(TradingFee,               UINT16,     5)
TYPED_SFIELD(DiscountedFee,            UINT16,     6)

// 16-bit integers (uncommon)
TYPED_SFIELD(Version,                  UINT16,    16)
TYPED_SFIELD(HookStateChangeCount,     UINT16,    17)
TYPED_SFIELD(HookEmitCount,            UINT16,    18)
TYPED_SFIELD(HookExecutionIndex,       UINT16,    19)
TYPED_SFIELD(HookApiVersion,           UINT16,    20)
TYPED_SFIELD(LedgerFixType,            UINT16,    21);

// 32-bit integers (common)
TYPED_SFIELD(NetworkID,                UINT32,     1)
TYPED_SFIELD(Flags,                    UINT32,     2)
TYPED_SFIELD(SourceTag,                UINT32,     3)
TYPED_SFIELD(Sequence,                 UINT32,     4)
TYPED_SFIELD(PreviousTxnLgrSeq,        UINT32,     5, SField::sMD_DeleteFinal)
TYPED_SFIELD(LedgerSequence,           UINT32,     6)
TYPED_SFIELD(CloseTime,                UINT32,     7)
TYPED_SFIELD(ParentCloseTime,          UINT32,     8)
TYPED_SFIELD(SigningTime,              UINT32,     9)
TYPED_SFIELD(Expiration,               UINT32,    10)
TYPED_SFIELD(TransferRate,             UINT32,    11)
TYPED_SFIELD(WalletSize,               UINT32,    12)
TYPED_SFIELD(OwnerCount,               UINT32,    13)
TYPED_SFIELD(DestinationTag,           UINT32,    14)
TYPED_SFIELD(LastUpdateTime,           UINT32,    15)

// 32-bit integers (uncommon)
TYPED_SFIELD(HighQualityIn,            UINT32,    16)
TYPED_SFIELD(HighQualityOut,           UINT32,    17)
TYPED_SFIELD(LowQualityIn,             UINT32,    18)
TYPED_SFIELD(LowQualityOut,            UINT32,    19)
TYPED_SFIELD(QualityIn,                UINT32,    20)
TYPED_SFIELD(QualityOut,               UINT32,    21)
TYPED_SFIELD(StampEscrow,              UINT32,    22)
TYPED_SFIELD(BondAmount,               UINT32,    23)
TYPED_SFIELD(LoadFee,                  UINT32,    24)
TYPED_SFIELD(OfferSequence,            UINT32,    25)
TYPED_SFIELD(FirstLedgerSequence,      UINT32,    26)
TYPED_SFIELD(LastLedgerSequence,       UINT32,    27)
TYPED_SFIELD(TransactionIndex,         UINT32,    28)
TYPED_SFIELD(OperationLimit,           UINT32,    29)
TYPED_SFIELD(ReferenceFeeUnits,        UINT32,    30)
TYPED_SFIELD(ReserveBase,              UINT32,    31)
TYPED_SFIELD(ReserveIncrement,         UINT32,    32)
TYPED_SFIELD(SetFlag,                  UINT32,    33)
TYPED_SFIELD(ClearFlag,                UINT32,    34)
TYPED_SFIELD(SignerQuorum,             UINT32,    35)
TYPED_SFIELD(CancelAfter,              UINT32,    36)
TYPED_SFIELD(FinishAfter,              UINT32,    37)
TYPED_SFIELD(SignerListID,             UINT32,    38)
TYPED_SFIELD(SettleDelay,              UINT32,    39)
TYPED_SFIELD(TicketCount,              UINT32,    40)
TYPED_SFIELD(TicketSequence,           UINT32,    41)
TYPED_SFIELD(NFTokenTaxon,             UINT32,    42)
TYPED_SFIELD(MintedNFTokens,           UINT32,    43)
TYPED_SFIELD(BurnedNFTokens,           UINT32,    44)
TYPED_SFIELD(HookStateCount,           UINT32,    45)
TYPED_SFIELD(EmitGeneration,           UINT32,    46)
//                                                47 reserved for Hooks
TYPED_SFIELD(VoteWeight,               UINT32,    48)
TYPED_SFIELD(FirstNFTokenSequence,     UINT32,    50)
TYPED_SFIELD(OracleDocumentID,         UINT32,    51)

// 64-bit integers (common)
TYPED_SFIELD(IndexNext,                UINT64,     1)
TYPED_SFIELD(IndexPrevious,            UINT64,     2)
TYPED_SFIELD(BookNode,                 UINT64,     3)
TYPED_SFIELD(OwnerNode,                UINT64,     4)
TYPED_SFIELD(BaseFee,                  UINT64,     5)
TYPED_SFIELD(ExchangeRate,             UINT64,     6)
TYPED_SFIELD(LowNode,                  UINT64,     7)
TYPED_SFIELD(HighNode,                 UINT64,     8)
TYPED_SFIELD(DestinationNode,          UINT64,     9)
TYPED_SFIELD(Cookie,                   UINT64,    10)
TYPED_SFIELD(ServerVersion,            UINT64,    11)
TYPED_SFIELD(NFTokenOfferNode,         UINT64,    12)
TYPED_SFIELD(EmitBurden,               UINT64,    13)

// 64-bit integers (uncommon)
TYPED_SFIELD(HookOn,                   UINT64,    16)
TYPED_SFIELD(HookInstructionCount,     UINT64,    17)
TYPED_SFIELD(HookReturnCode,           UINT64,    18)
TYPED_SFIELD(ReferenceCount,           UINT64,    19)
TYPED_SFIELD(XChainClaimID,            UINT64,    20)
TYPED_SFIELD(XChainAccountCreateCount, UINT64,    21)
TYPED_SFIELD(XChainAccountClaimCount,  UINT64,    22)
TYPED_SFIELD(AssetPrice,               UINT64,    23)

// 128-bit
TYPED_SFIELD(EmailHash,                UINT128,    1)

// 160-bit (common)
TYPED_SFIELD(TakerPaysCurrency,        UINT160,    1)
TYPED_SFIELD(TakerPaysIssuer,          UINT160,    2)
TYPED_SFIELD(TakerGetsCurrency,        UINT160,    3)
TYPED_SFIELD(TakerGetsIssuer,          UINT160,    4)

// 256-bit (common)
TYPED_SFIELD(LedgerHash,               UINT256,    1)
TYPED_SFIELD(ParentHash,               UINT256,    2)
TYPED_SFIELD(TransactionHash,          UINT256,    3)
TYPED_SFIELD(AccountHash,              UINT256,    4)
TYPED_SFIELD(PreviousTxnID,            UINT256,    5, SField::sMD_DeleteFinal)
TYPED_SFIELD(LedgerIndex,              UINT256,    6)
TYPED_SFIELD(WalletLocator,            UINT256,    7)
TYPED_SFIELD(RootIndex,                UINT256,    8, SField::sMD_Always)
TYPED_SFIELD(AccountTxnID,             UINT256,    9)
TYPED_SFIELD(NFTokenID,                UINT256,   10)
TYPED_SFIELD(EmitParentTxnID,          UINT256,   11)
TYPED_SFIELD(EmitNonce,                UINT256,   12)
TYPED_SFIELD(EmitHookHash,             UINT256,   13)
TYPED_SFIELD(AMMID,                    UINT256,   14)

// 256-bit (uncommon)
TYPED_SFIELD(BookDirectory,            UINT256,   16)
TYPED_SFIELD(InvoiceID,                UINT256,   17)
TYPED_SFIELD(Nickname,                 UINT256,   18)
TYPED_SFIELD(Amendment,                UINT256,   19)
//                                                20 unused
TYPED_SFIELD(Digest,                   UINT256,   21)
TYPED_SFIELD(Channel,                  UINT256,   22)
TYPED_SFIELD(ConsensusHash,            UINT256,   23)
TYPED_SFIELD(CheckID,                  UINT256,   24)
TYPED_SFIELD(ValidatedHash,            UINT256,   25)
TYPED_SFIELD(PreviousPageMin,          UINT256,   26)
TYPED_SFIELD(NextPageMin,              UINT256,   27)
TYPED_SFIELD(NFTokenBuyOffer,          UINT256,   28)
TYPED_SFIELD(NFTokenSellOffer,         UINT256,   29)
TYPED_SFIELD(HookStateKey,             UINT256,   30)
TYPED_SFIELD(HookHash,                 UINT256,   31)
TYPED_SFIELD(HookNamespace,            UINT256,   32)
TYPED_SFIELD(HookSetTxnID,             UINT256,   33)

// currency amount (common)
TYPED_SFIELD(Amount,                   AMOUNT,     1)
TYPED_SFIELD(Balance,                  AMOUNT,     2)
TYPED_SFIELD(LimitAmount,              AMOUNT,     3)
TYPED_SFIELD(TakerPays,                AMOUNT,     4)
TYPED_SFIELD(TakerGets,                AMOUNT,     5)
TYPED_SFIELD(LowLimit,                 AMOUNT,     6)
TYPED_SFIELD(HighLimit,                AMOUNT,     7)
TYPED_SFIELD(Fee,                      AMOUNT,     8)
TYPED_SFIELD(SendMax,                  AMOUNT,     9)
TYPED_SFIELD(DeliverMin,               AMOUNT,    10)
TYPED_SFIELD(Amount2,                  AMOUNT,    11)
TYPED_SFIELD(BidMin,                   AMOUNT,    12)
TYPED_SFIELD(BidMax,                   AMOUNT,    13)

// currency amount (uncommon)
TYPED_SFIELD(MinimumOffer,             AMOUNT,    16)
TYPED_SFIELD(RippleEscrow,             AMOUNT,    17)
TYPED_SFIELD(DeliveredAmount,          AMOUNT,    18)
TYPED_SFIELD(NFTokenBrokerFee,         AMOUNT,    19)

// Reserve 20 & 21 for Hooks.

// currency amount (fees)
TYPED_SFIELD(BaseFeeDrops,             AMOUNT,    22)
TYPED_SFIELD(ReserveBaseDrops,         AMOUNT,    23)
TYPED_SFIELD(ReserveIncrementDrops,    AMOUNT,    24)

// currency amount (AMM)
TYPED_SFIELD(LPTokenOut,               AMOUNT,    25)
TYPED_SFIELD(LPTokenIn,                AMOUNT,    26)
TYPED_SFIELD(EPrice,                   AMOUNT,    27)
TYPED_SFIELD(Price,                    AMOUNT,    28)
TYPED_SFIELD(SignatureReward,          AMOUNT,    29)
TYPED_SFIELD(MinAccountCreateAmount,   AMOUNT,    30)
TYPED_SFIELD(LPTokenBalance,           AMOUNT,    31)

// variable length (common)
TYPED_SFIELD(PublicKey,                VL,         1)
TYPED_SFIELD(MessageKey,               VL,         2)
TYPED_SFIELD(SigningPubKey,            VL,         3)
TYPED_SFIELD(TxnSignature,             VL,         4, SField::sMD_Default, SField::notSigning)
TYPED_SFIELD(URI,                      VL,         5)
TYPED_SFIELD(Signature,                VL,         6, SField::sMD_Default, SField::notSigning)
TYPED_SFIELD(Domain,                   VL,         7)
TYPED_SFIELD(FundCode,                 VL,         8)
TYPED_SFIELD(RemoveCode,               VL,         9)
TYPED_SFIELD(ExpireCode,               VL,        10)
TYPED_SFIELD(CreateCode,               VL,        11)
TYPED_SFIELD(MemoType,                 VL,        12)
TYPED_SFIELD(MemoData,                 VL,        13)
TYPED_SFIELD(MemoFormat,               VL,        14)

// variable length (uncommon)
TYPED_SFIELD(Fulfillment,              VL,        16)
TYPED_SFIELD(Condition,                VL,        17)
TYPED_SFIELD(MasterSignature,          VL,        18, SField::sMD_Default, SField::notSigning)
TYPED_SFIELD(UNLModifyValidator,       VL,        19)
TYPED_SFIELD(ValidatorToDisable,       VL,        20)
TYPED_SFIELD(ValidatorToReEnable,      VL,        21)
TYPED_SFIELD(HookStateData,            VL,        22)
TYPED_SFIELD(HookReturnString,         VL,        23)
TYPED_SFIELD(HookParameterName,        VL,        24)
TYPED_SFIELD(HookParameterValue,       VL,        25)
TYPED_SFIELD(DIDDocument,              VL,        26)
TYPED_SFIELD(Data,                     VL,        27)
TYPED_SFIELD(AssetClass,               VL,        28)
TYPED_SFIELD(Provider,                 VL,        29)

// account (common)
TYPED_SFIELD(Account,                  ACCOUNT,    1)
TYPED_SFIELD(Owner,                    ACCOUNT,    2)
TYPED_SFIELD(Destination,              ACCOUNT,    3)
TYPED_SFIELD(Issuer,                   ACCOUNT,    4)
TYPED_SFIELD(Authorize,                ACCOUNT,    5)
TYPED_SFIELD(Unauthorize,              ACCOUNT,    6)
//                                                 7 unused
TYPED_SFIELD(RegularKey,               ACCOUNT,    8)
TYPED_SFIELD(NFTokenMinter,            ACCOUNT,    9)
TYPED_SFIELD(EmitCallback,             ACCOUNT,   10)

// account (uncommon)
TYPED_SFIELD(HookAccount,              ACCOUNT,   16)
TYPED_SFIELD(OtherChainSource,         ACCOUNT,   18)
TYPED_SFIELD(OtherChainDestination,    ACCOUNT,   19)
TYPED_SFIELD(AttestationSignerAccount, ACCOUNT,   20)
TYPED_SFIELD(AttestationRewardAccount, ACCOUNT,   21)
TYPED_SFIELD(LockingChainDoor,         ACCOUNT,   22)
TYPED_SFIELD(IssuingChainDoor,         ACCOUNT,   23)

// vector of 256-bit
TYPED_SFIELD(Indexes,                  VECTOR256,  1, SField::sMD_Never)
TYPED_SFIELD(Hashes,                   VECTOR256,  2)
TYPED_SFIELD(Amendments,               VECTOR256,  3)
TYPED_SFIELD(NFTokenOffers,            VECTOR256,  4)

// path set
UNTYPED_SFIELD(Paths,                  PATHSET,    1)

// currency
TYPED_SFIELD(BaseAsset,                CURRENCY,   1)
TYPED_SFIELD(QuoteAsset,               CURRENCY,   2)

// issue
TYPED_SFIELD(LockingChainIssue,        ISSUE,      1)
TYPED_SFIELD(IssuingChainIssue,        ISSUE,      2)
TYPED_SFIELD(Asset,                    ISSUE,      3)
TYPED_SFIELD(Asset2,                   ISSUE,      4)

// bridge
TYPED_SFIELD(XChainBridge,             XCHAIN_BRIDGE, 1)

// inner object
// OBJECT/1 is reserved for end of object
UNTYPED_SFIELD(TransactionMetaData,    OBJECT,     2)
UNTYPED_SFIELD(CreatedNode,            OBJECT,     3)
UNTYPED_SFIELD(DeletedNode,            OBJECT,     4)
UNTYPED_SFIELD(ModifiedNode,           OBJECT,     5)
UNTYPED_SFIELD(PreviousFields,         OBJECT,     6)
UNTYPED_SFIELD(FinalFields,            OBJECT,     7)
UNTYPED_SFIELD(NewFields,              OBJECT,     8)
UNTYPED_SFIELD(TemplateEntry,          OBJECT,     9)
UNTYPED_SFIELD(Memo,                   OBJECT,    10)
UNTYPED_SFIELD(SignerEntry,            OBJECT,    11)
UNTYPED_SFIELD(NFToken,                OBJECT,    12)
UNTYPED_SFIELD(EmitDetails,            OBJECT,    13)
UNTYPED_SFIELD(Hook,                   OBJECT,    14)

// inner object (uncommon)
UNTYPED_SFIELD(Signer,                 OBJECT,    16)
//                                                17 unused
UNTYPED_SFIELD(Majority,               OBJECT,    18)
UNTYPED_SFIELD(DisabledValidator,      OBJECT,    19)
UNTYPED_SFIELD(EmittedTxn,             OBJECT,    20)
UNTYPED_SFIELD(HookExecution,          OBJECT,    21)
UNTYPED_SFIELD(HookDefinition,         OBJECT,    22)
UNTYPED_SFIELD(HookParameter,          OBJECT,    23)
UNTYPED_SFIELD(HookGrant,              OBJECT,    24)
UNTYPED_SFIELD(VoteEntry,              OBJECT,    25)
UNTYPED_SFIELD(AuctionSlot,            OBJECT,    26)
UNTYPED_SFIELD(AuthAccount,            OBJECT,    27)
UNTYPED_SFIELD(XChainClaimProofSig,    OBJECT,    28)
UNTYPED_SFIELD(XChainCreateAccountProofSig, OBJECT, 29)
UNTYPED_SFIELD(XChainClaimAttestationCollectionElement, OBJECT, 30)
UNTYPED_SFIELD(XChainCreateAccountAttestationCollectionElement, OBJECT, 31)
UNTYPED_SFIELD(PriceData,              OBJECT,    32)

// array of objects (common)
// ARRAY/1 is reserved for end of array
// sfSigningAccounts has never been used.
//UNTYPED_SFIELD(SigningAccounts,      ARRAY,      2)
UNTYPED_SFIELD(Signers,                ARRAY,      3, SField::sMD_Default, SField::notSigning)
UNTYPED_SFIELD(SignerEntries,          ARRAY,      4)
UNTYPED_SFIELD(Template,               ARRAY,      5)
UNTYPED_SFIELD(Necessary,              ARRAY,      6)
UNTYPED_SFIELD(Sufficient,             ARRAY,      7)
UNTYPED_SFIELD(AffectedNodes,          ARRAY,      8)
UNTYPED_SFIELD(Memos,                  ARRAY,      9)
UNTYPED_SFIELD(NFTokens,               ARRAY,     10)
UNTYPED_SFIELD(Hooks,                  ARRAY,     11)
UNTYPED_SFIELD(VoteSlots,              ARRAY,     12)

// array of objects (uncommon)
UNTYPED_SFIELD(Majorities,             ARRAY,     16)
UNTYPED_SFIELD(DisabledValidators,     ARRAY,     17)
UNTYPED_SFIELD(HookExecutions,         ARRAY,     18)
UNTYPED_SFIELD(HookParameters,         ARRAY,     19)
UNTYPED_SFIELD(HookGrants,             ARRAY,     20)
UNTYPED_SFIELD(XChainClaimAttestations, ARRAY,    21)
UNTYPED_SFIELD(XChainCreateAccountAttestations, ARRAY, 22)
//                                                23 unused
UNTYPED_SFIELD(PriceDataSeries,        ARRAY,     24)
UNTYPED_SFIELD(AuthAccounts,           ARRAY,     25)

// clang-format on
