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

#ifndef RIPPLE_PROTOCOL_FIELDNAMES_H_INCLUDED
#define RIPPLE_PROTOCOL_FIELDNAMES_H_INCLUDED

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

    enum class IsSigning : unsigned char
    {
        no,
        yes
    };
    static IsSigning const notSigning = IsSigning::no;

    int const               fieldCode;      // (type<<16)|index
    SerializedTypeID const  fieldType;      // STI_*
    int const               fieldValue;     // Code number for protocol
    std::string             fieldName;
    int                     fieldMeta;
    int                     fieldNum;
    IsSigning               signingField;
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

private:
    // These constructors can only be called from FieldNames.cpp
    SField (SerializedTypeID tid, int fv, const char* fn,
            int meta = sMD_Default, IsSigning signing = IsSigning::yes);
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
        return signingField == IsSigning::yes;
    }
    void notSigningField ()
    {
        signingField = IsSigning::no;
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
        return (fieldValue < 256) &&
            (withSigningField || (signingField == IsSigning::yes));
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

extern SField const sfInvalid;
extern SField const sfGeneric;
extern SField const sfLedgerEntry;
extern SField const sfTransaction;
extern SField const sfValidation;
extern SField const sfMetadata;

// 8-bit integers
extern SField const sfCloseResolution;
extern SField const sfTemplateEntryType;
extern SField const sfTransactionResult;

// 16-bit integers
extern SField const sfLedgerEntryType;
extern SField const sfTransactionType;
extern SField const sfSignerWeight;

// 32-bit integers (common)
extern SField const sfFlags;
extern SField const sfSourceTag;
extern SField const sfSequence;
extern SField const sfPreviousTxnLgrSeq;
extern SField const sfLedgerSequence;
extern SField const sfCloseTime;
extern SField const sfParentCloseTime;
extern SField const sfSigningTime;
extern SField const sfExpiration;
extern SField const sfTransferRate;
extern SField const sfWalletSize;
extern SField const sfOwnerCount;
extern SField const sfDestinationTag;

// 32-bit integers (uncommon)
extern SField const sfHighQualityIn;
extern SField const sfHighQualityOut;
extern SField const sfLowQualityIn;
extern SField const sfLowQualityOut;
extern SField const sfQualityIn;
extern SField const sfQualityOut;
extern SField const sfStampEscrow;
extern SField const sfBondAmount;
extern SField const sfLoadFee;
extern SField const sfOfferSequence;
extern SField const sfFirstLedgerSequence;  // Deprecated: do not use
extern SField const sfLastLedgerSequence;
extern SField const sfTransactionIndex;
extern SField const sfOperationLimit;
extern SField const sfReferenceFeeUnits;
extern SField const sfReserveBase;
extern SField const sfReserveIncrement;
extern SField const sfSetFlag;
extern SField const sfClearFlag;
extern SField const sfSignerQuorum;

// 64-bit integers
extern SField const sfIndexNext;
extern SField const sfIndexPrevious;
extern SField const sfBookNode;
extern SField const sfOwnerNode;
extern SField const sfBaseFee;
extern SField const sfExchangeRate;
extern SField const sfLowNode;
extern SField const sfHighNode;

// 128-bit
extern SField const sfEmailHash;

// 256-bit (common)
extern SField const sfLedgerHash;
extern SField const sfParentHash;
extern SField const sfTransactionHash;
extern SField const sfAccountHash;
extern SField const sfPreviousTxnID;
extern SField const sfLedgerIndex;
extern SField const sfWalletLocator;
extern SField const sfRootIndex;
extern SField const sfAccountTxnID;

// 256-bit (uncommon)
extern SField const sfBookDirectory;
extern SField const sfInvoiceID;
extern SField const sfNickname;
extern SField const sfAmendment;
extern SField const sfTicketID;

// 160-bit (common)
extern SField const sfTakerPaysCurrency;
extern SField const sfTakerPaysIssuer;
extern SField const sfTakerGetsCurrency;
extern SField const sfTakerGetsIssuer;

// currency amount (common)
extern SField const sfAmount;
extern SField const sfBalance;
extern SField const sfLimitAmount;
extern SField const sfTakerPays;
extern SField const sfTakerGets;
extern SField const sfLowLimit;
extern SField const sfHighLimit;
extern SField const sfFee;
extern SField const sfSendMax;

// currency amount (uncommon)
extern SField const sfMinimumOffer;
extern SField const sfRippleEscrow;
extern SField const sfDeliveredAmount;

// variable length
extern SField const sfPublicKey;
extern SField const sfMessageKey;
extern SField const sfSigningPubKey;
extern SField const sfTxnSignature;
extern SField const sfGenerator;
extern SField const sfSignature;
extern SField const sfDomain;
extern SField const sfFundCode;
extern SField const sfRemoveCode;
extern SField const sfExpireCode;
extern SField const sfCreateCode;
extern SField const sfMemoType;
extern SField const sfMemoData;
extern SField const sfMemoFormat;
extern SField const sfMultiSignature;

// account
extern SField const sfAccount;
extern SField const sfOwner;
extern SField const sfDestination;
extern SField const sfIssuer;
extern SField const sfTarget;
extern SField const sfRegularKey;

// path set
extern SField const sfPaths;

// vector of 256-bit
extern SField const sfIndexes;
extern SField const sfHashes;
extern SField const sfAmendments;

// inner object
// OBJECT/1 is reserved for end of object
extern SField const sfTransactionMetaData;
extern SField const sfCreatedNode;
extern SField const sfDeletedNode;
extern SField const sfModifiedNode;
extern SField const sfPreviousFields;
extern SField const sfFinalFields;
extern SField const sfNewFields;
extern SField const sfTemplateEntry;
extern SField const sfMemo;
extern SField const sfSignerEntry;
extern SField const sfSigningAccount;

// array of objects
// ARRAY/1 is reserved for end of array
extern SField const sfSigningAccounts;
extern SField const sfTxnSignatures;
extern SField const sfSignerEntries;
extern SField const sfTemplate;
extern SField const sfNecessary;
extern SField const sfSufficient;
extern SField const sfAffectedNodes;
extern SField const sfMemos;

} // ripple

#endif
