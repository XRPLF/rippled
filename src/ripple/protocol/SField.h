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

#include <ripple/basics/safe_cast.h>
#include <ripple/json/json_value.h>
#include <cstdint>
#include <map>
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

enum SerializedTypeID {
    // special types
    STI_UNKNOWN = -2,
    STI_DONE = -1,
    STI_NOTPRESENT = 0,

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
    STI_VALIDATION = 10003,
    STI_METADATA = 10004,
};

// constexpr
inline int
field_code(SerializedTypeID id, int index)
{
    return (safe_cast<int>(id) << 16) | index;
}

// constexpr
inline int
field_code(int id, int index)
{
    return (id << 16) | index;
}

/** Identifies fields.

    Fields are necessary to tag data in signed transactions so that
    the binary format of the transaction can be canonicalized.  All
    SFields are created at compile time.

    Each SField, once constructed, lives until program termination, and there
    is only one instance per fieldType/fieldValue pair which serves the entire
    application.
*/
class SField
{
public:
    enum {
        sMD_Never = 0x00,
        sMD_ChangeOrig = 0x01,   // original value when it changes
        sMD_ChangeNew = 0x02,    // new value when it changes
        sMD_DeleteFinal = 0x04,  // final value when it is deleted
        sMD_Create = 0x08,       // value when it's created
        sMD_Always = 0x10,  // value when node containing it is affected at all
        sMD_Default =
            sMD_ChangeOrig | sMD_ChangeNew | sMD_DeleteFinal | sMD_Create
    };

    enum class IsSigning : unsigned char { no, yes };
    static IsSigning const notSigning = IsSigning::no;

    int const fieldCode;               // (type<<16)|index
    SerializedTypeID const fieldType;  // STI_*
    int const fieldValue;              // Code number for protocol
    std::string const fieldName;
    int const fieldMeta;
    int const fieldNum;
    IsSigning const signingField;
    Json::StaticString const jsonName;

    SField(SField const&) = delete;
    SField&
    operator=(SField const&) = delete;
    SField(SField&&) = delete;
    SField&
    operator=(SField&&) = delete;

public:
    struct private_access_tag_t;  // public, but still an implementation detail

    // These constructors can only be called from SField.cpp
    SField(
        private_access_tag_t,
        SerializedTypeID tid,
        int fv,
        const char* fn,
        int meta = sMD_Default,
        IsSigning signing = IsSigning::yes);
    explicit SField(private_access_tag_t, int fc);

    static const SField&
    getField(int fieldCode);
    static const SField&
    getField(std::string const& fieldName);
    static const SField&
    getField(int type, int value)
    {
        return getField(field_code(type, value));
    }

    static const SField&
    getField(SerializedTypeID type, int value)
    {
        return getField(field_code(type, value));
    }

    std::string const&
    getName() const
    {
        return fieldName;
    }

    bool
    hasName() const
    {
        return fieldCode > 0;
    }

    Json::StaticString const&
    getJsonName() const
    {
        return jsonName;
    }

    bool
    isGeneric() const
    {
        return fieldCode == 0;
    }
    bool
    isInvalid() const
    {
        return fieldCode == -1;
    }
    bool
    isUseful() const
    {
        return fieldCode > 0;
    }
    bool
    isKnown() const
    {
        return fieldType != STI_UNKNOWN;
    }
    bool
    isBinary() const
    {
        return fieldValue < 256;
    }

    // A discardable field is one that cannot be serialized, and
    // should be discarded during serialization,like 'hash'.
    // You cannot serialize an object's hash inside that object,
    // but you can have it in the JSON representation.
    bool
    isDiscardable() const
    {
        return fieldValue > 256;
    }

    int
    getCode() const
    {
        return fieldCode;
    }
    int
    getNum() const
    {
        return fieldNum;
    }
    static int
    getNumFields()
    {
        return num;
    }

    bool
    isSigningField() const
    {
        return signingField == IsSigning::yes;
    }
    bool
    shouldMeta(int c) const
    {
        return (fieldMeta & c) != 0;
    }

    bool
    shouldInclude(bool withSigningField) const
    {
        return (fieldValue < 256) &&
            (withSigningField || (signingField == IsSigning::yes));
    }

    bool
    operator==(const SField& f) const
    {
        return fieldCode == f.fieldCode;
    }

    bool
    operator!=(const SField& f) const
    {
        return fieldCode != f.fieldCode;
    }

