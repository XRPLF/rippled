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

#include <ripple/json/json_value.h>
#include <cstdint>
#include <utility>

namespace ripple {

/*

Some fields have a different meaning for their
    default value versus not present.
        Example:
            QualityIn on a TrustLine

*/

//------------------------------------------------------------------------------

// Forwards
class STAccount;
class STAmount;
class STBlob;
template <std::size_t>
class STBitString;
template <class>
class STInteger;
class STVector256;

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
    IsSigning const         signingField;
    std::string             jsonName;

    SField(SField const&) = delete;
    SField& operator=(SField const&) = delete;
    SField(SField&&) = default;

protected:
    // These constructors can only be called from FieldNames.cpp
    SField (SerializedTypeID tid, int fv, const char* fn,
            int meta = sMD_Default, IsSigning signing = IsSigning::yes);
    explicit SField (int fc);
    SField (SerializedTypeID id, int val);

public:
    // getField will dynamically construct a new SField if necessary
    static const SField& getField (int fieldCode);
    static const SField& getField (std::string const& fieldName);
    static const SField& getField (int type, int value)
    {
        return getField (field_code (type, value));
    }
    static const SField& getField (SerializedTypeID type, int value)
    {
        return getField (field_code (type, value));
    }

    std::string getName () const;
    bool hasName () const
    {
        return !fieldName.empty ();
    }

    std::string const& getJsonName () const
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

    static int compare (const SField& f1, const SField& f2);

    struct make;  // public, but still an implementation detail

private:
    static int num;
};

/** A field with a type known at compile time. */
template <class T>
struct TypedField : SField
{
    using type = T;

    template <class... Args>
    explicit
    TypedField (Args&&... args)
        : SField(std::forward<Args>(args)...)
    {
    }

    TypedField(TypedField&& u) : SField(std::move(u))
    {
    }
};

/** Indicate boost::optional field semantics. */
template <class T>
struct OptionaledField
{
    TypedField<T> const* f;

    explicit
    OptionaledField (TypedField<T> const& f_)
        : f (&f_)
    {
    }
};

template <class T>
inline
OptionaledField<T>
operator~(TypedField<T> const& f)
{
    return OptionaledField<T>(f);
}

//------------------------------------------------------------------------------

//------------------------------------------------------------------------------

using SF_U8 = TypedField<STInteger<std::uint8_t>>;
using SF_U16 = TypedField<STInteger<std::uint16_t>>;
using SF_U32 = TypedField<STInteger<std::uint32_t>>;
using SF_U64 = TypedField<STInteger<std::uint64_t>>;
using SF_U128 = TypedField<STBitString<128>>;
using SF_U160 = TypedField<STBitString<160>>;
using SF_U256 = TypedField<STBitString<256>>;
using SF_Account = TypedField<STAccount>;
using SF_Amount = TypedField<STAmount>;
using SF_Blob = TypedField<STBlob>;
using SF_Vec256 = TypedField<STVector256>;

//------------------------------------------------------------------------------

extern SField const sfInvalid;
extern SField const sfGeneric;
extern SField const sfLedgerEntry;
extern SField const sfTransaction;
extern SField const sfValidation;
extern SField const sfMetadata;

// 8-bit integers
extern SF_U8 const sfCloseResolution;
extern SF_U8 const sfMethod;
extern SF_U8 const sfTransactionResult;
extern SF_U8 const sfTickSize;

// 16-bit integers
extern SF_U16 const sfLedgerEntryType;
extern SF_U16 const sfTransactionType;
extern SF_U16 const sfSignerWeight;

// 32-bit integers (common)
extern SF_U32 const sfFlags;
extern SF_U32 const sfSourceTag;
extern SF_U32 const sfSequence;
extern SF_U32 const sfPreviousTxnLgrSeq;
extern SF_U32 const sfLedgerSequence;
extern SF_U32 const sfCloseTime;
extern SF_U32 const sfParentCloseTime;
extern SF_U32 const sfSigningTime;
extern SF_U32 const sfExpiration;
extern SF_U32 const sfTransferRate;
extern SF_U32 const sfWalletSize;
extern SF_U32 const sfOwnerCount;
extern SF_U32 const sfDestinationTag;

