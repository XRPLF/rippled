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

#ifndef RIPPLE_PROTOCOL_SFIELD_H_INCLUDED
#define RIPPLE_PROTOCOL_SFIELD_H_INCLUDED

#include <ripple/basics/BasicTypes.h>
#include <ripple/json/json_value.h>

namespace ripple {

enum SerializedTypeID
{
    // special types
    STI_UNKNOWN     = -2,
    STI_DONE        = -1,
    STI_NOTPRESENT  = 0,

    // // types (common)
    STI_UINT16 = 1,
    STI_UINT32 = 2,
    STI_UINT64 = 3,
    STI_HASH128 = 4,
    STI_HASH256 = 5,
    STI_AMOUNT = 6,
    STI_VL = 7,
    STI_ACCOUNT = 8,
    // 9-13 are reserved
    STI_OBJECT = 14,
    STI_ARRAY = 15,

    // types (uncommon)
    STI_UINT8 = 16,
    STI_HASH160 = 17,
    STI_PATHSET = 18,
    STI_VECTOR256 = 19,

    // high level types
    // cannot be serialized inside other types
    STI_TRANSACTION = 10001,
    STI_LEDGERENTRY = 10002,
    STI_VALIDATION  = 10003,
    STI_METADATA    = 10004,
};

// constexpr
inline
int
field_code(SerializedTypeID id, int index)
{
    return (static_cast<int>(id) << 16) | index;
}

// constexpr
inline
int
field_code(int id, int index)
{
    return (id << 16) | index;
}

/** Identifies fields.

    Fields are necessary to tag data in signed transactions so that
    the binary format of the transaction can be canonicalized.

    There are two categories of these fields:

    1.  Those that are created at compile time.
    2.  Those that are created at run time.

    Both are always const.  Category 1 can only be created in FieldNames.cpp.
    This is enforced at compile time.  Category 2 can only be created by
    calling getField with an as yet unused fieldType and fieldValue (or the
    equivalent fieldCode).

    Each SField, once constructed, lives until program termination, and there
    is only one instance per fieldType/fieldValue pair which serves the entire
    application.
*/
class SField
{
public:
    typedef const SField&   ref;
    typedef SField const*   ptr;

    enum
    {
        sMD_Never          = 0x00,
        sMD_ChangeOrig     = 0x01, // original value when it changes
        sMD_ChangeNew      = 0x02, // new value when it changes
        sMD_DeleteFinal    = 0x04, // final value when it is deleted
        sMD_Create         = 0x08, // value when it's created
        sMD_Always         = 0x10, // value when node containing it is affected at all
        sMD_Default        = sMD_ChangeOrig | sMD_ChangeNew | sMD_DeleteFinal | sMD_Create
    };

    const int               fieldCode;      // (type<<16)|index
    const SerializedTypeID  fieldType;      // STI_*
    const int               fieldValue;     // Code number for protocol
    std::string             fieldName;
    int                     fieldMeta;
    int                     fieldNum;
    bool                    signingField;
    std::string             rawJsonName;
    Json::StaticString      jsonName;

    SField(SField const&) = delete;
    SField& operator=(SField const&) = delete;
#ifndef _MSC_VER
    SField(SField&&) = default;
#else  // remove this when VS gets defaulted move members
    SField(SField&& sf)
        : fieldCode (std::move(sf.fieldCode))
        , fieldType (std::move(sf.fieldType))
        , fieldValue (std::move(sf.fieldValue))
        , fieldName (std::move(sf.fieldName))
        , fieldMeta (std::move(sf.fieldMeta))
        , fieldNum (std::move(sf.fieldNum))
        , signingField (std::move(sf.signingField))
        , rawJsonName (std::move(sf.rawJsonName))
        , jsonName (rawJsonName.c_str ())
    {}
#endif

// private:
    // These constructors can only be called from FieldNames.cpp
    SField (SerializedTypeID tid, int fv, const char* fn,
            int meta = sMD_Default, bool signing = true);
    explicit SField (int fc);
    SField (SerializedTypeID id, int val);

public:
    // getField will dynamically construct a new SField if necessary
    static SField::ref getField (int fieldCode);
    static SField::ref getField (std::string const& fieldName);
    static SField::ref getField (int type, int value)
    {
        return getField (field_code (type, value));
    }
    static SField::ref getField (SerializedTypeID type, int value)
    {
        return getField (field_code (type, value));
    }

    std::string getName () const;
    bool hasName () const
    {
        return !fieldName.empty ();
    }

    Json::StaticString const& getJsonName () const
    {
        return jsonName;
    }

    bool isGeneric () const
    {
        return fieldCode == 0;
    }
    bool isInvalid () const
    {
        return fieldCode == -1;
    }
    bool isUseful () const
    {
        return fieldCode > 0;
    }
    bool isKnown () const
    {
        return fieldType != STI_UNKNOWN;
    }
    bool isBinary () const
    {
        return fieldValue < 256;
    }

