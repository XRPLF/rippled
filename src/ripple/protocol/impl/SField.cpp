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

#include <BeastConfig.h>
#include <ripple/protocol/SField.h>
#include <cassert>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace ripple {

// These must stay at the top of this file, and in this order
// Files-cope statics are preferred here because the SFields must be
// file-scope.  The following 3 objects must have scope prior to
// the file-scope SFields.
static std::mutex SField_mutex;
static std::map<int, SField const*> knownCodeToField;
static std::map<int, std::unique_ptr<SField const>> unknownCodeToField;

// Storage for static const member.
SField::IsSigning const SField::notSigning;

int SField::num = 0;

using StaticScopedLockType = std::lock_guard <std::mutex>;

// Give this translation unit only, permission to construct SFields
struct SField::make
{
    template <class ...Args>
    static SField one(SField const* p, Args&& ...args)
    {
        SField result(std::forward<Args>(args)...);
        knownCodeToField[result.fieldCode] = p;
        return result;
    }

    template <class T, class ...Args>
    static TypedField<T> one(SField const* p, Args&& ...args)
    {
        TypedField<T> result(std::forward<Args>(args)...);
        knownCodeToField[result.fieldCode] = p;
        return result;
    }
};

using make = SField::make;

// Construct all compile-time SFields, and register them in the knownCodeToField
// database:

SField const sfInvalid     = make::one(&sfInvalid, -1);
SField const sfGeneric     = make::one(&sfGeneric, 0);
SField const sfLedgerEntry = make::one(&sfLedgerEntry, STI_LEDGERENTRY, 257, "LedgerEntry");
SField const sfTransaction = make::one(&sfTransaction, STI_TRANSACTION, 257, "Transaction");
SField const sfValidation  = make::one(&sfValidation,  STI_VALIDATION,  257, "Validation");
SField const sfMetadata    = make::one(&sfMetadata,    STI_METADATA,    257, "Metadata");
SField const sfHash        = make::one(&sfHash,        STI_HASH256,     257, "hash");
SField const sfIndex       = make::one(&sfIndex,       STI_HASH256,     258, "index");

// 8-bit integers
SF_U8 const sfCloseResolution   = make::one<SF_U8::type>(&sfCloseResolution,   STI_UINT8, 1, "CloseResolution");
SF_U8 const sfMethod            = make::one<SF_U8::type>(&sfMethod,            STI_UINT8, 2, "Method");
SF_U8 const sfTransactionResult = make::one<SF_U8::type>(&sfTransactionResult, STI_UINT8, 3, "TransactionResult");

// 8-bit integers (uncommon)
SF_U8 const sfTickSize          = make::one<SF_U8::type>(&sfTickSize,          STI_UINT8, 16, "TickSize");

// 16-bit integers
SF_U16 const sfLedgerEntryType = make::one<SF_U16::type>(&sfLedgerEntryType, STI_UINT16, 1, "LedgerEntryType", SField::sMD_Never);
SF_U16 const sfTransactionType = make::one<SF_U16::type>(&sfTransactionType, STI_UINT16, 2, "TransactionType");
SF_U16 const sfSignerWeight    = make::one<SF_U16::type>(&sfSignerWeight,    STI_UINT16, 3, "SignerWeight");

// 32-bit integers (common)
SF_U32 const sfFlags             = make::one<SF_U32::type>(&sfFlags,             STI_UINT32,  2, "Flags");
SF_U32 const sfSourceTag         = make::one<SF_U32::type>(&sfSourceTag,         STI_UINT32,  3, "SourceTag");
SF_U32 const sfSequence          = make::one<SF_U32::type>(&sfSequence,          STI_UINT32,  4, "Sequence");
SF_U32 const sfPreviousTxnLgrSeq = make::one<SF_U32::type>(&sfPreviousTxnLgrSeq, STI_UINT32,  5, "PreviousTxnLgrSeq", SField::sMD_DeleteFinal);
SF_U32 const sfLedgerSequence    = make::one<SF_U32::type>(&sfLedgerSequence,    STI_UINT32,  6, "LedgerSequence");
SF_U32 const sfCloseTime         = make::one<SF_U32::type>(&sfCloseTime,         STI_UINT32,  7, "CloseTime");
SF_U32 const sfParentCloseTime   = make::one<SF_U32::type>(&sfParentCloseTime,   STI_UINT32,  8, "ParentCloseTime");
SF_U32 const sfSigningTime       = make::one<SF_U32::type>(&sfSigningTime,       STI_UINT32,  9, "SigningTime");
SF_U32 const sfExpiration        = make::one<SF_U32::type>(&sfExpiration,        STI_UINT32, 10, "Expiration");
SF_U32 const sfTransferRate      = make::one<SF_U32::type>(&sfTransferRate,      STI_UINT32, 11, "TransferRate");
SF_U32 const sfWalletSize        = make::one<SF_U32::type>(&sfWalletSize,        STI_UINT32, 12, "WalletSize");
SF_U32 const sfOwnerCount        = make::one<SF_U32::type>(&sfOwnerCount,        STI_UINT32, 13, "OwnerCount");
SF_U32 const sfDestinationTag    = make::one<SF_U32::type>(&sfDestinationTag,    STI_UINT32, 14, "DestinationTag");