    static int
    compare(const SField& f1, const SField& f2);

private:
    static int num;
    static std::map<int, SField const*> knownCodeToField;
};

/** A field with a type known at compile time. */
template <class T>
struct TypedField : SField
{
    using type = T;

    template <class... Args>
    explicit TypedField(Args&&... args) : SField(std::forward<Args>(args)...)
    {
    }

    TypedField(TypedField&& u) : SField(std::move(u))
    {
    }
};

/** Indicate std::optional field semantics. */
template <class T>
struct OptionaledField
{
    TypedField<T> const* f;

    explicit OptionaledField(TypedField<T> const& f_) : f(&f_)
    {
    }
};

template <class T>
inline OptionaledField<T>
operator~(TypedField<T> const& f)
{
    return OptionaledField<T>(f);
}

//------------------------------------------------------------------------------

//------------------------------------------------------------------------------

using SF_UINT8 = TypedField<STInteger<std::uint8_t>>;
using SF_UINT16 = TypedField<STInteger<std::uint16_t>>;
using SF_UINT32 = TypedField<STInteger<std::uint32_t>>;
using SF_UINT64 = TypedField<STInteger<std::uint64_t>>;
using SF_HASH128 = TypedField<STBitString<128>>;
using SF_HASH160 = TypedField<STBitString<160>>;
using SF_HASH256 = TypedField<STBitString<256>>;
using SF_ACCOUNT = TypedField<STAccount>;
using SF_AMOUNT = TypedField<STAmount>;
using SF_VL = TypedField<STBlob>;
using SF_VECTOR256 = TypedField<STVector256>;

//------------------------------------------------------------------------------

extern SField const sfInvalid;
extern SField const sfGeneric;
extern SField const sfLedgerEntry;
extern SField const sfTransaction;
extern SField const sfValidation;
extern SField const sfMetadata;

// 8-bit integers
extern SF_UINT8 const sfCloseResolution;
extern SF_UINT8 const sfMethod;
extern SF_UINT8 const sfTransactionResult;
extern SF_UINT8 const sfTickSize;
extern SF_UINT8 const sfUNLModifyDisabling;

// 16-bit integers
extern SF_UINT16 const sfLedgerEntryType;
extern SF_UINT16 const sfTransactionType;
extern SF_UINT16 const sfSignerWeight;

// 16-bit integers (uncommon)
extern SF_UINT16 const sfVersion;

// 32-bit integers (common)
extern SF_UINT32 const sfFlags;
extern SF_UINT32 const sfSourceTag;
extern SF_UINT32 const sfSequence;
extern SF_UINT32 const sfPreviousTxnLgrSeq;
extern SF_UINT32 const sfLedgerSequence;
extern SF_UINT32 const sfCloseTime;
extern SF_UINT32 const sfParentCloseTime;
extern SF_UINT32 const sfSigningTime;
extern SF_UINT32 const sfExpiration;
extern SF_UINT32 const sfTransferRate;
extern SF_UINT32 const sfWalletSize;
extern SF_UINT32 const sfOwnerCount;
extern SF_UINT32 const sfDestinationTag;

// 32-bit integers (uncommon)
extern SF_UINT32 const sfHighQualityIn;
extern SF_UINT32 const sfHighQualityOut;
extern SF_UINT32 const sfLowQualityIn;
extern SF_UINT32 const sfLowQualityOut;
extern SF_UINT32 const sfQualityIn;
extern SF_UINT32 const sfQualityOut;
extern SF_UINT32 const sfStampEscrow;
extern SF_UINT32 const sfBondAmount;
extern SF_UINT32 const sfLoadFee;
extern SF_UINT32 const sfOfferSequence;
extern SF_UINT32 const sfFirstLedgerSequence;
extern SF_UINT32 const sfLastLedgerSequence;
extern SF_UINT32 const sfTransactionIndex;
extern SF_UINT32 const sfOperationLimit;
extern SF_UINT32 const sfReferenceFeeUnits;
extern SF_UINT32 const sfReserveBase;
extern SF_UINT32 const sfReserveIncrement;
extern SF_UINT32 const sfSetFlag;
extern SF_UINT32 const sfClearFlag;
extern SF_UINT32 const sfSignerQuorum;
extern SF_UINT32 const sfCancelAfter;
extern SF_UINT32 const sfFinishAfter;
extern SF_UINT32 const sfSignerListID;
extern SF_UINT32 const sfSettleDelay;
extern SF_UINT32 const sfTicketCount;
extern SF_UINT32 const sfTicketSequence;
extern SF_UINT32 const sfNotValidBefore;
extern SF_UINT32 const sfNotValidAfter;