    // A discardable field is one that cannot be serialized, and
    // should be discarded during serialization,like 'hash'.
    // You cannot serialize an object's hash inside that object,
    // but you can have it in the JSON representation.
    bool isDiscardable () const
    {
        return fieldValue > 256;
    }

    int getCode () const
    {
        return fieldCode;
    }
    int getNum () const
    {
        return fieldNum;
    }
    static int getNumFields ()
    {
        return num;
    }

    bool isSigningField () const
    {
        return signingField;
    }
    void notSigningField ()
    {
        signingField = false;
    }
    bool shouldMeta (int c) const
    {
        return (fieldMeta & c) != 0;
    }
    void setMeta (int c)
    {
        fieldMeta = c;
    }

    bool shouldInclude (bool withSigningField) const
    {
        return (fieldValue < 256) && (withSigningField || signingField);
    }

    bool operator== (const SField& f) const
    {
        return fieldCode == f.fieldCode;
    }

    bool operator!= (const SField& f) const
    {
        return fieldCode != f.fieldCode;
    }

    static int compare (SField::ref f1, SField::ref f2);

    struct make;  // public, but still an implementation detail

private:
    static int num;
};

template<SerializedTypeID type>
class TypedSField : public SField
{
    public:
        TypedSField (SerializedTypeID tid, int fv, const char* fn,
                int meta = sMD_Default, bool signing = true) :
                SField (tid, fv, fn, meta, signing) {};
        explicit TypedSField (int fc) : SField(fc) {};
        TypedSField (SerializedTypeID id, int val) : SField(id, val) {};
};

typedef TypedSField<STI_UNKNOWN> UnknownSField;
typedef TypedSField<STI_DONE> DoneSField;
typedef TypedSField<STI_NOTPRESENT> NotPresentSField;

typedef TypedSField<STI_UINT8> SFieldU8;
typedef TypedSField<STI_UINT16> SFieldU16;
typedef TypedSField<STI_UINT32> SFieldU32;
typedef TypedSField<STI_UINT64> SFieldU64;
typedef TypedSField<STI_HASH128> SFieldH128;
typedef TypedSField<STI_HASH256> SFieldH256;
typedef TypedSField<STI_VECTOR256> SFieldV256;
typedef TypedSField<STI_ACCOUNT> SFieldAccount;
typedef TypedSField<STI_VL> SFieldVL;
typedef TypedSField<STI_AMOUNT> SFieldAmount;
typedef TypedSField<STI_PATHSET> SFieldPathSet;
typedef TypedSField<STI_ARRAY> SFieldArray;
typedef TypedSField<STI_HASH160> SFieldH160;

typedef TypedSField<STI_OBJECT> SFieldObject;

typedef TypedSField<STI_TRANSACTION> TransactionSField;
typedef TypedSField<STI_LEDGERENTRY> LedgerEntrySField;
typedef TypedSField<STI_VALIDATION> ValidationSField;
typedef TypedSField<STI_METADATA> MetadataSField;

extern SField const sfInvalid;
extern SField const sfGeneric;
extern SField const sfLedgerEntry;
extern SField const sfTransaction;
extern SField const sfValidation;
extern SField const sfMetadata;

// 8-bit integers
extern SFieldU8 const sfCloseResolution;
extern SFieldU8 const sfTemplateEntryType;
extern SFieldU8 const sfTransactionResult;

// 16-bit integers
extern SFieldU16 const sfLedgerEntryType;
extern SFieldU16 const sfTransactionType;

// 32-bit integers (common)
extern SFieldU32 const sfFlags;
extern SFieldU32 const sfSourceTag;
extern SFieldU32 const sfSequence;
extern SFieldU32 const sfPreviousTxnLgrSeq;
extern SFieldU32 const sfLedgerSequence;
extern SFieldU32 const sfCloseTime;
extern SFieldU32 const sfParentCloseTime;
extern SFieldU32 const sfSigningTime;
extern SFieldU32 const sfExpiration;
extern SFieldU32 const sfTransferRate;
extern SFieldU32 const sfWalletSize;
extern SFieldU32 const sfOwnerCount;
extern SFieldU32 const sfDestinationTag;

// 32-bit integers (uncommon)
extern SFieldU32 const sfHighQualityIn;
extern SFieldU32 const sfHighQualityOut;
extern SFieldU32 const sfLowQualityIn;
extern SFieldU32 const sfLowQualityOut;
extern SFieldU32 const sfQualityIn;
extern SFieldU32 const sfQualityOut;
extern SFieldU32 const sfStampEscrow;
extern SFieldU32 const sfBondAmount;
extern SFieldU32 const sfLoadFee;
extern SFieldU32 const sfOfferSequence;
extern SFieldU32 const sfFirstLedgerSequence;  // Deprecated: do not use
extern SFieldU32 const sfLastLedgerSequence;
extern SFieldU32 const sfTransactionIndex;
extern SFieldU32 const sfOperationLimit;
extern SFieldU32 const sfReferenceFeeUnits;
extern SFieldU32 const sfReserveBase;
extern SFieldU32 const sfReserveIncrement;
extern SFieldU32 const sfSetFlag;
extern SFieldU32 const sfClearFlag;

// 64-bit integers
extern SFieldU64 const sfIndexNext;
extern SFieldU64 const sfIndexPrevious;
extern SFieldU64 const sfBookNode;
extern SFieldU64 const sfOwnerNode;
extern SFieldU64 const sfBaseFee;
extern SFieldU64 const sfExchangeRate;
extern SFieldU64 const sfLowNode;
extern SFieldU64 const sfHighNode;

// 128-bit
extern SFieldH128 const sfEmailHash;

// 256-bit (common)
extern SFieldH256 const sfLedgerHash;
extern SFieldH256 const sfParentHash;
extern SFieldH256 const sfTransactionHash;
extern SFieldH256 const sfAccountHash;
extern SFieldH256 const sfPreviousTxnID;
extern SFieldH256 const sfLedgerIndex;
extern SFieldH256 const sfWalletLocator;
extern SFieldH256 const sfRootIndex;
extern SFieldH256 const sfAccountTxnID;

// 256-bit (uncommon)
extern SFieldH256 const sfBookDirectory;
extern SFieldH256 const sfInvoiceID;
extern SFieldH256 const sfNickname;
extern SFieldH256 const sfAmendment;
extern SFieldH256 const sfTicketID;

// 160-bit (common)
extern SFieldH160 const sfTakerPaysCurrency;
extern SFieldH160 const sfTakerPaysIssuer;
extern SFieldH160 const sfTakerGetsCurrency;
extern SFieldH160 const sfTakerGetsIssuer;

// currency amount (common)
extern SFieldAmount const sfAmount;
extern SFieldAmount const sfBalance;
extern SFieldAmount const sfLimitAmount;
extern SFieldAmount const sfTakerPays;
extern SFieldAmount const sfTakerGets;
extern SFieldAmount const sfLowLimit;
extern SFieldAmount const sfHighLimit;
extern SFieldAmount const sfFee;
extern SFieldAmount const sfSendMax;

// currency amount (uncommon)
extern SFieldAmount const sfMinimumOffer;
extern SFieldAmount const sfRippleEscrow;
extern SFieldAmount const sfDeliveredAmount;

// variable length
extern SFieldVL const sfPublicKey;
extern SFieldVL const sfMessageKey;
extern SFieldVL const sfSigningPubKey;
extern SFieldVL const sfTxnSignature;
extern SFieldVL const sfGenerator;
extern SFieldVL const sfSignature;
extern SFieldVL const sfDomain;
extern SFieldVL const sfFundCode;
extern SFieldVL const sfRemoveCode;
extern SFieldVL const sfExpireCode;
extern SFieldVL const sfCreateCode;
extern SFieldVL const sfMemoType;
extern SFieldVL const sfMemoData;
extern SFieldVL const sfMemoFormat;

// account
extern SFieldAccount const sfAccount;
extern SFieldAccount const sfOwner;
extern SFieldAccount const sfDestination;
extern SFieldAccount const sfIssuer;
extern SFieldAccount const sfTarget;
extern SFieldAccount const sfRegularKey;

// path set
extern SFieldPathSet const sfPaths;

// vector of 256-bit
extern SFieldV256 const sfIndexes;
extern SFieldV256 const sfHashes;
extern SFieldV256 const sfAmendments;

// inner object
// OBJECT/1 is reserved for end of object
extern SFieldObject const sfTransactionMetaData;
extern SFieldObject const sfCreatedNode;
extern SFieldObject const sfDeletedNode;
extern SFieldObject const sfModifiedNode;
extern SFieldObject const sfPreviousFields;
extern SFieldObject const sfFinalFields;
extern SFieldObject const sfNewFields;
extern SFieldObject const sfTemplateEntry;
extern SFieldObject const sfMemo;

// array of objects
// ARRAY/1 is reserved for end of array
extern SFieldArray const sfSigningAccounts;
extern SFieldArray const sfTxnSignatures;
extern SFieldArray const sfSignatures;
extern SFieldArray const sfTemplate;
extern SFieldArray const sfNecessary;
extern SFieldArray const sfSufficient;
extern SFieldArray const sfAffectedNodes;
extern SFieldArray const sfMemos;

} // ripple

#endif