// 32-bit integers (uncommon)
SF_U32 const sfHighQualityIn       = make::one<SF_U32::type>(&sfHighQualityIn,       STI_UINT32, 16, "HighQualityIn");
SF_U32 const sfHighQualityOut      = make::one<SF_U32::type>(&sfHighQualityOut,      STI_UINT32, 17, "HighQualityOut");
SF_U32 const sfLowQualityIn        = make::one<SF_U32::type>(&sfLowQualityIn,        STI_UINT32, 18, "LowQualityIn");
SF_U32 const sfLowQualityOut       = make::one<SF_U32::type>(&sfLowQualityOut,       STI_UINT32, 19, "LowQualityOut");
SF_U32 const sfQualityIn           = make::one<SF_U32::type>(&sfQualityIn,           STI_UINT32, 20, "QualityIn");
SF_U32 const sfQualityOut          = make::one<SF_U32::type>(&sfQualityOut,          STI_UINT32, 21, "QualityOut");
SF_U32 const sfStampEscrow         = make::one<SF_U32::type>(&sfStampEscrow,         STI_UINT32, 22, "StampEscrow");
SF_U32 const sfBondAmount          = make::one<SF_U32::type>(&sfBondAmount,          STI_UINT32, 23, "BondAmount");
SF_U32 const sfLoadFee             = make::one<SF_U32::type>(&sfLoadFee,             STI_UINT32, 24, "LoadFee");
SF_U32 const sfOfferSequence       = make::one<SF_U32::type>(&sfOfferSequence,       STI_UINT32, 25, "OfferSequence");
SF_U32 const sfFirstLedgerSequence = make::one<SF_U32::type>(&sfFirstLedgerSequence, STI_UINT32, 26, "FirstLedgerSequence");  // Deprecated: do not use
SF_U32 const sfLastLedgerSequence  = make::one<SF_U32::type>(&sfLastLedgerSequence,  STI_UINT32, 27, "LastLedgerSequence");
SF_U32 const sfTransactionIndex    = make::one<SF_U32::type>(&sfTransactionIndex,    STI_UINT32, 28, "TransactionIndex");
SF_U32 const sfOperationLimit      = make::one<SF_U32::type>(&sfOperationLimit,      STI_UINT32, 29, "OperationLimit");
SF_U32 const sfReferenceFeeUnits   = make::one<SF_U32::type>(&sfReferenceFeeUnits,   STI_UINT32, 30, "ReferenceFeeUnits");
SF_U32 const sfReserveBase         = make::one<SF_U32::type>(&sfReserveBase,         STI_UINT32, 31, "ReserveBase");
SF_U32 const sfReserveIncrement    = make::one<SF_U32::type>(&sfReserveIncrement,    STI_UINT32, 32, "ReserveIncrement");
SF_U32 const sfSetFlag             = make::one<SF_U32::type>(&sfSetFlag,             STI_UINT32, 33, "SetFlag");
SF_U32 const sfClearFlag           = make::one<SF_U32::type>(&sfClearFlag,           STI_UINT32, 34, "ClearFlag");
SF_U32 const sfSignerQuorum        = make::one<SF_U32::type>(&sfSignerQuorum,        STI_UINT32, 35, "SignerQuorum");
SF_U32 const sfCancelAfter         = make::one<SF_U32::type>(&sfCancelAfter,         STI_UINT32, 36, "CancelAfter");
SF_U32 const sfFinishAfter         = make::one<SF_U32::type>(&sfFinishAfter,         STI_UINT32, 37, "FinishAfter");
SF_U32 const sfSignerListID        = make::one<SF_U32::type>(&sfSignerListID,        STI_UINT32, 38, "SignerListID");
SF_U32 const sfSettleDelay         = make::one<SF_U32::type>(&sfSettleDelay,         STI_UINT32, 39, "SettleDelay");