// 64-bit integers
extern SF_UINT64 const sfIndexNext;
extern SF_UINT64 const sfIndexPrevious;
extern SF_UINT64 const sfBookNode;
extern SF_UINT64 const sfOwnerNode;
extern SF_UINT64 const sfBaseFee;
extern SF_UINT64 const sfExchangeRate;
extern SF_UINT64 const sfLowNode;
extern SF_UINT64 const sfHighNode;
extern SF_UINT64 const sfDestinationNode;
extern SF_UINT64 const sfCookie;
extern SF_UINT64 const sfServerVersion;

// 128-bit
extern SF_HASH128 const sfEmailHash;

// 160-bit (common)
extern SF_HASH160 const sfTakerPaysCurrency;
extern SF_HASH160 const sfTakerPaysIssuer;
extern SF_HASH160 const sfTakerGetsCurrency;
extern SF_HASH160 const sfTakerGetsIssuer;

// 256-bit (common)
extern SF_HASH256 const sfLedgerHash;
extern SF_HASH256 const sfParentHash;
extern SF_HASH256 const sfTransactionHash;
extern SF_HASH256 const sfAccountHash;
extern SF_HASH256 const sfPreviousTxnID;
extern SF_HASH256 const sfLedgerIndex;
extern SF_HASH256 const sfWalletLocator;
extern SF_HASH256 const sfRootIndex;
extern SF_HASH256 const sfAccountTxnID;

// 256-bit (uncommon)
extern SF_HASH256 const sfBookDirectory;
extern SF_HASH256 const sfInvoiceID;
extern SF_HASH256 const sfNickname;
extern SF_HASH256 const sfAmendment;
extern SF_HASH256 const sfDigest;
extern SF_HASH256 const sfChannel;
extern SF_HASH256 const sfConsensusHash;
extern SF_HASH256 const sfCheckID;
extern SF_HASH256 const sfValidatedHash;

// currency amount (common)
extern SF_AMOUNT const sfAmount;
extern SF_AMOUNT const sfBalance;
extern SF_AMOUNT const sfLimitAmount;
extern SF_AMOUNT const sfTakerPays;
extern SF_AMOUNT const sfTakerGets;
extern SF_AMOUNT const sfLowLimit;
extern SF_AMOUNT const sfHighLimit;
extern SF_AMOUNT const sfFee;
extern SF_AMOUNT const sfSendMax;
extern SF_AMOUNT const sfDeliverMin;

// currency amount (uncommon)
extern SF_AMOUNT const sfMinimumOffer;
extern SF_AMOUNT const sfRippleEscrow;
extern SF_AMOUNT const sfDeliveredAmount;

// variable length (common)
extern SF_VL const sfPublicKey;
extern SF_VL const sfMessageKey;
extern SF_VL const sfSigningPubKey;
extern SF_VL const sfTxnSignature;
extern SF_VL const sfSignature;
extern SF_VL const sfDomain;
extern SF_VL const sfFundCode;
extern SF_VL const sfRemoveCode;
extern SF_VL const sfExpireCode;
extern SF_VL const sfCreateCode;
extern SF_VL const sfMemoType;
extern SF_VL const sfMemoData;
extern SF_VL const sfMemoFormat;

// variable length (uncommon)
extern SF_VL const sfFulfillment;
extern SF_VL const sfCondition;
extern SF_VL const sfMasterSignature;
extern SF_VL const sfUNLModifyValidator;
extern SF_VL const sfValidatorToDisable;
extern SF_VL const sfValidatorToReEnable;

// account
extern SF_ACCOUNT const sfAccount;
extern SF_ACCOUNT const sfOwner;
extern SF_ACCOUNT const sfDestination;
extern SF_ACCOUNT const sfIssuer;
extern SF_ACCOUNT const sfAuthorize;
extern SF_ACCOUNT const sfUnauthorize;
extern SF_ACCOUNT const sfTarget;
extern SF_ACCOUNT const sfRegularKey;

// path set
extern SField const sfPaths;

// vector of 256-bit
extern SF_VECTOR256 const sfIndexes;
extern SF_VECTOR256 const sfHashes;
extern SF_VECTOR256 const sfAmendments;

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
extern SField const sfDisabledValidator;

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
extern SField const sfDisabledValidators;
//------------------------------------------------------------------------------

}  // namespace ripple

#endif