// 32-bit integers (uncommon)
extern SF_U32 const sfHighQualityIn;
extern SF_U32 const sfHighQualityOut;
extern SF_U32 const sfLowQualityIn;
extern SF_U32 const sfLowQualityOut;
extern SF_U32 const sfQualityIn;
extern SF_U32 const sfQualityOut;
extern SF_U32 const sfStampEscrow;
extern SF_U32 const sfBondAmount;
extern SF_U32 const sfLoadFee;
extern SF_U32 const sfOfferSequence;
extern SF_U32 const sfFirstLedgerSequence;  // Deprecated: do not use
extern SF_U32 const sfLastLedgerSequence;
extern SF_U32 const sfTransactionIndex;
extern SF_U32 const sfOperationLimit;
extern SF_U32 const sfReferenceFeeUnits;
extern SF_U32 const sfReserveBase;
extern SF_U32 const sfReserveIncrement;
extern SF_U32 const sfSetFlag;
extern SF_U32 const sfClearFlag;
extern SF_U32 const sfSignerQuorum;
extern SF_U32 const sfCancelAfter;
extern SF_U32 const sfFinishAfter;
extern SF_U32 const sfSignerListID;
extern SF_U32 const sfSettleDelay;

// 64-bit integers
extern SF_U64 const sfIndexNext;
extern SF_U64 const sfIndexPrevious;
extern SF_U64 const sfBookNode;
extern SF_U64 const sfOwnerNode;
extern SF_U64 const sfBaseFee;
extern SF_U64 const sfExchangeRate;
extern SF_U64 const sfLowNode;
extern SF_U64 const sfHighNode;
extern SF_U64 const sfDestinationNode;
extern SF_U64 const sfCookie;


// 128-bit
extern SF_U128 const sfEmailHash;

// 160-bit (common)
extern SF_U160 const sfTakerPaysCurrency;
extern SF_U160 const sfTakerPaysIssuer;
extern SF_U160 const sfTakerGetsCurrency;
extern SF_U160 const sfTakerGetsIssuer;

// 256-bit (common)
extern SF_U256 const sfLedgerHash;
extern SF_U256 const sfParentHash;
extern SF_U256 const sfTransactionHash;
extern SF_U256 const sfAccountHash;
extern SF_U256 const sfPreviousTxnID;
extern SF_U256 const sfLedgerIndex;
extern SF_U256 const sfWalletLocator;
extern SF_U256 const sfRootIndex;
extern SF_U256 const sfAccountTxnID;

// 256-bit (uncommon)
extern SF_U256 const sfBookDirectory;
extern SF_U256 const sfInvoiceID;
extern SF_U256 const sfNickname;
extern SF_U256 const sfAmendment;
extern SF_U256 const sfTicketID;
extern SF_U256 const sfDigest;
extern SF_U256 const sfPayChannel;
extern SF_U256 const sfConsensusHash;
extern SF_U256 const sfCheckID;

// currency amount (common)
extern SF_Amount const sfAmount;
extern SF_Amount const sfBalance;
extern SF_Amount const sfLimitAmount;
extern SF_Amount const sfTakerPays;
extern SF_Amount const sfTakerGets;
extern SF_Amount const sfLowLimit;
extern SF_Amount const sfHighLimit;
extern SF_Amount const sfFee;
extern SF_Amount const sfSendMax;
extern SF_Amount const sfDeliverMin;

// currency amount (uncommon)
extern SF_Amount const sfMinimumOffer;
extern SF_Amount const sfRippleEscrow;
extern SF_Amount const sfDeliveredAmount;

// variable length (common)
extern SF_Blob const sfPublicKey;
extern SF_Blob const sfMessageKey;
extern SF_Blob const sfSigningPubKey;
extern SF_Blob const sfTxnSignature;
extern SF_Blob const sfSignature;
extern SF_Blob const sfDomain;
extern SF_Blob const sfFundCode;
extern SF_Blob const sfRemoveCode;
extern SF_Blob const sfExpireCode;
extern SF_Blob const sfCreateCode;
extern SF_Blob const sfMemoType;
extern SF_Blob const sfMemoData;
extern SF_Blob const sfMemoFormat;

// variable length (uncommon)
extern SF_Blob const sfFulfillment;
extern SF_Blob const sfCondition;
extern SF_Blob const sfMasterSignature;

// account
extern SF_Account const sfAccount;
extern SF_Account const sfOwner;
extern SF_Account const sfDestination;
extern SF_Account const sfIssuer;
extern SF_Account const sfAuthorize;
extern SF_Account const sfUnauthorize;
extern SF_Account const sfTarget;
extern SF_Account const sfRegularKey;

// path set
extern SField const sfPaths;

// vector of 256-bit
extern SF_Vec256 const sfIndexes;
extern SF_Vec256 const sfHashes;
extern SF_Vec256 const sfAmendments;

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
extern SField const sfSigner;
extern SField const sfMajority;

// array of objects
// ARRAY/1 is reserved for end of array
// extern SField const sfSigningAccounts;  // Never been used.
extern SField const sfSigners;
extern SField const sfSignerEntries;
extern SField const sfTemplate;
extern SField const sfNecessary;
extern SField const sfSufficient;
extern SField const sfAffectedNodes;
extern SField const sfMemos;
extern SField const sfMajorities;

//------------------------------------------------------------------------------

} // ripple

#endif