// 64-bit integers
SF_U64 const sfIndexNext        = make::one<SF_U64::type>(&sfIndexNext,        STI_UINT64, 1, "IndexNext");
SF_U64 const sfIndexPrevious    = make::one<SF_U64::type>(&sfIndexPrevious,    STI_UINT64, 2, "IndexPrevious");
SF_U64 const sfBookNode         = make::one<SF_U64::type>(&sfBookNode,         STI_UINT64, 3, "BookNode");
SF_U64 const sfOwnerNode        = make::one<SF_U64::type>(&sfOwnerNode,        STI_UINT64, 4, "OwnerNode");
SF_U64 const sfBaseFee          = make::one<SF_U64::type>(&sfBaseFee,          STI_UINT64, 5, "BaseFee");
SF_U64 const sfExchangeRate     = make::one<SF_U64::type>(&sfExchangeRate,     STI_UINT64, 6, "ExchangeRate");
SF_U64 const sfLowNode          = make::one<SF_U64::type>(&sfLowNode,          STI_UINT64, 7, "LowNode");
SF_U64 const sfHighNode         = make::one<SF_U64::type>(&sfHighNode,         STI_UINT64, 8, "HighNode");
SF_U64 const sfDestinationNode  = make::one<SF_U64::type>(&sfDestinationNode,  STI_UINT64, 9, "DestinationNode");

// 128-bit
SF_U128 const sfEmailHash = make::one<SF_U128::type>(&sfEmailHash, STI_HASH128, 1, "EmailHash");

// 160-bit (common)
SF_U160 const sfTakerPaysCurrency = make::one<SF_U160::type>(&sfTakerPaysCurrency, STI_HASH160, 1, "TakerPaysCurrency");
SF_U160 const sfTakerPaysIssuer   = make::one<SF_U160::type>(&sfTakerPaysIssuer,   STI_HASH160, 2, "TakerPaysIssuer");
SF_U160 const sfTakerGetsCurrency = make::one<SF_U160::type>(&sfTakerGetsCurrency, STI_HASH160, 3, "TakerGetsCurrency");
SF_U160 const sfTakerGetsIssuer   = make::one<SF_U160::type>(&sfTakerGetsIssuer,   STI_HASH160, 4, "TakerGetsIssuer");

// 256-bit (common)
SF_U256 const sfLedgerHash      = make::one<SF_U256::type>(&sfLedgerHash,      STI_HASH256, 1, "LedgerHash");
SF_U256 const sfParentHash      = make::one<SF_U256::type>(&sfParentHash,      STI_HASH256, 2, "ParentHash");
SF_U256 const sfTransactionHash = make::one<SF_U256::type>(&sfTransactionHash, STI_HASH256, 3, "TransactionHash");
SF_U256 const sfAccountHash     = make::one<SF_U256::type>(&sfAccountHash,     STI_HASH256, 4, "AccountHash");
SF_U256 const sfPreviousTxnID   = make::one<SF_U256::type>(&sfPreviousTxnID,   STI_HASH256, 5, "PreviousTxnID", SField::sMD_DeleteFinal);
SF_U256 const sfLedgerIndex     = make::one<SF_U256::type>(&sfLedgerIndex,     STI_HASH256, 6, "LedgerIndex");
SF_U256 const sfWalletLocator   = make::one<SF_U256::type>(&sfWalletLocator,   STI_HASH256, 7, "WalletLocator");
SF_U256 const sfRootIndex       = make::one<SF_U256::type>(&sfRootIndex,       STI_HASH256, 8, "RootIndex", SField::sMD_Always);
SF_U256 const sfAccountTxnID    = make::one<SF_U256::type>(&sfAccountTxnID,    STI_HASH256, 9, "AccountTxnID");

