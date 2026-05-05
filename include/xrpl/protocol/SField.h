#pragma once

#include <xrpl/basics/safe_cast.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Units.h>

#include <cstdint>
#include <map>

namespace xrpl {

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
    STYPE(StiUnknown, -2)                         \
    STYPE(StiNotpresent, 0)                       \
    STYPE(StiUinT16, 1)                           \
                                                  \
    /* types (common) */                          \
    STYPE(StiUinT32, 2)                           \
    STYPE(StiUinT64, 3)                           \
    STYPE(StiUinT128, 4)                          \
    STYPE(StiUinT256, 5)                          \
    STYPE(StiAmount, 6)                           \
    STYPE(StiVl, 7)                               \
    STYPE(StiAccount, 8)                          \
    STYPE(StiNumber, 9)                           \
    STYPE(StiInT32, 10)                           \
    STYPE(StiInT64, 11)                           \
                                                  \
    /* 12-13 are reserved */                      \
    STYPE(StiObject, 14)                          \
    STYPE(StiArray, 15)                           \
                                                  \
    /* types (uncommon) */                        \
    STYPE(StiUinT8, 16)                           \
    STYPE(StiUinT160, 17)                         \
    STYPE(StiPathset, 18)                         \
    STYPE(StiVectoR256, 19)                       \
    STYPE(StiUinT96, 20)                          \
    STYPE(StiUinT192, 21)                         \
    STYPE(StiUinT384, 22)                         \
    STYPE(StiUinT512, 23)                         \
    STYPE(StiIssue, 24)                           \
    STYPE(StiXchainBridge, 25)                    \
    STYPE(StiCurrency, 26)                        \
                                                  \
    /* high-level types */                        \
    /* cannot be serialized inside other types */ \
    STYPE(StiTransaction, 10001)                  \
    STYPE(StiLedgerentry, 10002)                  \
    STYPE(StiValidation, 10003)                   \
    STYPE(StiMetadata, 10004)

#pragma push_macro("TO_ENUM")
#undef TO_ENUM
#pragma push_macro("TO_MAP")
#undef TO_MAP

#define TO_ENUM(name, value) name = (value),
#define TO_MAP(name, value) {#name, value},

// Protocol infrastructure, 39+ files
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum SerializedTypeID { XMACRO(TO_ENUM) };

static std::map<std::string, int> const kS_TYPE_MAP = {XMACRO(TO_MAP)};

#undef XMACRO
#undef TO_ENUM

#pragma pop_macro("XMACRO")
#pragma pop_macro("TO_ENUM")
#pragma pop_macro("TO_MAP")

// constexpr
inline int
field_code(SerializedTypeID id, int index)
{
    return (safeCast<int>(id) << 16) | index;
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
    is only one instance per fieldType/fieldValue pair which serves the
    entire application.
*/
class SField
{
public:
    // Need to be named before converting
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
    enum {
        SMdNever = 0x00,
        SMdChangeOrig = 0x01,     // original value when it changes
        SMdChangeNew = 0x02,      // new value when it changes
        SMdDeleteFinal = 0x04,    // final value when it is deleted
        SMdCreate = 0x08,         // value when it's created
        SMdAlways = 0x10,         // value when node containing it is affected at all
        SMdBaseTen = 0x20,        // value is treated as base 10, overriding behavior
        SMdPseudoAccount = 0x40,  // if this field is set in an ACCOUNT_ROOT
                                  // _only_, then it is a pseudo-account
        SMdNeedsAsset = 0x80,     // This field needs to be associated with an
                                  // asset before it is serialized as a ledger
                                  // object. Intended for STNumber.
        SMdDefault = SMdChangeOrig | SMdChangeNew | SMdDeleteFinal | SMdCreate
    };

    enum class IsSigning : unsigned char { No, Yes };
    static IsSigning const kNOT_SIGNING = IsSigning::No;

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
        char const* fn,
        int meta = SMdDefault,
        IsSigning signing = IsSigning::Yes);
    explicit SField(private_access_tag_t, int fc, char const* fn);

    static SField const&
    getField(int fieldCode);
    static SField const&
    getField(std::string const& fieldName);
    static SField const&
    getField(int type, int value)
    {
        return getField(field_code(type, value));
    }

    static SField const&
    getField(SerializedTypeID type, int value)
    {
        return getField(field_code(type, value));
    }

    [[nodiscard]] std::string const&
    getName() const
    {
        return fieldName;
    }

    [[nodiscard]] bool
    hasName() const
    {
        return fieldCode > 0;
    }

    [[nodiscard]] Json::StaticString const&
    getJsonName() const
    {
        return jsonName;
    }

    operator Json::StaticString const&() const
    {
        return jsonName;
    }

    [[nodiscard]] bool
    isInvalid() const
    {
        return fieldCode == -1;
    }

    [[nodiscard]] bool
    isUseful() const
    {
        return fieldCode > 0;
    }

    [[nodiscard]] bool
    isBinary() const
    {
        return fieldValue < 256;
    }

    // A discardable field is one that cannot be serialized, and
    // should be discarded during serialization,like 'hash'.
    // You cannot serialize an object's hash inside that object,
    // but you can have it in the JSON representation.
    [[nodiscard]] bool
    isDiscardable() const
    {
        return fieldValue > 256;
    }

    [[nodiscard]] int
    getCode() const
    {
        return fieldCode;
    }
    [[nodiscard]] int
    getNum() const
    {
        return fieldNum;
    }
    static int
    getNumFields()
    {
        return num;
    }

    [[nodiscard]] bool
    shouldMeta(int c) const
    {
        return (fieldMeta & c) != 0;
    }

    [[nodiscard]] bool
    shouldInclude(bool withSigningField) const
    {
        return (fieldValue < 256) && (withSigningField || (signingField == IsSigning::Yes));
    }

    bool
    operator==(SField const& f) const
    {
        return fieldCode == f.fieldCode;
    }

    bool
    operator!=(SField const& f) const
    {
        return fieldCode != f.fieldCode;
    }

    static int
    compare(SField const& f1, SField const& f2);

    static std::unordered_map<int, SField const*> const&
    getKnownCodeToField()
    {
        return knownCodeToField;
    }

private:
    static int num;
    static std::unordered_map<int, SField const*> knownCodeToField;
    static std::unordered_map<std::string, SField const*> knownNameToField;
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

    explicit OptionaledField(TypedField<T> const& f) : f(&f)
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

using SF_INT32 = TypedField<STInteger<std::int32_t>>;
using SF_INT64 = TypedField<STInteger<std::int64_t>>;

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

#define UNTYPED_SFIELD(sfName, stiSuffix, fieldValue, ...) extern SField const sfName;
#define TYPED_SFIELD(sfName, stiSuffix, fieldValue, ...) extern SF_##stiSuffix const sfName;

extern SField const kSF_INVALID;
extern SField const kSF_GENERIC;

#include <xrpl/protocol/detail/sfields.macro>

#undef TYPED_SFIELD
#pragma pop_macro("TYPED_SFIELD")
#undef UNTYPED_SFIELD
#pragma pop_macro("UNTYPED_SFIELD")

}  // namespace xrpl
