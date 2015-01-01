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
    template <SerializedTypeID type, class ...Args>
    static TypedSField<type> typed(TypedSField<type> const* p, Args&& ...args)
    {
        TypedSField<type> result(type, std::forward<Args>(args)...);
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

/*

SFieldU8 const&
SFieldU16 const&
SFieldU32 const&
SFieldU64 const&
SFieldH128 const&
SFieldH160 const&
SFieldH256 const&
SFieldAccount const&
SFieldAccount const&
SFieldVL const&
SFieldAmount const&
SFieldArray const&
SFieldPathSet const&
SFieldV256 const&

SFieldObject

getFieldU8
getFieldU16
getFieldU32
getFieldU64
getFieldH128
getFieldH160
getFieldH256
getFieldAccount
getFieldAccount160
getFieldVL
getFieldAmount
getFieldArray
getFieldPathSet
getFieldV256

*/

// Construct all compile-time SFields, and register them in the knownCodeToField
// database:
/*

getFieldU8
getFieldU16
getFieldU32
getFieldU64
getFieldH128
getFieldH160
getFieldH256
getFieldAccount
getFieldAccount160
getFieldVL
getFieldAmount
getFieldPathSet
getFieldV256
getFieldArray

*/

SField const sfInvalid     = make::one(&sfInvalid, -1);
SField const sfGeneric     = make::one(&sfGeneric, 0);

SField const sfLedgerEntry = make::one(&sfLedgerEntry, STI_LEDGERENTRY, 257, "LedgerEntry");
SField const sfTransaction = make::one(&sfTransaction, STI_TRANSACTION, 257, "Transaction");
SField const sfValidation  = make::one(&sfValidation,  STI_VALIDATION,  257, "Validation");
SField const sfMetadata    = make::one(&sfMetadata,    STI_METADATA,    257, "Metadata");

SFieldH256 const sfHash        = make::typed<STI_HASH256>(&sfHash, 257, "hash");
SFieldH256 const sfIndex       = make::typed<STI_HASH256>(&sfIndex, 258, "index");

// 8-bit integers
SFieldU8 const sfCloseResolution   = make::typed<STI_UINT8>(&sfCloseResolution, 1, "CloseResolution");
SFieldU8 const sfTemplateEntryType = make::typed<STI_UINT8>(&sfTemplateEntryType, 2, "TemplateEntryType");
SFieldU8 const sfTransactionResult = make::typed<STI_UINT8>(&sfTransactionResult, 3, "TransactionResult");

// 16-bit integers
SFieldU16 const sfLedgerEntryType = make::typed<STI_UINT16>(&sfLedgerEntryType, 1, "LedgerEntryType", SField::sMD_Never);
SFieldU16 const sfTransactionType = make::typed<STI_UINT16>(&sfTransactionType, 2, "TransactionType");

// 32-bit integers (common)
SFieldU32 const sfFlags             = make::typed<STI_UINT32>(&sfFlags, 2, "Flags");
SFieldU32 const sfSourceTag         = make::typed<STI_UINT32>(&sfSourceTag, 3, "SourceTag");
SFieldU32 const sfSequence          = make::typed<STI_UINT32>(&sfSequence, 4, "Sequence");
SFieldU32 const sfPreviousTxnLgrSeq = make::typed<STI_UINT32>(&sfPreviousTxnLgrSeq, 5, "PreviousTxnLgrSeq", SField::sMD_DeleteFinal);
SFieldU32 const sfLedgerSequence    = make::typed<STI_UINT32>(&sfLedgerSequence, 6, "LedgerSequence");
SFieldU32 const sfCloseTime         = make::typed<STI_UINT32>(&sfCloseTime, 7, "CloseTime");
SFieldU32 const sfParentCloseTime   = make::typed<STI_UINT32>(&sfParentCloseTime, 8, "ParentCloseTime");
SFieldU32 const sfSigningTime       = make::typed<STI_UINT32>(&sfSigningTime, 9, "SigningTime");
SFieldU32 const sfExpiration        = make::typed<STI_UINT32>(&sfExpiration, 10, "Expiration");
SFieldU32 const sfTransferRate      = make::typed<STI_UINT32>(&sfTransferRate, 11, "TransferRate");
SFieldU32 const sfWalletSize        = make::typed<STI_UINT32>(&sfWalletSize, 12, "WalletSize");
SFieldU32 const sfOwnerCount        = make::typed<STI_UINT32>(&sfOwnerCount, 13, "OwnerCount");
SFieldU32 const sfDestinationTag    = make::typed<STI_UINT32>(&sfDestinationTag, 14, "DestinationTag");

// 32-bit integers (uncommon)
SFieldU32 const sfHighQualityIn       = make::typed<STI_UINT32>(&sfHighQualityIn, 16, "HighQualityIn");
SFieldU32 const sfHighQualityOut      = make::typed<STI_UINT32>(&sfHighQualityOut, 17, "HighQualityOut");
SFieldU32 const sfLowQualityIn        = make::typed<STI_UINT32>(&sfLowQualityIn, 18, "LowQualityIn");
SFieldU32 const sfLowQualityOut       = make::typed<STI_UINT32>(&sfLowQualityOut, 19, "LowQualityOut");
SFieldU32 const sfQualityIn           = make::typed<STI_UINT32>(&sfQualityIn, 20, "QualityIn");
SFieldU32 const sfQualityOut          = make::typed<STI_UINT32>(&sfQualityOut, 21, "QualityOut");
SFieldU32 const sfStampEscrow         = make::typed<STI_UINT32>(&sfStampEscrow, 22, "StampEscrow");
SFieldU32 const sfBondAmount          = make::typed<STI_UINT32>(&sfBondAmount, 23, "BondAmount");
SFieldU32 const sfLoadFee             = make::typed<STI_UINT32>(&sfLoadFee, 24, "LoadFee");
SFieldU32 const sfOfferSequence       = make::typed<STI_UINT32>(&sfOfferSequence, 25, "OfferSequence");
SFieldU32 const sfFirstLedgerSequence = make::typed<STI_UINT32>(&sfFirstLedgerSequence, 26, "FirstLedgerSequence");  // Deprecated: do not use
SFieldU32 const sfLastLedgerSequence  = make::typed<STI_UINT32>(&sfLastLedgerSequence, 27, "LastLedgerSequence");
SFieldU32 const sfTransactionIndex    = make::typed<STI_UINT32>(&sfTransactionIndex, 28, "TransactionIndex");
SFieldU32 const sfOperationLimit      = make::typed<STI_UINT32>(&sfOperationLimit, 29, "OperationLimit");
SFieldU32 const sfReferenceFeeUnits   = make::typed<STI_UINT32>(&sfReferenceFeeUnits, 30, "ReferenceFeeUnits");
SFieldU32 const sfReserveBase         = make::typed<STI_UINT32>(&sfReserveBase, 31, "ReserveBase");
SFieldU32 const sfReserveIncrement    = make::typed<STI_UINT32>(&sfReserveIncrement, 32, "ReserveIncrement");
SFieldU32 const sfSetFlag             = make::typed<STI_UINT32>(&sfSetFlag, 33, "SetFlag");
SFieldU32 const sfClearFlag           = make::typed<STI_UINT32>(&sfClearFlag, 34, "ClearFlag");

// 64-bit integers
SFieldU64 const sfIndexNext     = make::typed<STI_UINT64>(&sfIndexNext, 1, "IndexNext");
SFieldU64 const sfIndexPrevious = make::typed<STI_UINT64>(&sfIndexPrevious, 2, "IndexPrevious");
SFieldU64 const sfBookNode      = make::typed<STI_UINT64>(&sfBookNode, 3, "BookNode");
SFieldU64 const sfOwnerNode     = make::typed<STI_UINT64>(&sfOwnerNode, 4, "OwnerNode");
SFieldU64 const sfBaseFee       = make::typed<STI_UINT64>(&sfBaseFee, 5, "BaseFee");
SFieldU64 const sfExchangeRate  = make::typed<STI_UINT64>(&sfExchangeRate, 6, "ExchangeRate");
SFieldU64 const sfLowNode       = make::typed<STI_UINT64>(&sfLowNode, 7, "LowNode");
SFieldU64 const sfHighNode      = make::typed<STI_UINT64>(&sfHighNode, 8, "HighNode");

// 128-bit
SFieldH128 const sfEmailHash = make::typed<STI_HASH128>(&sfEmailHash, 1, "EmailHash");

// 256-bit (common)
SFieldH256 const sfLedgerHash      = make::typed<STI_HASH256>(&sfLedgerHash, 1, "LedgerHash");
SFieldH256 const sfParentHash      = make::typed<STI_HASH256>(&sfParentHash, 2, "ParentHash");
SFieldH256 const sfTransactionHash = make::typed<STI_HASH256>(&sfTransactionHash, 3, "TransactionHash");
SFieldH256 const sfAccountHash     = make::typed<STI_HASH256>(&sfAccountHash, 4, "AccountHash");
SFieldH256 const sfPreviousTxnID   = make::typed<STI_HASH256>(&sfPreviousTxnID, 5, "PreviousTxnID", SField::sMD_DeleteFinal);
SFieldH256 const sfLedgerIndex     = make::typed<STI_HASH256>(&sfLedgerIndex, 6, "LedgerIndex");
SFieldH256 const sfWalletLocator   = make::typed<STI_HASH256>(&sfWalletLocator, 7, "WalletLocator");
SFieldH256 const sfRootIndex       = make::typed<STI_HASH256>(&sfRootIndex, 8, "RootIndex", SField::sMD_Always);
SFieldH256 const sfAccountTxnID    = make::typed<STI_HASH256>(&sfAccountTxnID, 9, "AccountTxnID");

// 256-bit (uncommon)
SFieldH256 const sfBookDirectory = make::typed<STI_HASH256>(&sfBookDirectory, 16, "BookDirectory");
SFieldH256 const sfInvoiceID     = make::typed<STI_HASH256>(&sfInvoiceID, 17, "InvoiceID");
SFieldH256 const sfNickname      = make::typed<STI_HASH256>(&sfNickname, 18, "Nickname");
SFieldH256 const sfAmendment     = make::typed<STI_HASH256>(&sfAmendment, 19, "Amendment");
SFieldH256 const sfTicketID      = make::typed<STI_HASH256>(&sfTicketID, 20, "TicketID");

// 160-bit (common)
SFieldH160 const sfTakerPaysCurrency = make::typed<STI_HASH160>(&sfTakerPaysCurrency, 1, "TakerPaysCurrency");
SFieldH160 const sfTakerPaysIssuer   = make::typed<STI_HASH160>(&sfTakerPaysIssuer, 2, "TakerPaysIssuer");
SFieldH160 const sfTakerGetsCurrency = make::typed<STI_HASH160>(&sfTakerGetsCurrency, 3, "TakerGetsCurrency");
SFieldH160 const sfTakerGetsIssuer   = make::typed<STI_HASH160>(&sfTakerGetsIssuer, 4, "TakerGetsIssuer");

// currency amount (common)
SFieldAmount const sfAmount      = make::typed<STI_AMOUNT>(&sfAmount, 1, "Amount");
SFieldAmount const sfBalance     = make::typed<STI_AMOUNT>(&sfBalance, 2, "Balance");
SFieldAmount const sfLimitAmount = make::typed<STI_AMOUNT>(&sfLimitAmount, 3, "LimitAmount");
SFieldAmount const sfTakerPays   = make::typed<STI_AMOUNT>(&sfTakerPays, 4, "TakerPays");
SFieldAmount const sfTakerGets   = make::typed<STI_AMOUNT>(&sfTakerGets, 5, "TakerGets");
SFieldAmount const sfLowLimit    = make::typed<STI_AMOUNT>(&sfLowLimit, 6, "LowLimit");
SFieldAmount const sfHighLimit   = make::typed<STI_AMOUNT>(&sfHighLimit, 7, "HighLimit");
SFieldAmount const sfFee         = make::typed<STI_AMOUNT>(&sfFee, 8, "Fee");
SFieldAmount const sfSendMax     = make::typed<STI_AMOUNT>(&sfSendMax, 9, "SendMax");

// currency amount (uncommon)
SFieldAmount const sfMinimumOffer    = make::typed<STI_AMOUNT>(&sfMinimumOffer, 16, "MinimumOffer");
SFieldAmount const sfRippleEscrow    = make::typed<STI_AMOUNT>(&sfRippleEscrow, 17, "RippleEscrow");
SFieldAmount const sfDeliveredAmount = make::typed<STI_AMOUNT>(&sfDeliveredAmount, 18, "DeliveredAmount");

// variable length
SFieldVL const sfPublicKey     = make::typed<STI_VL>(&sfPublicKey, 1, "PublicKey");
SFieldVL const sfMessageKey    = make::typed<STI_VL>(&sfMessageKey, 2, "MessageKey");
SFieldVL const sfSigningPubKey = make::typed<STI_VL>(&sfSigningPubKey, 3, "SigningPubKey");
SFieldVL const sfTxnSignature  = make::typed<STI_VL>(&sfTxnSignature, 4, "TxnSignature", SField::sMD_Default, false);
SFieldVL const sfGenerator     = make::typed<STI_VL>(&sfGenerator, 5, "Generator");
SFieldVL const sfSignature     = make::typed<STI_VL>(&sfSignature, 6, "Signature", SField::sMD_Default, false);
SFieldVL const sfDomain        = make::typed<STI_VL>(&sfDomain, 7, "Domain");
SFieldVL const sfFundCode      = make::typed<STI_VL>(&sfFundCode, 8, "FundCode");
SFieldVL const sfRemoveCode    = make::typed<STI_VL>(&sfRemoveCode, 9, "RemoveCode");
SFieldVL const sfExpireCode    = make::typed<STI_VL>(&sfExpireCode, 10, "ExpireCode");
SFieldVL const sfCreateCode    = make::typed<STI_VL>(&sfCreateCode, 11, "CreateCode");
SFieldVL const sfMemoType      = make::typed<STI_VL>(&sfMemoType, 12, "MemoType");
SFieldVL const sfMemoData      = make::typed<STI_VL>(&sfMemoData, 13, "MemoData");
SFieldVL const sfMemoFormat    = make::typed<STI_VL>(&sfMemoFormat, 14, "MemoFormat");

// account
SFieldAccount const sfAccount     = make::typed<STI_ACCOUNT>(&sfAccount, 1, "Account");
SFieldAccount const sfOwner       = make::typed<STI_ACCOUNT>(&sfOwner, 2, "Owner");
SFieldAccount const sfDestination = make::typed<STI_ACCOUNT>(&sfDestination, 3, "Destination");
SFieldAccount const sfIssuer      = make::typed<STI_ACCOUNT>(&sfIssuer, 4, "Issuer");
SFieldAccount const sfTarget      = make::typed<STI_ACCOUNT>(&sfTarget, 7, "Target");
SFieldAccount const sfRegularKey  = make::typed<STI_ACCOUNT>(&sfRegularKey, 8, "RegularKey");

// path set
SFieldPathSet const sfPaths = make::typed<STI_PATHSET>(&sfPaths, 1, "Paths");

// vector of 256-bit
SFieldV256 const sfIndexes    = make::typed<STI_VECTOR256>(&sfIndexes, 1, "Indexes", SField::sMD_Never);
SFieldV256 const sfHashes     = make::typed<STI_VECTOR256>(&sfHashes, 2, "Hashes");
SFieldV256 const sfAmendments = make::typed<STI_VECTOR256>(&sfAmendments, 3, "Amendments");

// inner object
// OBJECT/1 is reserved for end of object
SFieldObject const sfTransactionMetaData = make::typed<STI_OBJECT>(&sfTransactionMetaData, 2, "TransactionMetaData");
SFieldObject const sfCreatedNode         = make::typed<STI_OBJECT>(&sfCreatedNode, 3, "CreatedNode");
SFieldObject const sfDeletedNode         = make::typed<STI_OBJECT>(&sfDeletedNode, 4, "DeletedNode");
SFieldObject const sfModifiedNode        = make::typed<STI_OBJECT>(&sfModifiedNode, 5, "ModifiedNode");
SFieldObject const sfPreviousFields      = make::typed<STI_OBJECT>(&sfPreviousFields, 6, "PreviousFields");
SFieldObject const sfFinalFields         = make::typed<STI_OBJECT>(&sfFinalFields, 7, "FinalFields");
SFieldObject const sfNewFields           = make::typed<STI_OBJECT>(&sfNewFields, 8, "NewFields");
SFieldObject const sfTemplateEntry       = make::typed<STI_OBJECT>(&sfTemplateEntry, 9, "TemplateEntry");
SFieldObject const sfMemo                = make::typed<STI_OBJECT>(&sfMemo, 10, "Memo");

// array of objects
// ARRAY/1 is reserved for end of array
SFieldArray const sfSigningAccounts = make::typed<STI_ARRAY>(&sfSigningAccounts, 2, "SigningAccounts");
SFieldArray const sfTxnSignatures   = make::typed<STI_ARRAY>(&sfTxnSignatures, 3, "TxnSignatures", SField::sMD_Default, false);
SFieldArray const sfSignatures      = make::typed<STI_ARRAY>(&sfSignatures, 4, "Signatures");
SFieldArray const sfTemplate        = make::typed<STI_ARRAY>(&sfTemplate, 5, "Template");
SFieldArray const sfNecessary       = make::typed<STI_ARRAY>(&sfNecessary, 6, "Necessary");
SFieldArray const sfSufficient      = make::typed<STI_ARRAY>(&sfSufficient, 7, "Sufficient");
SFieldArray const sfAffectedNodes   = make::typed<STI_ARRAY>(&sfAffectedNodes, 8, "AffectedNodes");
SFieldArray const sfMemos           = make::typed<STI_ARRAY>(&sfMemos, 9, "Memos");

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