// 256-bit (uncommon)
SF_U256 const sfBookDirectory = make::one<SF_U256::type>(&sfBookDirectory, STI_HASH256, 16, "BookDirectory");
SF_U256 const sfInvoiceID     = make::one<SF_U256::type>(&sfInvoiceID,     STI_HASH256, 17, "InvoiceID");
SF_U256 const sfNickname      = make::one<SF_U256::type>(&sfNickname,      STI_HASH256, 18, "Nickname");
SF_U256 const sfAmendment     = make::one<SF_U256::type>(&sfAmendment,     STI_HASH256, 19, "Amendment");
SF_U256 const sfTicketID      = make::one<SF_U256::type>(&sfTicketID,      STI_HASH256, 20, "TicketID");
SF_U256 const sfDigest        = make::one<SF_U256::type>(&sfDigest,        STI_HASH256, 21, "Digest");
SF_U256 const sfPayChannel    = make::one<SF_U256::type>(&sfPayChannel,    STI_HASH256, 22, "Channel");
SF_U256 const sfConsensusHash = make::one<SF_U256::type>(&sfConsensusHash, STI_HASH256, 23, "ConsensusHash");
SF_U256 const sfCheckID       = make::one<SF_U256::type>(&sfCheckID,       STI_HASH256, 24, "CheckID");

// currency amount (common)
SF_Amount const sfAmount      = make::one<SF_Amount::type>(&sfAmount,      STI_AMOUNT,  1, "Amount");
SF_Amount const sfBalance     = make::one<SF_Amount::type>(&sfBalance,     STI_AMOUNT,  2, "Balance");
SF_Amount const sfLimitAmount = make::one<SF_Amount::type>(&sfLimitAmount, STI_AMOUNT,  3, "LimitAmount");
SF_Amount const sfTakerPays   = make::one<SF_Amount::type>(&sfTakerPays,   STI_AMOUNT,  4, "TakerPays");
SF_Amount const sfTakerGets   = make::one<SF_Amount::type>(&sfTakerGets,   STI_AMOUNT,  5, "TakerGets");
SF_Amount const sfLowLimit    = make::one<SF_Amount::type>(&sfLowLimit,    STI_AMOUNT,  6, "LowLimit");
SF_Amount const sfHighLimit   = make::one<SF_Amount::type>(&sfHighLimit,   STI_AMOUNT,  7, "HighLimit");
SF_Amount const sfFee         = make::one<SF_Amount::type>(&sfFee,         STI_AMOUNT,  8, "Fee");
SF_Amount const sfSendMax     = make::one<SF_Amount::type>(&sfSendMax,     STI_AMOUNT,  9, "SendMax");
SF_Amount const sfDeliverMin  = make::one<SF_Amount::type>(&sfDeliverMin,  STI_AMOUNT, 10, "DeliverMin");

// currency amount (uncommon)
SF_Amount const sfMinimumOffer    = make::one<SF_Amount::type>(&sfMinimumOffer,    STI_AMOUNT, 16, "MinimumOffer");
SF_Amount const sfRippleEscrow    = make::one<SF_Amount::type>(&sfRippleEscrow,    STI_AMOUNT, 17, "RippleEscrow");
SF_Amount const sfDeliveredAmount = make::one<SF_Amount::type>(&sfDeliveredAmount, STI_AMOUNT, 18, "DeliveredAmount");

// variable length (common)
SF_Blob const sfPublicKey       = make::one<SF_Blob::type>(&sfPublicKey,     STI_VL,  1, "PublicKey");
SF_Blob const sfSigningPubKey   = make::one<SF_Blob::type>(&sfSigningPubKey, STI_VL,  3, "SigningPubKey");
SF_Blob const sfSignature       = make::one<SF_Blob::type>(&sfSignature,     STI_VL,  6, "Signature", SField::sMD_Default, SField::notSigning);
SF_Blob const sfMessageKey      = make::one<SF_Blob::type>(&sfMessageKey,    STI_VL,  2, "MessageKey");
SF_Blob const sfTxnSignature    = make::one<SF_Blob::type>(&sfTxnSignature,  STI_VL,  4, "TxnSignature", SField::sMD_Default, SField::notSigning);
SF_Blob const sfDomain          = make::one<SF_Blob::type>(&sfDomain,        STI_VL,  7, "Domain");
SF_Blob const sfFundCode        = make::one<SF_Blob::type>(&sfFundCode,      STI_VL,  8, "FundCode");
SF_Blob const sfRemoveCode      = make::one<SF_Blob::type>(&sfRemoveCode,    STI_VL,  9, "RemoveCode");
SF_Blob const sfExpireCode      = make::one<SF_Blob::type>(&sfExpireCode,    STI_VL, 10, "ExpireCode");
SF_Blob const sfCreateCode      = make::one<SF_Blob::type>(&sfCreateCode,    STI_VL, 11, "CreateCode");
SF_Blob const sfMemoType        = make::one<SF_Blob::type>(&sfMemoType,      STI_VL, 12, "MemoType");
SF_Blob const sfMemoData        = make::one<SF_Blob::type>(&sfMemoData,      STI_VL, 13, "MemoData");
SF_Blob const sfMemoFormat      = make::one<SF_Blob::type>(&sfMemoFormat,    STI_VL, 14, "MemoFormat");


