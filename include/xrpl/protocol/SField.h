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

#include <xrpl/basics/safe_cast.h>
#include <xrpl/json/json_value.h>

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
class STIssue;
class STBlob;
template <int>
class STBitString;
template <class>
class STInteger;
class STNumber;
class STXChainBridge;
class STVector256;
class STCurrency;

#pragma push_macro("XMACRO")
#undef XMACRO

#define XMACRO(STYPE)                             \
    /* special types */                           \
    STYPE(STI_UNKNOWN, -2)                        \
    STYPE(STI_NOTPRESENT, 0)                      \
    STYPE(STI_UINT16, 1)                          \
                                                  \
    /* types (common) */                          \
    STYPE(STI_UINT32, 2)                          \
    STYPE(STI_UINT64, 3)                          \
    STYPE(STI_UINT128, 4)                         \
    STYPE(STI_UINT256, 5)                         \
    STYPE(STI_AMOUNT, 6)                          \
    STYPE(STI_VL, 7)                              \
    STYPE(STI_ACCOUNT, 8)                         \
    STYPE(STI_NUMBER, 9)                          \
                                                  \
    /* 10-13 are reserved */                      \
    STYPE(STI_OBJECT, 14)                         \
    STYPE(STI_ARRAY, 15)                          \
                                                  \
    /* types (uncommon) */                        \
    STYPE(STI_UINT8, 16)                          \
    STYPE(STI_UINT160, 17)                        \
    STYPE(STI_PATHSET, 18)                        \
    STYPE(STI_VECTOR256, 19)                      \
    STYPE(STI_UINT96, 20)                         \
    STYPE(STI_UINT192, 21)                        \
    STYPE(STI_UINT384, 22)                        \
    STYPE(STI_UINT512, 23)                        \
    STYPE(STI_ISSUE, 24)                          \
    STYPE(STI_XCHAIN_BRIDGE, 25)                  \
    STYPE(STI_CURRENCY, 26)                       \
                                                  \
    /* high-level types */                        \
    /* cannot be serialized inside other types */ \
    STYPE(STI_TRANSACTION, 10001)                 \
    STYPE(STI_LEDGERENTRY, 10002)                 \
    STYPE(STI_VALIDATION, 10003)                  \
    STYPE(STI_METADATA, 10004)

#pragma push_macro("TO_ENUM")
#undef TO_ENUM
#pragma push_macro("TO_MAP")
#undef TO_MAP

#define TO_ENUM(name, value) name = value,
#define TO_MAP(name, value) {#name, value},

enum SerializedTypeID { XMACRO(TO_ENUM) };

static std::map<std::string, int> const sTypeMap = {XMACRO(TO_MAP)};

#undef XMACRO
#undef TO_ENUM

#pragma pop_macro("XMACRO")
#pragma pop_macro("TO_ENUM")
#pragma pop_macro("TO_MAP")

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
        sMD_BaseTen = 0x20,
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

    operator Json::StaticString const&() const
    {
        return jsonName;
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

    static std::map<int, SField const*> const&
    getKnownCodeToField()
    {
        return knownCodeToField;
    }

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
    explicit TypedField(private_access_tag_t pat, Args&&... args);
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
using SF_UINT96 = TypedField<STBitString<96>>;
using SF_UINT128 = TypedField<STBitString<128>>;
using SF_UINT160 = TypedField<STBitString<160>>;
using SF_UINT192 = TypedField<STBitString<192>>;
using SF_UINT256 = TypedField<STBitString<256>>;
using SF_UINT384 = TypedField<STBitString<384>>;
using SF_UINT512 = TypedField<STBitString<512>>;

using SF_ACCOUNT = TypedField<STAccount>;
using SF_AMOUNT = TypedField<STAmount>;
using SF_ISSUE = TypedField<STIssue>;
using SF_CURRENCY = TypedField<STCurrency>;
using SF_NUMBER = TypedField<STNumber>;
using SF_VL = TypedField<STBlob>;
using SF_VECTOR256 = TypedField<STVector256>;
using SF_XCHAIN_BRIDGE = TypedField<STXChainBridge>;

//------------------------------------------------------------------------------

// Use macros for most SField construction to enforce naming conventions.
#pragma push_macro("UNTYPED_SFIELD")
#undef UNTYPED_SFIELD
#pragma push_macro("TYPED_SFIELD")
#undef TYPED_SFIELD

#define UNTYPED_SFIELD(sfName, stiSuffix, fieldValue, ...) \
    extern SField const sfName;
#define TYPED_SFIELD(sfName, stiSuffix, fieldValue, ...) \
    extern SF_##stiSuffix const sfName;

extern SField const sfInvalid;
extern SField const sfGeneric;

#include <xrpl/protocol/detail/sfields.macro>

#undef TYPED_SFIELD
#pragma pop_macro("TYPED_SFIELD")
#undef UNTYPED_SFIELD
#pragma pop_macro("UNTYPED_SFIELD")

}  // namespace ripple

#endif
