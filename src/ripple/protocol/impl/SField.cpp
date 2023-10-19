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

#include <ripple/protocol/SField.h>
#include <cassert>
#include <string>
#include <string_view>
#include <utility>

namespace ripple {

// Storage for static const members.
SField::IsSigning const SField::notSigning;
int SField::num = 0;
std::map<int, SField const*> SField::knownCodeToField;

// Give only this translation unit permission to construct SFields
struct SField::private_access_tag_t
{
    explicit private_access_tag_t() = default;
};

static SField::private_access_tag_t access;

template <class T>
template <class... Args>
TypedField<T>::TypedField(private_access_tag_t pat, Args&&... args)
    : SField(pat, std::forward<Args>(args)...)
{
}

// Construct all compile-time SFields, and register them in the knownCodeToField
// database:

// Use macros for most SField construction to enforce naming conventions.
#pragma push_macro("CONSTRUCT_UNTYPED_SFIELD")
#undef CONSTRUCT_UNTYPED_SFIELD

// It would be possible to design the macros so that sfName and txtName would
// be constructed from a single macro parameter.  We chose not to take that
// path because then you cannot grep for the exact SField name and find
// where it is constructed.  These macros allow that grep to succeed.
#define CONSTRUCT_UNTYPED_SFIELD(sfName, txtName, stiSuffix, fieldValue, ...) \
    SField const sfName(                                                      \
        access, STI_##stiSuffix, fieldValue, txtName, ##__VA_ARGS__);         \
    static_assert(                                                            \
        std::string_view(#sfName) == "sf" txtName,                            \
        "Declaration of SField does not match its text name")

#pragma push_macro("CONSTRUCT_TYPED_SFIELD")
#undef CONSTRUCT_TYPED_SFIELD

#define CONSTRUCT_TYPED_SFIELD(sfName, txtName, stiSuffix, fieldValue, ...) \
    SF_##stiSuffix const sfName(                                            \
        access, STI_##stiSuffix, fieldValue, txtName, ##__VA_ARGS__);       \
    static_assert(                                                          \
        std::string_view(#sfName) == "sf" txtName,                          \
        "Declaration of SField does not match its text name")

// clang-format off

// SFields which, for historical reasons, do not follow naming conventions.
SField const sfInvalid(access, -1);
SField const sfGeneric(access, 0);
SField const sfHash(access, STI_UINT256, 257, "hash");
SField const sfIndex(access, STI_UINT256, 258, "index");

// Untyped SFields
CONSTRUCT_UNTYPED_SFIELD(sfLedgerEntry,         "LedgerEntry",          LEDGERENTRY, 257);
CONSTRUCT_UNTYPED_SFIELD(sfTransaction,         "Transaction",          TRANSACTION, 257);
CONSTRUCT_UNTYPED_SFIELD(sfValidation,          "Validation",           VALIDATION,  257);
CONSTRUCT_UNTYPED_SFIELD(sfMetadata,            "Metadata",             METADATA,    257);

// 8-bit integers
CONSTRUCT_TYPED_SFIELD(sfCloseResolution,       "CloseResolution",      UINT8,      1);
CONSTRUCT_TYPED_SFIELD(sfMethod,                "Method",               UINT8,      2);
CONSTRUCT_TYPED_SFIELD(sfTransactionResult,     "TransactionResult",    UINT8,      3);

// 8-bit integers (uncommon)
CONSTRUCT_TYPED_SFIELD(sfTickSize,              "TickSize",             UINT8,     16);
CONSTRUCT_TYPED_SFIELD(sfUNLModifyDisabling,    "UNLModifyDisabling",   UINT8,     17);
CONSTRUCT_TYPED_SFIELD(sfHookResult,            "HookResult",           UINT8,     18);
CONSTRUCT_TYPED_SFIELD(sfWasLockingChainSend,   "WasLockingChainSend",  UINT8,     19);

// 16-bit integers
CONSTRUCT_TYPED_SFIELD(sfLedgerEntryType,       "LedgerEntryType",      UINT16,     1, SField::sMD_Never);
CONSTRUCT_TYPED_SFIELD(sfTransactionType,       "TransactionType",      UINT16,     2);
CONSTRUCT_TYPED_SFIELD(sfSignerWeight,          "SignerWeight",         UINT16,     3);
CONSTRUCT_TYPED_SFIELD(sfTransferFee,           "TransferFee",          UINT16,     4);
CONSTRUCT_TYPED_SFIELD(sfTradingFee,            "TradingFee",           UINT16,     5);
CONSTRUCT_TYPED_SFIELD(sfDiscountedFee,         "DiscountedFee",        UINT16,     6);

// 16-bit integers (uncommon)
CONSTRUCT_TYPED_SFIELD(sfVersion,               "Version",              UINT16,    16);
CONSTRUCT_TYPED_SFIELD(sfHookStateChangeCount,  "HookStateChangeCount", UINT16,    17);
CONSTRUCT_TYPED_SFIELD(sfHookEmitCount,         "HookEmitCount",        UINT16,    18);
CONSTRUCT_TYPED_SFIELD(sfHookExecutionIndex,    "HookExecutionIndex",   UINT16,    19);
CONSTRUCT_TYPED_SFIELD(sfHookApiVersion,        "HookApiVersion",       UINT16,    20);

// 32-bit integers (common)
CONSTRUCT_TYPED_SFIELD(sfNetworkID,             "NetworkID",            UINT32,     1);
CONSTRUCT_TYPED_SFIELD(sfFlags,                 "Flags",                UINT32,     2);
CONSTRUCT_TYPED_SFIELD(sfSourceTag,             "SourceTag",            UINT32,     3);
CONSTRUCT_TYPED_SFIELD(sfSequence,              "Sequence",             UINT32,     4);
CONSTRUCT_TYPED_SFIELD(sfPreviousTxnLgrSeq,     "PreviousTxnLgrSeq",    UINT32,     5, SField::sMD_DeleteFinal);
CONSTRUCT_TYPED_SFIELD(sfLedgerSequence,        "LedgerSequence",       UINT32,     6);
CONSTRUCT_TYPED_SFIELD(sfCloseTime,             "CloseTime",            UINT32,     7);
CONSTRUCT_TYPED_SFIELD(sfParentCloseTime,       "ParentCloseTime",      UINT32,     8);
CONSTRUCT_TYPED_SFIELD(sfSigningTime,           "SigningTime",          UINT32,     9);
CONSTRUCT_TYPED_SFIELD(sfExpiration,            "Expiration",           UINT32,    10);
CONSTRUCT_TYPED_SFIELD(sfTransferRate,          "TransferRate",         UINT32,    11);
CONSTRUCT_TYPED_SFIELD(sfWalletSize,            "WalletSize",           UINT32,    12);
CONSTRUCT_TYPED_SFIELD(sfOwnerCount,            "OwnerCount",           UINT32,    13);
CONSTRUCT_TYPED_SFIELD(sfDestinationTag,        "DestinationTag",       UINT32,    14);

// 32-bit integers (uncommon)
CONSTRUCT_TYPED_SFIELD(sfHighQualityIn,         "HighQualityIn",        UINT32,    16);
CONSTRUCT_TYPED_SFIELD(sfHighQualityOut,        "HighQualityOut",       UINT32,    17);
CONSTRUCT_TYPED_SFIELD(sfLowQualityIn,          "LowQualityIn",         UINT32,    18);
CONSTRUCT_TYPED_SFIELD(sfLowQualityOut,         "LowQualityOut",        UINT32,    19);
CONSTRUCT_TYPED_SFIELD(sfQualityIn,             "QualityIn",            UINT32,    20);
CONSTRUCT_TYPED_SFIELD(sfQualityOut,            "QualityOut",           UINT32,    21);
CONSTRUCT_TYPED_SFIELD(sfStampEscrow,           "StampEscrow",          UINT32,    22);
CONSTRUCT_TYPED_SFIELD(sfBondAmount,            "BondAmount",           UINT32,    23);
CONSTRUCT_TYPED_SFIELD(sfLoadFee,               "LoadFee",              UINT32,    24);
CONSTRUCT_TYPED_SFIELD(sfOfferSequence,         "OfferSequence",        UINT32,    25);
CONSTRUCT_TYPED_SFIELD(sfFirstLedgerSequence,   "FirstLedgerSequence",  UINT32,    26);
CONSTRUCT_TYPED_SFIELD(sfLastLedgerSequence,    "LastLedgerSequence",   UINT32,    27);
CONSTRUCT_TYPED_SFIELD(sfTransactionIndex,      "TransactionIndex",     UINT32,    28);
CONSTRUCT_TYPED_SFIELD(sfOperationLimit,        "OperationLimit",       UINT32,    29);
CONSTRUCT_TYPED_SFIELD(sfReferenceFeeUnits,     "ReferenceFeeUnits",    UINT32,    30);
CONSTRUCT_TYPED_SFIELD(sfReserveBase,           "ReserveBase",          UINT32,    31);
CONSTRUCT_TYPED_SFIELD(sfReserveIncrement,      "ReserveIncrement",     UINT32,    32);
CONSTRUCT_TYPED_SFIELD(sfSetFlag,               "SetFlag",              UINT32,    33);
CONSTRUCT_TYPED_SFIELD(sfClearFlag,             "ClearFlag",            UINT32,    34);
CONSTRUCT_TYPED_SFIELD(sfSignerQuorum,          "SignerQuorum",         UINT32,    35);
CONSTRUCT_TYPED_SFIELD(sfCancelAfter,           "CancelAfter",          UINT32,    36);
CONSTRUCT_TYPED_SFIELD(sfFinishAfter,           "FinishAfter",          UINT32,    37);
CONSTRUCT_TYPED_SFIELD(sfSignerListID,          "SignerListID",         UINT32,    38);
CONSTRUCT_TYPED_SFIELD(sfSettleDelay,           "SettleDelay",          UINT32,    39);
CONSTRUCT_TYPED_SFIELD(sfTicketCount,           "TicketCount",          UINT32,    40);
CONSTRUCT_TYPED_SFIELD(sfTicketSequence,        "TicketSequence",       UINT32,    41);
CONSTRUCT_TYPED_SFIELD(sfNFTokenTaxon,          "NFTokenTaxon",         UINT32,    42);
CONSTRUCT_TYPED_SFIELD(sfMintedNFTokens,        "MintedNFTokens",       UINT32,    43);
CONSTRUCT_TYPED_SFIELD(sfBurnedNFTokens,        "BurnedNFTokens",       UINT32,    44);
CONSTRUCT_TYPED_SFIELD(sfHookStateCount,        "HookStateCount",       UINT32,    45);
CONSTRUCT_TYPED_SFIELD(sfEmitGeneration,        "EmitGeneration",       UINT32,    46);
// 47 is reserved for LockCount(Hooks)
CONSTRUCT_TYPED_SFIELD(sfVoteWeight,            "VoteWeight",           UINT32,    48);
CONSTRUCT_TYPED_SFIELD(sfFirstNFTokenSequence,  "FirstNFTokenSequence", UINT32,    50);

// 64-bit integers (common)
CONSTRUCT_TYPED_SFIELD(sfIndexNext,             "IndexNext",            UINT64,     1);
CONSTRUCT_TYPED_SFIELD(sfIndexPrevious,         "IndexPrevious",        UINT64,     2);
CONSTRUCT_TYPED_SFIELD(sfBookNode,              "BookNode",             UINT64,     3);
CONSTRUCT_TYPED_SFIELD(sfOwnerNode,             "OwnerNode",            UINT64,     4);
CONSTRUCT_TYPED_SFIELD(sfBaseFee,               "BaseFee",              UINT64,     5);
CONSTRUCT_TYPED_SFIELD(sfExchangeRate,          "ExchangeRate",         UINT64,     6);
CONSTRUCT_TYPED_SFIELD(sfLowNode,               "LowNode",              UINT64,     7);
CONSTRUCT_TYPED_SFIELD(sfHighNode,              "HighNode",             UINT64,     8);
CONSTRUCT_TYPED_SFIELD(sfDestinationNode,       "DestinationNode",      UINT64,     9);
CONSTRUCT_TYPED_SFIELD(sfCookie,                "Cookie",               UINT64,    10);
CONSTRUCT_TYPED_SFIELD(sfServerVersion,         "ServerVersion",        UINT64,    11);
CONSTRUCT_TYPED_SFIELD(sfNFTokenOfferNode,      "NFTokenOfferNode",     UINT64,    12);
CONSTRUCT_TYPED_SFIELD(sfEmitBurden,            "EmitBurden",           UINT64,    13);

// 64-bit integers (uncommon)
CONSTRUCT_TYPED_SFIELD(sfHookOn,                   "HookOn",                   UINT64, 16);
CONSTRUCT_TYPED_SFIELD(sfHookInstructionCount,     "HookInstructionCount",     UINT64, 17);
CONSTRUCT_TYPED_SFIELD(sfHookReturnCode,           "HookReturnCode",           UINT64, 18);
CONSTRUCT_TYPED_SFIELD(sfReferenceCount,           "ReferenceCount",           UINT64, 19);
CONSTRUCT_TYPED_SFIELD(sfXChainClaimID,            "XChainClaimID",            UINT64, 20);
CONSTRUCT_TYPED_SFIELD(sfXChainAccountCreateCount, "XChainAccountCreateCount", UINT64, 21);
CONSTRUCT_TYPED_SFIELD(sfXChainAccountClaimCount,  "XChainAccountClaimCount",  UINT64, 22);

// 128-bit
CONSTRUCT_TYPED_SFIELD(sfEmailHash,             "EmailHash",            UINT128,    1);

// 160-bit (common)
CONSTRUCT_TYPED_SFIELD(sfTakerPaysCurrency,     "TakerPaysCurrency",    UINT160,    1);
CONSTRUCT_TYPED_SFIELD(sfTakerPaysIssuer,       "TakerPaysIssuer",      UINT160,    2);
CONSTRUCT_TYPED_SFIELD(sfTakerGetsCurrency,     "TakerGetsCurrency",    UINT160,    3);
CONSTRUCT_TYPED_SFIELD(sfTakerGetsIssuer,       "TakerGetsIssuer",      UINT160,    4);

// 256-bit (common)
CONSTRUCT_TYPED_SFIELD(sfLedgerHash,            "LedgerHash",           UINT256,    1);
CONSTRUCT_TYPED_SFIELD(sfParentHash,            "ParentHash",           UINT256,    2);
CONSTRUCT_TYPED_SFIELD(sfTransactionHash,       "TransactionHash",      UINT256,    3);
CONSTRUCT_TYPED_SFIELD(sfAccountHash,           "AccountHash",          UINT256,    4);
CONSTRUCT_TYPED_SFIELD(sfPreviousTxnID,         "PreviousTxnID",        UINT256,    5, SField::sMD_DeleteFinal);
CONSTRUCT_TYPED_SFIELD(sfLedgerIndex,           "LedgerIndex",          UINT256,    6);
CONSTRUCT_TYPED_SFIELD(sfWalletLocator,         "WalletLocator",        UINT256,    7);
CONSTRUCT_TYPED_SFIELD(sfRootIndex,             "RootIndex",            UINT256,    8, SField::sMD_Always);
CONSTRUCT_TYPED_SFIELD(sfAccountTxnID,          "AccountTxnID",         UINT256,    9);
CONSTRUCT_TYPED_SFIELD(sfNFTokenID,             "NFTokenID",            UINT256,   10);
CONSTRUCT_TYPED_SFIELD(sfEmitParentTxnID,       "EmitParentTxnID",      UINT256,   11);
CONSTRUCT_TYPED_SFIELD(sfEmitNonce,             "EmitNonce",            UINT256,   12);
CONSTRUCT_TYPED_SFIELD(sfEmitHookHash,          "EmitHookHash",         UINT256,   13);
CONSTRUCT_TYPED_SFIELD(sfAMMID,                 "AMMID",                UINT256,   14);

// 256-bit (uncommon)
CONSTRUCT_TYPED_SFIELD(sfBookDirectory,         "BookDirectory",        UINT256,   16);
CONSTRUCT_TYPED_SFIELD(sfInvoiceID,             "InvoiceID",            UINT256,   17);
CONSTRUCT_TYPED_SFIELD(sfNickname,              "Nickname",             UINT256,   18);
CONSTRUCT_TYPED_SFIELD(sfAmendment,             "Amendment",            UINT256,   19);
//                                                                                 20 is currently unused
CONSTRUCT_TYPED_SFIELD(sfDigest,                "Digest",               UINT256,   21);
CONSTRUCT_TYPED_SFIELD(sfChannel,               "Channel",              UINT256,   22);
CONSTRUCT_TYPED_SFIELD(sfConsensusHash,         "ConsensusHash",        UINT256,   23);
CONSTRUCT_TYPED_SFIELD(sfCheckID,               "CheckID",              UINT256,   24);
CONSTRUCT_TYPED_SFIELD(sfValidatedHash,         "ValidatedHash",        UINT256,   25);
CONSTRUCT_TYPED_SFIELD(sfPreviousPageMin,       "PreviousPageMin",      UINT256,   26);
CONSTRUCT_TYPED_SFIELD(sfNextPageMin,           "NextPageMin",          UINT256,   27);
CONSTRUCT_TYPED_SFIELD(sfNFTokenBuyOffer,       "NFTokenBuyOffer",      UINT256,   28);
CONSTRUCT_TYPED_SFIELD(sfNFTokenSellOffer,      "NFTokenSellOffer",     UINT256,   29);
CONSTRUCT_TYPED_SFIELD(sfHookStateKey,          "HookStateKey",         UINT256,   30);
CONSTRUCT_TYPED_SFIELD(sfHookHash,              "HookHash",             UINT256,   31);
CONSTRUCT_TYPED_SFIELD(sfHookNamespace,         "HookNamespace",        UINT256,   32);
CONSTRUCT_TYPED_SFIELD(sfHookSetTxnID,          "HookSetTxnID",         UINT256,   33);

// currency amount (common)
CONSTRUCT_TYPED_SFIELD(sfAmount,                "Amount",               AMOUNT,     1);
CONSTRUCT_TYPED_SFIELD(sfBalance,               "Balance",              AMOUNT,     2);
CONSTRUCT_TYPED_SFIELD(sfLimitAmount,           "LimitAmount",          AMOUNT,     3);
CONSTRUCT_TYPED_SFIELD(sfTakerPays,             "TakerPays",            AMOUNT,     4);
CONSTRUCT_TYPED_SFIELD(sfTakerGets,             "TakerGets",            AMOUNT,     5);
CONSTRUCT_TYPED_SFIELD(sfLowLimit,              "LowLimit",             AMOUNT,     6);
CONSTRUCT_TYPED_SFIELD(sfHighLimit,             "HighLimit",            AMOUNT,     7);
CONSTRUCT_TYPED_SFIELD(sfFee,                   "Fee",                  AMOUNT,     8);
CONSTRUCT_TYPED_SFIELD(sfSendMax,               "SendMax",              AMOUNT,     9);
CONSTRUCT_TYPED_SFIELD(sfDeliverMin,            "DeliverMin",           AMOUNT,    10);
CONSTRUCT_TYPED_SFIELD(sfAmount2,               "Amount2",              AMOUNT,    11);
CONSTRUCT_TYPED_SFIELD(sfBidMin,                "BidMin",               AMOUNT,    12);
CONSTRUCT_TYPED_SFIELD(sfBidMax,                "BidMax",               AMOUNT,    13);

// currency amount (uncommon)
CONSTRUCT_TYPED_SFIELD(sfMinimumOffer,          "MinimumOffer",         AMOUNT,    16);
CONSTRUCT_TYPED_SFIELD(sfRippleEscrow,          "RippleEscrow",         AMOUNT,    17);
CONSTRUCT_TYPED_SFIELD(sfDeliveredAmount,       "DeliveredAmount",      AMOUNT,    18);
CONSTRUCT_TYPED_SFIELD(sfNFTokenBrokerFee,      "NFTokenBrokerFee",     AMOUNT,    19);

// Reserve 20 & 21 for Hooks

// currency amount (fees)
CONSTRUCT_TYPED_SFIELD(sfBaseFeeDrops,            "BaseFeeDrops",           AMOUNT,    22);
CONSTRUCT_TYPED_SFIELD(sfReserveBaseDrops,        "ReserveBaseDrops",       AMOUNT,    23);
CONSTRUCT_TYPED_SFIELD(sfReserveIncrementDrops,   "ReserveIncrementDrops",  AMOUNT,    24);

// currency amount (AMM)
CONSTRUCT_TYPED_SFIELD(sfLPTokenOut,            "LPTokenOut",           AMOUNT,    25);
CONSTRUCT_TYPED_SFIELD(sfLPTokenIn,             "LPTokenIn",            AMOUNT,    26);
CONSTRUCT_TYPED_SFIELD(sfEPrice,                "EPrice",               AMOUNT,    27);
CONSTRUCT_TYPED_SFIELD(sfPrice,                 "Price",                AMOUNT,    28);
CONSTRUCT_TYPED_SFIELD(sfSignatureReward,       "SignatureReward",      AMOUNT,    29);
CONSTRUCT_TYPED_SFIELD(sfMinAccountCreateAmount, "MinAccountCreateAmount", AMOUNT, 30);
CONSTRUCT_TYPED_SFIELD(sfLPTokenBalance,        "LPTokenBalance",       AMOUNT,    31);

// variable length (common)
CONSTRUCT_TYPED_SFIELD(sfPublicKey,             "PublicKey",            VL,         1);
CONSTRUCT_TYPED_SFIELD(sfMessageKey,            "MessageKey",           VL,         2);
CONSTRUCT_TYPED_SFIELD(sfSigningPubKey,         "SigningPubKey",        VL,         3);
CONSTRUCT_TYPED_SFIELD(sfTxnSignature,          "TxnSignature",         VL,         4, SField::sMD_Default, SField::notSigning);
CONSTRUCT_TYPED_SFIELD(sfURI,                   "URI",                  VL,         5);
CONSTRUCT_TYPED_SFIELD(sfSignature,             "Signature",            VL,         6, SField::sMD_Default, SField::notSigning);
CONSTRUCT_TYPED_SFIELD(sfDomain,                "Domain",               VL,         7);
CONSTRUCT_TYPED_SFIELD(sfFundCode,              "FundCode",             VL,         8);
CONSTRUCT_TYPED_SFIELD(sfRemoveCode,            "RemoveCode",           VL,         9);
CONSTRUCT_TYPED_SFIELD(sfExpireCode,            "ExpireCode",           VL,        10);
CONSTRUCT_TYPED_SFIELD(sfCreateCode,            "CreateCode",           VL,        11);
CONSTRUCT_TYPED_SFIELD(sfMemoType,              "MemoType",             VL,        12);
CONSTRUCT_TYPED_SFIELD(sfMemoData,              "MemoData",             VL,        13);
CONSTRUCT_TYPED_SFIELD(sfMemoFormat,            "MemoFormat",           VL,        14);

// variable length (uncommon)
CONSTRUCT_TYPED_SFIELD(sfFulfillment,           "Fulfillment",          VL,        16);
CONSTRUCT_TYPED_SFIELD(sfCondition,             "Condition",            VL,        17);
CONSTRUCT_TYPED_SFIELD(sfMasterSignature,       "MasterSignature",      VL,        18, SField::sMD_Default, SField::notSigning);
CONSTRUCT_TYPED_SFIELD(sfUNLModifyValidator,    "UNLModifyValidator",   VL,        19);
CONSTRUCT_TYPED_SFIELD(sfValidatorToDisable,    "ValidatorToDisable",   VL,        20);
CONSTRUCT_TYPED_SFIELD(sfValidatorToReEnable,   "ValidatorToReEnable",  VL,        21);
CONSTRUCT_TYPED_SFIELD(sfHookStateData,         "HookStateData",        VL,        22);
CONSTRUCT_TYPED_SFIELD(sfHookReturnString,      "HookReturnString",     VL,        23);
CONSTRUCT_TYPED_SFIELD(sfHookParameterName,     "HookParameterName",    VL,        24);
CONSTRUCT_TYPED_SFIELD(sfHookParameterValue,    "HookParameterValue",   VL,        25);
CONSTRUCT_TYPED_SFIELD(sfDIDDocument,           "DIDDocument",          VL,        26);
CONSTRUCT_TYPED_SFIELD(sfData,                  "Data",                 VL,        27);

// account
CONSTRUCT_TYPED_SFIELD(sfAccount,               "Account",              ACCOUNT,    1);
CONSTRUCT_TYPED_SFIELD(sfOwner,                 "Owner",                ACCOUNT,    2);
CONSTRUCT_TYPED_SFIELD(sfDestination,           "Destination",          ACCOUNT,    3);
CONSTRUCT_TYPED_SFIELD(sfIssuer,                "Issuer",               ACCOUNT,    4);
CONSTRUCT_TYPED_SFIELD(sfAuthorize,             "Authorize",            ACCOUNT,    5);
CONSTRUCT_TYPED_SFIELD(sfUnauthorize,           "Unauthorize",          ACCOUNT,    6);
//                                                                                  7 is currently unused
CONSTRUCT_TYPED_SFIELD(sfRegularKey,            "RegularKey",           ACCOUNT,    8);
CONSTRUCT_TYPED_SFIELD(sfNFTokenMinter,         "NFTokenMinter",        ACCOUNT,    9);
CONSTRUCT_TYPED_SFIELD(sfEmitCallback,          "EmitCallback",         ACCOUNT,   10);

// account (uncommon)
CONSTRUCT_TYPED_SFIELD(sfHookAccount,           "HookAccount",          ACCOUNT,   16);
CONSTRUCT_TYPED_SFIELD(sfOtherChainSource,      "OtherChainSource",     ACCOUNT,   18);
CONSTRUCT_TYPED_SFIELD(sfOtherChainDestination, "OtherChainDestination",ACCOUNT,   19);
CONSTRUCT_TYPED_SFIELD(sfAttestationSignerAccount, "AttestationSignerAccount", ACCOUNT, 20);
CONSTRUCT_TYPED_SFIELD(sfAttestationRewardAccount, "AttestationRewardAccount", ACCOUNT, 21);
CONSTRUCT_TYPED_SFIELD(sfLockingChainDoor,      "LockingChainDoor",     ACCOUNT,   22);
CONSTRUCT_TYPED_SFIELD(sfIssuingChainDoor,      "IssuingChainDoor",     ACCOUNT,   23);

// vector of 256-bit
CONSTRUCT_TYPED_SFIELD(sfIndexes,               "Indexes",              VECTOR256,  1, SField::sMD_Never);
CONSTRUCT_TYPED_SFIELD(sfHashes,                "Hashes",               VECTOR256,  2);
CONSTRUCT_TYPED_SFIELD(sfAmendments,            "Amendments",           VECTOR256,  3);
CONSTRUCT_TYPED_SFIELD(sfNFTokenOffers,         "NFTokenOffers",        VECTOR256,  4);

// path set
CONSTRUCT_UNTYPED_SFIELD(sfPaths,               "Paths",                PATHSET,    1);

// issue
CONSTRUCT_TYPED_SFIELD(sfLockingChainIssue,     "LockingChainIssue",    ISSUE,      1);
CONSTRUCT_TYPED_SFIELD(sfIssuingChainIssue,     "IssuingChainIssue",    ISSUE,      2);
CONSTRUCT_TYPED_SFIELD(sfAsset,                 "Asset",                ISSUE,      3);
CONSTRUCT_TYPED_SFIELD(sfAsset2,                "Asset2",               ISSUE,      4);

// Bridge
CONSTRUCT_TYPED_SFIELD(sfXChainBridge,           "XChainBridge",        XCHAIN_BRIDGE,
                                                                               1);
// inner object
// OBJECT/1 is reserved for end of object
CONSTRUCT_UNTYPED_SFIELD(sfTransactionMetaData, "TransactionMetaData",  OBJECT,     2);
CONSTRUCT_UNTYPED_SFIELD(sfCreatedNode,         "CreatedNode",          OBJECT,     3);
CONSTRUCT_UNTYPED_SFIELD(sfDeletedNode,         "DeletedNode",          OBJECT,     4);
CONSTRUCT_UNTYPED_SFIELD(sfModifiedNode,        "ModifiedNode",         OBJECT,     5);
CONSTRUCT_UNTYPED_SFIELD(sfPreviousFields,      "PreviousFields",       OBJECT,     6);
CONSTRUCT_UNTYPED_SFIELD(sfFinalFields,         "FinalFields",          OBJECT,     7);
CONSTRUCT_UNTYPED_SFIELD(sfNewFields,           "NewFields",            OBJECT,     8);
CONSTRUCT_UNTYPED_SFIELD(sfTemplateEntry,       "TemplateEntry",        OBJECT,     9);
CONSTRUCT_UNTYPED_SFIELD(sfMemo,                "Memo",                 OBJECT,    10);
CONSTRUCT_UNTYPED_SFIELD(sfSignerEntry,         "SignerEntry",          OBJECT,    11);
CONSTRUCT_UNTYPED_SFIELD(sfNFToken,             "NFToken",              OBJECT,    12);
CONSTRUCT_UNTYPED_SFIELD(sfEmitDetails,         "EmitDetails",          OBJECT,    13);
CONSTRUCT_UNTYPED_SFIELD(sfHook,                "Hook",                 OBJECT,    14);

// inner object (uncommon)
CONSTRUCT_UNTYPED_SFIELD(sfSigner,              "Signer",               OBJECT,    16);
//                                                                                 17 has not been used yet
CONSTRUCT_UNTYPED_SFIELD(sfMajority,            "Majority",             OBJECT,    18);
CONSTRUCT_UNTYPED_SFIELD(sfDisabledValidator,   "DisabledValidator",    OBJECT,    19);
CONSTRUCT_UNTYPED_SFIELD(sfEmittedTxn,          "EmittedTxn",           OBJECT,    20);
CONSTRUCT_UNTYPED_SFIELD(sfHookExecution,       "HookExecution",        OBJECT,    21);
CONSTRUCT_UNTYPED_SFIELD(sfHookDefinition,      "HookDefinition",       OBJECT,    22);
CONSTRUCT_UNTYPED_SFIELD(sfHookParameter,       "HookParameter",        OBJECT,    23);
CONSTRUCT_UNTYPED_SFIELD(sfHookGrant,           "HookGrant",            OBJECT,    24);
CONSTRUCT_UNTYPED_SFIELD(sfVoteEntry,           "VoteEntry",            OBJECT,    25);
CONSTRUCT_UNTYPED_SFIELD(sfAuctionSlot,         "AuctionSlot",          OBJECT,    26);
CONSTRUCT_UNTYPED_SFIELD(sfAuthAccount,         "AuthAccount",          OBJECT,    27);
CONSTRUCT_UNTYPED_SFIELD(sfXChainClaimProofSig, "XChainClaimProofSig",  OBJECT,    28);
CONSTRUCT_UNTYPED_SFIELD(sfXChainCreateAccountProofSig,
                                                "XChainCreateAccountProofSig",
                                                                        OBJECT,    29);
CONSTRUCT_UNTYPED_SFIELD(sfXChainClaimAttestationCollectionElement,
                                                 "XChainClaimAttestationCollectionElement",
                                                                        OBJECT,    30);
CONSTRUCT_UNTYPED_SFIELD(sfXChainCreateAccountAttestationCollectionElement,
                                                 "XChainCreateAccountAttestationCollectionElement",
                                                                        OBJECT,    31);

// array of objects
//                                                                            ARRAY/1 is reserved for end of array
//                                                                                  2 has never been used
CONSTRUCT_UNTYPED_SFIELD(sfSigners,             "Signers",              ARRAY,      3, SField::sMD_Default, SField::notSigning);
CONSTRUCT_UNTYPED_SFIELD(sfSignerEntries,       "SignerEntries",        ARRAY,      4);
CONSTRUCT_UNTYPED_SFIELD(sfTemplate,            "Template",             ARRAY,      5);
CONSTRUCT_UNTYPED_SFIELD(sfNecessary,           "Necessary",            ARRAY,      6);
CONSTRUCT_UNTYPED_SFIELD(sfSufficient,          "Sufficient",           ARRAY,      7);
CONSTRUCT_UNTYPED_SFIELD(sfAffectedNodes,       "AffectedNodes",        ARRAY,      8);
CONSTRUCT_UNTYPED_SFIELD(sfMemos,               "Memos",                ARRAY,      9);
CONSTRUCT_UNTYPED_SFIELD(sfNFTokens,            "NFTokens",             ARRAY,     10);
CONSTRUCT_UNTYPED_SFIELD(sfHooks,               "Hooks",                ARRAY,     11);
CONSTRUCT_UNTYPED_SFIELD(sfVoteSlots,           "VoteSlots",            ARRAY,     12);

// array of objects (uncommon)
CONSTRUCT_UNTYPED_SFIELD(sfMajorities,          "Majorities",           ARRAY,     16);
CONSTRUCT_UNTYPED_SFIELD(sfDisabledValidators,  "DisabledValidators",   ARRAY,     17);
CONSTRUCT_UNTYPED_SFIELD(sfHookExecutions,      "HookExecutions",       ARRAY,     18);
CONSTRUCT_UNTYPED_SFIELD(sfHookParameters,      "HookParameters",       ARRAY,     19);
CONSTRUCT_UNTYPED_SFIELD(sfHookGrants,          "HookGrants",           ARRAY,     20);
CONSTRUCT_UNTYPED_SFIELD(sfXChainClaimAttestations,
                                                 "XChainClaimAttestations",
                                                                        ARRAY,     21);
CONSTRUCT_UNTYPED_SFIELD(sfXChainCreateAccountAttestations,
                                                 "XChainCreateAccountAttestations",
                                                                        ARRAY,     22);
// 23 and 24 are unused and available for use
CONSTRUCT_UNTYPED_SFIELD(sfAuthAccounts,        "AuthAccounts",         ARRAY,     25);

// clang-format on

#undef CONSTRUCT_TYPED_SFIELD
#undef CONSTRUCT_UNTYPED_SFIELD

#pragma pop_macro("CONSTRUCT_TYPED_SFIELD")
#pragma pop_macro("CONSTRUCT_UNTYPED_SFIELD")

SField::SField(
    private_access_tag_t,
    SerializedTypeID tid,
    int fv,
    const char* fn,
    int meta,
    IsSigning signing)
    : fieldCode(field_code(tid, fv))
    , fieldType(tid)
    , fieldValue(fv)
    , fieldName(fn)
    , fieldMeta(meta)
    , fieldNum(++num)
    , signingField(signing)
    , jsonName(fieldName.c_str())
{
    knownCodeToField[fieldCode] = this;
}

SField::SField(private_access_tag_t, int fc)
    : fieldCode(fc)
    , fieldType(STI_UNKNOWN)
    , fieldValue(0)
    , fieldMeta(sMD_Never)
    , fieldNum(++num)
    , signingField(IsSigning::yes)
    , jsonName(fieldName.c_str())
{
    knownCodeToField[fieldCode] = this;
}

SField const&
SField::getField(int code)
{
    auto it = knownCodeToField.find(code);

    if (it != knownCodeToField.end())
    {
        return *(it->second);
    }
    return sfInvalid;
}

int
SField::compare(SField const& f1, SField const& f2)
{
    // -1 = f1 comes before f2, 0 = illegal combination, 1 = f1 comes after f2
    if ((f1.fieldCode <= 0) || (f2.fieldCode <= 0))
        return 0;

    if (f1.fieldCode < f2.fieldCode)
        return -1;

    if (f2.fieldCode < f1.fieldCode)
        return 1;

    return 0;
}

SField const&
SField::getField(std::string const& fieldName)
{
    for (auto const& [_, f] : knownCodeToField)
    {
        (void)_;
        if (f->fieldName == fieldName)
            return *f;
    }
    return sfInvalid;
}

}  // namespace ripple