// variable length (uncommon)
SF_Blob const sfFulfillment     = make::one<SF_Blob::type>(&sfFulfillment,     STI_VL, 16, "Fulfillment");
SF_Blob const sfCondition       = make::one<SF_Blob::type>(&sfCondition,       STI_VL, 17, "Condition");
SF_Blob const sfMasterSignature = make::one<SF_Blob::type>(&sfMasterSignature, STI_VL, 18, "MasterSignature", SField::sMD_Default, SField::notSigning);


// account
SF_Account const sfAccount     = make::one<SF_Account::type>(&sfAccount,     STI_ACCOUNT, 1, "Account");
SF_Account const sfOwner       = make::one<SF_Account::type>(&sfOwner,       STI_ACCOUNT, 2, "Owner");
SF_Account const sfDestination = make::one<SF_Account::type>(&sfDestination, STI_ACCOUNT, 3, "Destination");
SF_Account const sfIssuer      = make::one<SF_Account::type>(&sfIssuer,      STI_ACCOUNT, 4, "Issuer");
SF_Account const sfTarget      = make::one<SF_Account::type>(&sfTarget,      STI_ACCOUNT, 7, "Target");
SF_Account const sfRegularKey  = make::one<SF_Account::type>(&sfRegularKey,  STI_ACCOUNT, 8, "RegularKey");

// path set
SField const sfPaths = make::one(&sfPaths, STI_PATHSET, 1, "Paths");

// vector of 256-bit
SF_Vec256 const sfIndexes    = make::one<SF_Vec256::type>(&sfIndexes,    STI_VECTOR256, 1, "Indexes", SField::sMD_Never);
SF_Vec256 const sfHashes     = make::one<SF_Vec256::type>(&sfHashes,     STI_VECTOR256, 2, "Hashes");
SF_Vec256 const sfAmendments = make::one<SF_Vec256::type>(&sfAmendments, STI_VECTOR256, 3, "Amendments");

// inner object
// OBJECT/1 is reserved for end of object
SField const sfTransactionMetaData = make::one(&sfTransactionMetaData, STI_OBJECT,  2, "TransactionMetaData");
SField const sfCreatedNode         = make::one(&sfCreatedNode,         STI_OBJECT,  3, "CreatedNode");
SField const sfDeletedNode         = make::one(&sfDeletedNode,         STI_OBJECT,  4, "DeletedNode");
SField const sfModifiedNode        = make::one(&sfModifiedNode,        STI_OBJECT,  5, "ModifiedNode");
SField const sfPreviousFields      = make::one(&sfPreviousFields,      STI_OBJECT,  6, "PreviousFields");
SField const sfFinalFields         = make::one(&sfFinalFields,         STI_OBJECT,  7, "FinalFields");
SField const sfNewFields           = make::one(&sfNewFields,           STI_OBJECT,  8, "NewFields");
SField const sfTemplateEntry       = make::one(&sfTemplateEntry,       STI_OBJECT,  9, "TemplateEntry");
SField const sfMemo                = make::one(&sfMemo,                STI_OBJECT, 10, "Memo");
SField const sfSignerEntry         = make::one(&sfSignerEntry,         STI_OBJECT, 11, "SignerEntry");

// inner object (uncommon)
SField const sfSigner              = make::one(&sfSigner,              STI_OBJECT, 16, "Signer");
//                                                                                 17 has not been used yet...
SField const sfMajority            = make::one(&sfMajority,            STI_OBJECT, 18, "Majority");

