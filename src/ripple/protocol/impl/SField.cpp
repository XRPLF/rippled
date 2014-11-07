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
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace ripple {

// These must stay at the top of this file, and in this order
// Files-cope statics are preferred here because the SFields must be
// file-scope.  The following 3 objects must have scope prior to
// the file-scope SFields.
static std::mutex SField_mutex;
static std::map<int, SField::ptr> knownCodeToField;
static std::map<int, std::unique_ptr<SField const>> unknownCodeToField;

int SField::num = 0;

typedef std::lock_guard <std::mutex> StaticScopedLockType;

// Give this translation unit only, permission to construct SFields
struct SField::make
{
#ifndef _MSC_VER
    template <class ...Args>
    static SField one(SField const* p, Args&& ...args)
    {
        SField result(std::forward<Args>(args)...);
        knownCodeToField[result.fieldCode] = p;
        return result;
    }
#else  // remove this when VS gets variadic templates
    template <class A0>
    static SField one(SField const* p, A0&& arg0)
    {
        SField result(std::forward<A0>(arg0));
        knownCodeToField[result.fieldCode] = p;
        return result;
    }

    template <class A0, class A1, class A2>
    static SField one(SField const* p, A0&& arg0, A1&& arg1, A2&& arg2)
    {
        SField result(std::forward<A0>(arg0), std::forward<A1>(arg1),
                      std::forward<A2>(arg2));
        knownCodeToField[result.fieldCode] = p;
        return result;
    }

    template <class A0, class A1, class A2, class A3>
    static SField one(SField const* p, A0&& arg0, A1&& arg1, A2&& arg2,
                                       A3&& arg3)
    {
        SField result(std::forward<A0>(arg0), std::forward<A1>(arg1),
                      std::forward<A2>(arg2), std::forward<A3>(arg3));
        knownCodeToField[result.fieldCode] = p;
        return result;
    }

    template <class A0, class A1, class A2, class A3, class A4>
    static SField one(SField const* p, A0&& arg0, A1&& arg1, A2&& arg2,
                                       A3&& arg3, A4&& arg4)
    {
        SField result(std::forward<A0>(arg0), std::forward<A1>(arg1),
                      std::forward<A2>(arg2), std::forward<A3>(arg3),
                      std::forward<A4>(arg4));
        knownCodeToField[result.fieldCode] = p;
        return result;
    }
#endif
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
SField const sfCloseResolution   = make::one(&sfCloseResolution,   STI_UINT8, 1, "CloseResolution");
SField const sfTemplateEntryType = make::one(&sfTemplateEntryType, STI_UINT8, 2, "TemplateEntryType");
SField const sfTransactionResult = make::one(&sfTransactionResult, STI_UINT8, 3, "TransactionResult");

// 16-bit integers
SField const sfLedgerEntryType = make::one(&sfLedgerEntryType, STI_UINT16, 1, "LedgerEntryType", SField::sMD_Never);
SField const sfTransactionType = make::one(&sfTransactionType, STI_UINT16, 2, "TransactionType");

// 32-bit integers (common)
SField const sfFlags             = make::one(&sfFlags,             STI_UINT32,  2, "Flags");
SField const sfSourceTag         = make::one(&sfSourceTag,         STI_UINT32,  3, "SourceTag");
SField const sfSequence          = make::one(&sfSequence,          STI_UINT32,  4, "Sequence");
SField const sfPreviousTxnLgrSeq = make::one(&sfPreviousTxnLgrSeq, STI_UINT32,  5, "PreviousTxnLgrSeq", SField::sMD_DeleteFinal);
SField const sfLedgerSequence    = make::one(&sfLedgerSequence,    STI_UINT32,  6, "LedgerSequence");
SField const sfCloseTime         = make::one(&sfCloseTime,         STI_UINT32,  7, "CloseTime");
SField const sfParentCloseTime   = make::one(&sfParentCloseTime,   STI_UINT32,  8, "ParentCloseTime");
SField const sfSigningTime       = make::one(&sfSigningTime,       STI_UINT32,  9, "SigningTime");
SField const sfExpiration        = make::one(&sfExpiration,        STI_UINT32, 10, "Expiration");
SField const sfTransferRate      = make::one(&sfTransferRate,      STI_UINT32, 11, "TransferRate");
SField const sfWalletSize        = make::one(&sfWalletSize,        STI_UINT32, 12, "WalletSize");
SField const sfOwnerCount        = make::one(&sfOwnerCount,        STI_UINT32, 13, "OwnerCount");
SField const sfDestinationTag    = make::one(&sfDestinationTag,    STI_UINT32, 14, "DestinationTag");

// 32-bit integers (uncommon)
SField const sfHighQualityIn       = make::one(&sfHighQualityIn,       STI_UINT32, 16, "HighQualityIn");
SField const sfHighQualityOut      = make::one(&sfHighQualityOut,      STI_UINT32, 17, "HighQualityOut");
SField const sfLowQualityIn        = make::one(&sfLowQualityIn,        STI_UINT32, 18, "LowQualityIn");
SField const sfLowQualityOut       = make::one(&sfLowQualityOut,       STI_UINT32, 19, "LowQualityOut");
SField const sfQualityIn           = make::one(&sfQualityIn,           STI_UINT32, 20, "QualityIn");
SField const sfQualityOut          = make::one(&sfQualityOut,          STI_UINT32, 21, "QualityOut");
SField const sfStampEscrow         = make::one(&sfStampEscrow,         STI_UINT32, 22, "StampEscrow");
SField const sfBondAmount          = make::one(&sfBondAmount,          STI_UINT32, 23, "BondAmount");
SField const sfLoadFee             = make::one(&sfLoadFee,             STI_UINT32, 24, "LoadFee");
SField const sfOfferSequence       = make::one(&sfOfferSequence,       STI_UINT32, 25, "OfferSequence");
SField const sfFirstLedgerSequence = make::one(&sfFirstLedgerSequence, STI_UINT32, 26, "FirstLedgerSequence");  // Deprecated: do not use
SField const sfLastLedgerSequence  = make::one(&sfLastLedgerSequence,  STI_UINT32, 27, "LastLedgerSequence");
SField const sfTransactionIndex    = make::one(&sfTransactionIndex,    STI_UINT32, 28, "TransactionIndex");
SField const sfOperationLimit      = make::one(&sfOperationLimit,      STI_UINT32, 29, "OperationLimit");
SField const sfReferenceFeeUnits   = make::one(&sfReferenceFeeUnits,   STI_UINT32, 30, "ReferenceFeeUnits");
SField const sfReserveBase         = make::one(&sfReserveBase,         STI_UINT32, 31, "ReserveBase");
SField const sfReserveIncrement    = make::one(&sfReserveIncrement,    STI_UINT32, 32, "ReserveIncrement");
SField const sfSetFlag             = make::one(&sfSetFlag,             STI_UINT32, 33, "SetFlag");
SField const sfClearFlag           = make::one(&sfClearFlag,           STI_UINT32, 34, "ClearFlag");

// 64-bit integers
SField const sfIndexNext     = make::one(&sfIndexNext,     STI_UINT64, 1, "IndexNext");
SField const sfIndexPrevious = make::one(&sfIndexPrevious, STI_UINT64, 2, "IndexPrevious");
SField const sfBookNode      = make::one(&sfBookNode,      STI_UINT64, 3, "BookNode");
SField const sfOwnerNode     = make::one(&sfOwnerNode,     STI_UINT64, 4, "OwnerNode");
SField const sfBaseFee       = make::one(&sfBaseFee,       STI_UINT64, 5, "BaseFee");
SField const sfExchangeRate  = make::one(&sfExchangeRate,  STI_UINT64, 6, "ExchangeRate");
SField const sfLowNode       = make::one(&sfLowNode,       STI_UINT64, 7, "LowNode");
SField const sfHighNode      = make::one(&sfHighNode,      STI_UINT64, 8, "HighNode");

// 128-bit
SField const sfEmailHash = make::one(&sfEmailHash, STI_HASH128, 1, "EmailHash");

// 256-bit (common)
SField const sfLedgerHash      = make::one(&sfLedgerHash,      STI_HASH256, 1, "LedgerHash");
SField const sfParentHash      = make::one(&sfParentHash,      STI_HASH256, 2, "ParentHash");
SField const sfTransactionHash = make::one(&sfTransactionHash, STI_HASH256, 3, "TransactionHash");
SField const sfAccountHash     = make::one(&sfAccountHash,     STI_HASH256, 4, "AccountHash");
SField const sfPreviousTxnID   = make::one(&sfPreviousTxnID,   STI_HASH256, 5, "PreviousTxnID", SField::sMD_DeleteFinal);
SField const sfLedgerIndex     = make::one(&sfLedgerIndex,     STI_HASH256, 6, "LedgerIndex");
SField const sfWalletLocator   = make::one(&sfWalletLocator,   STI_HASH256, 7, "WalletLocator");
SField const sfRootIndex       = make::one(&sfRootIndex,       STI_HASH256, 8, "RootIndex", SField::sMD_Always);
SField const sfAccountTxnID    = make::one(&sfAccountTxnID,    STI_HASH256, 9, "AccountTxnID");

// 256-bit (uncommon)
SField const sfBookDirectory = make::one(&sfBookDirectory, STI_HASH256, 16, "BookDirectory");
SField const sfInvoiceID     = make::one(&sfInvoiceID,     STI_HASH256, 17, "InvoiceID");
SField const sfNickname      = make::one(&sfNickname,      STI_HASH256, 18, "Nickname");
SField const sfAmendment     = make::one(&sfAmendment,     STI_HASH256, 19, "Amendment");
SField const sfTicketID      = make::one(&sfTicketID,      STI_HASH256, 20, "TicketID");

// 160-bit (common)
SField const sfTakerPaysCurrency = make::one(&sfTakerPaysCurrency, STI_HASH160, 1, "TakerPaysCurrency");
SField const sfTakerPaysIssuer   = make::one(&sfTakerPaysIssuer,   STI_HASH160, 2, "TakerPaysIssuer");
SField const sfTakerGetsCurrency = make::one(&sfTakerGetsCurrency, STI_HASH160, 3, "TakerGetsCurrency");
SField const sfTakerGetsIssuer   = make::one(&sfTakerGetsIssuer,   STI_HASH160, 4, "TakerGetsIssuer");

// currency amount (common)
SField const sfAmount      = make::one(&sfAmount,      STI_AMOUNT, 1, "Amount");
SField const sfBalance     = make::one(&sfBalance,     STI_AMOUNT, 2, "Balance");
SField const sfLimitAmount = make::one(&sfLimitAmount, STI_AMOUNT, 3, "LimitAmount");
SField const sfTakerPays   = make::one(&sfTakerPays,   STI_AMOUNT, 4, "TakerPays");
SField const sfTakerGets   = make::one(&sfTakerGets,   STI_AMOUNT, 5, "TakerGets");
SField const sfLowLimit    = make::one(&sfLowLimit,    STI_AMOUNT, 6, "LowLimit");
SField const sfHighLimit   = make::one(&sfHighLimit,   STI_AMOUNT, 7, "HighLimit");
SField const sfFee         = make::one(&sfFee,         STI_AMOUNT, 8, "Fee");
SField const sfSendMax     = make::one(&sfSendMax,     STI_AMOUNT, 9, "SendMax");

// currency amount (uncommon)
SField const sfMinimumOffer    = make::one(&sfMinimumOffer,    STI_AMOUNT, 16, "MinimumOffer");
SField const sfRippleEscrow    = make::one(&sfRippleEscrow,    STI_AMOUNT, 17, "RippleEscrow");
SField const sfDeliveredAmount = make::one(&sfDeliveredAmount, STI_AMOUNT, 18, "DeliveredAmount");

// variable length
SField const sfPublicKey     = make::one(&sfPublicKey,     STI_VL,  1, "PublicKey");
SField const sfMessageKey    = make::one(&sfMessageKey,    STI_VL,  2, "MessageKey");
SField const sfSigningPubKey = make::one(&sfSigningPubKey, STI_VL,  3, "SigningPubKey");
SField const sfTxnSignature  = make::one(&sfTxnSignature,  STI_VL,  4, "TxnSignature", SField::sMD_Default, false);
SField const sfGenerator     = make::one(&sfGenerator,     STI_VL,  5, "Generator");
SField const sfSignature     = make::one(&sfSignature,     STI_VL,  6, "Signature", SField::sMD_Default, false);
SField const sfDomain        = make::one(&sfDomain,        STI_VL,  7, "Domain");
SField const sfFundCode      = make::one(&sfFundCode,      STI_VL,  8, "FundCode");
SField const sfRemoveCode    = make::one(&sfRemoveCode,    STI_VL,  9, "RemoveCode");
SField const sfExpireCode    = make::one(&sfExpireCode,    STI_VL, 10, "ExpireCode");
SField const sfCreateCode    = make::one(&sfCreateCode,    STI_VL, 11, "CreateCode");
SField const sfMemoType      = make::one(&sfMemoType,      STI_VL, 12, "MemoType");
SField const sfMemoData      = make::one(&sfMemoData,      STI_VL, 13, "MemoData");
SField const sfMemoFormat    = make::one(&sfMemoFormat,    STI_VL, 14, "MemoFormat");

// account
SField const sfAccount     = make::one(&sfAccount,     STI_ACCOUNT, 1, "Account");
SField const sfOwner       = make::one(&sfOwner,       STI_ACCOUNT, 2, "Owner");
SField const sfDestination = make::one(&sfDestination, STI_ACCOUNT, 3, "Destination");
SField const sfIssuer      = make::one(&sfIssuer,      STI_ACCOUNT, 4, "Issuer");
SField const sfTarget      = make::one(&sfTarget,      STI_ACCOUNT, 7, "Target");
SField const sfRegularKey  = make::one(&sfRegularKey,  STI_ACCOUNT, 8, "RegularKey");

// path set
SField const sfPaths = make::one(&sfPaths, STI_PATHSET, 1, "Paths");

// vector of 256-bit
SField const sfIndexes    = make::one(&sfIndexes,    STI_VECTOR256, 1, "Indexes", SField::sMD_Never);
SField const sfHashes     = make::one(&sfHashes,     STI_VECTOR256, 2, "Hashes");
SField const sfAmendments = make::one(&sfAmendments, STI_VECTOR256, 3, "Amendments");

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

// array of objects
// ARRAY/1 is reserved for end of array
SField const sfSigningAccounts = make::one(&sfSigningAccounts, STI_ARRAY, 2, "SigningAccounts");
SField const sfTxnSignatures   = make::one(&sfTxnSignatures,   STI_ARRAY, 3, "TxnSignatures", SField::sMD_Default, false);
SField const sfSignatures      = make::one(&sfSignatures,      STI_ARRAY, 4, "Signatures");
SField const sfTemplate        = make::one(&sfTemplate,        STI_ARRAY, 5, "Template");
SField const sfNecessary       = make::one(&sfNecessary,       STI_ARRAY, 6, "Necessary");
SField const sfSufficient      = make::one(&sfSufficient,      STI_ARRAY, 7, "Sufficient");
SField const sfAffectedNodes   = make::one(&sfAffectedNodes,   STI_ARRAY, 8, "AffectedNodes");
SField const sfMemos           = make::one(&sfMemos,           STI_ARRAY, 9, "Memos");

SField::SField (SerializedTypeID tid, int fv, const char* fn,
                int meta, bool signing)
    : fieldCode (field_code (tid, fv))
    , fieldType (tid)
    , fieldValue (fv)
    , fieldName (fn)
    , fieldMeta (meta)
    , fieldNum (++num)
    , signingField (signing)
    , rawJsonName (getName ())
    , jsonName (rawJsonName.c_str ())
{
}

SField::SField (int fc)
    : fieldCode (fc)
    , fieldType (STI_UNKNOWN)
    , fieldValue (0)
    , fieldMeta (sMD_Never)
    , fieldNum (++num)
    , signingField (true)
    , rawJsonName (getName ())
    , jsonName (rawJsonName.c_str ())
{
}

// call with the map mutex to protect num.
// This is naturally done with no extra expense
// from getField(int code).
SField::SField (SerializedTypeID tid, int fv)
        : fieldCode (field_code (tid, fv)), fieldType (tid), fieldValue (fv),
          fieldMeta (sMD_Default),
          fieldNum (++num),
          signingField (true),
          jsonName (nullptr)
{
    fieldName = std::to_string (tid) + '/' + std::to_string (fv);
    rawJsonName = getName ();
    jsonName = Json::StaticString (rawJsonName.c_str ());
    assert ((fv != 1) || ((tid != STI_ARRAY) && (tid != STI_OBJECT)));
}

SField::ref SField::getField (int code)
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

int SField::compare (SField::ref f1, SField::ref f2)
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

SField::ref SField::getField (std::string const& fieldName)
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