// array of objects
// ARRAY/1 is reserved for end of array
// SField const sfSigningAccounts = make::one(&sfSigningAccounts, STI_ARRAY, 2, "SigningAccounts"); // Never been used.
SField const sfSigners         = make::one(&sfSigners,         STI_ARRAY, 3, "Signers", SField::sMD_Default, SField::notSigning);
SField const sfSignerEntries   = make::one(&sfSignerEntries,   STI_ARRAY, 4, "SignerEntries");
SField const sfTemplate        = make::one(&sfTemplate,        STI_ARRAY, 5, "Template");
SField const sfNecessary       = make::one(&sfNecessary,       STI_ARRAY, 6, "Necessary");
SField const sfSufficient      = make::one(&sfSufficient,      STI_ARRAY, 7, "Sufficient");
SField const sfAffectedNodes   = make::one(&sfAffectedNodes,   STI_ARRAY, 8, "AffectedNodes");
SField const sfMemos           = make::one(&sfMemos,           STI_ARRAY, 9, "Memos");

// array of objects (uncommon)
SField const sfMajorities      = make::one(&sfMajorities,      STI_ARRAY, 16, "Majorities");

SField::SField (SerializedTypeID tid, int fv, const char* fn,
                int meta, IsSigning signing)
    : fieldCode (field_code (tid, fv))
    , fieldType (tid)
    , fieldValue (fv)
    , fieldName (fn)
    , fieldMeta (meta)
    , fieldNum (++num)
    , signingField (signing)
    , jsonName (getName ())
{
}

SField::SField (int fc)
    : fieldCode (fc)
    , fieldType (STI_UNKNOWN)
    , fieldValue (0)
    , fieldMeta (sMD_Never)
    , fieldNum (++num)
    , signingField (IsSigning::yes)
    , jsonName (getName ())
{
}

// call with the map mutex to protect num.
// This is naturally done with no extra expense
// from getField(int code).
SField::SField (SerializedTypeID tid, int fv)
        : fieldCode (field_code (tid, fv))
        , fieldType (tid)
        , fieldValue (fv)
        , fieldMeta (sMD_Default)
        , fieldNum (++num)
        , signingField (IsSigning::yes)
{
    fieldName = std::to_string (tid) + '/' + std::to_string (fv);
    jsonName = getName ();
    assert ((fv != 1) || ((tid != STI_ARRAY) && (tid != STI_OBJECT)));
}

SField const&
SField::getField (int code)
{
    auto it = knownCodeToField.find (code);

    if (it != knownCodeToField.end ())
    {
        // 99+% of the time, it will be a valid, known field
        return * (it->second);
    }

    int type = code >> 16;
    int field = code & 0xffff;

    // Don't dynamically extend types that have no binary encoding.
    if ((field > 255) || (code < 0))
        return sfInvalid;

    switch (type)
    {
    // Types we are willing to dynamically extend
    // types (common)
    case STI_UINT16:
    case STI_UINT32:
    case STI_UINT64:
    case STI_HASH128:
    case STI_HASH256:
    case STI_AMOUNT:
    case STI_VL:
    case STI_ACCOUNT:
    case STI_OBJECT:
    case STI_ARRAY:
    // types (uncommon)
    case STI_UINT8:
    case STI_HASH160:
    case STI_PATHSET:
    case STI_VECTOR256:
        break;

    default:
        return sfInvalid;
    }

    {
        // Lookup in the run-time data base, and create if it does not
        // yet exist.
        StaticScopedLockType sl (SField_mutex);

        auto it = unknownCodeToField.find (code);

        if (it != unknownCodeToField.end ())
            return * (it->second);
        return *(unknownCodeToField[code] = std::unique_ptr<SField const>(
                       new SField(static_cast<SerializedTypeID>(type), field)));
    }
}

int SField::compare (SField const& f1, SField const& f2)
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

std::string SField::getName () const
{
    if (!fieldName.empty ())
        return fieldName;

    if (fieldValue == 0)
        return "";

    return std::to_string(static_cast<int> (fieldType)) + "/" +
            std::to_string(fieldValue);
}

SField const&
SField::getField (std::string const& fieldName)
{
    for (auto const & fieldPair : knownCodeToField)
    {
        if (fieldPair.second->fieldName == fieldName)
            return * (fieldPair.second);
    }
    {
        StaticScopedLockType sl (SField_mutex);

        for (auto const & fieldPair : unknownCodeToField)
        {
            if (fieldPair.second->fieldName == fieldName)
                return * (fieldPair.second);
        }
    }
    return sfInvalid;
}

} // ripple
