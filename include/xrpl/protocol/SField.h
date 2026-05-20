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

// NOLINTBEGIN(readability-identifier-naming)
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
    STYPE(STI_INT32, 10)                          \
    STYPE(STI_INT64, 11)                          \
                                                  \
    /* 12-13 are reserved */                      \
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

#define TO_ENUM(name, value) name = (value),
#define TO_MAP(name, value) {#name, value},

// Protocol infrastructure, 39+ files
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum SerializedTypeID { XMACRO(TO_ENUM) };

static std::map<std::string, int> const kSTypeMap = {XMACRO(TO_MAP)};

#undef XMACRO
#undef TO_ENUM

#pragma pop_macro("XMACRO")
#pragma pop_macro("TO_ENUM")
#pragma pop_macro("TO_MAP")
// NOLINTEND(readability-identifier-naming)

// constexpr
inline int
fieldCode(SerializedTypeID id, int index)
{
    return (safeCast<int>(id) << 16) | index;
}

// constexpr
inline int
fieldCode(int id, int index)
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
    static constexpr auto kSmdNever = 0x00;
    static constexpr auto kSmdChangeOrig = 0x01;   // original value when it changes
    static constexpr auto kSmdChangeNew = 0x02;    // new value when it changes
    static constexpr auto kSmdDeleteFinal = 0x04;  // final value when it is deleted
    static constexpr auto kSmdCreate = 0x08;       // value when it's created
    static constexpr auto kSmdAlways = 0x10;   // value when node containing it is affected at all
    static constexpr auto kSmdBaseTen = 0x20;  // value is treated as base 10, overriding behavior
    static constexpr auto kSmdPseudoAccount = 0x40;  // if this field is set in an ACCOUNT_ROOT
                                                     // _only_, then it is a pseudo-account
    static constexpr auto kSmdNeedsAsset = 0x80;     // This field needs to be associated with an
                                                     // asset before it is serialized as a ledger
                                                     // object. Intended for STNumber.
    static constexpr auto kSmdDefault =
        kSmdChangeOrig | kSmdChangeNew | kSmdDeleteFinal | kSmdCreate;

    enum class IsSigning : unsigned char { No, Yes };
    static IsSigning const kNotSigning = IsSigning::No;

    int const fieldCodeMem;            // (type<<16)|index // TODO: rename, clashes with function
    SerializedTypeID const fieldType;  // STI_*
    int const fieldValue;              // Code number for protocol
    std::string const fieldName;
    int const fieldMeta;
    int const fieldNum;
    IsSigning const signingField;
    json::StaticString const jsonName;

    SField(SField const&) = delete;
    SField&
    operator=(SField const&) = delete;
    SField(SField&&) = delete;
    SField&
    operator=(SField&&) = delete;

public:
    struct PrivateAccessTagT;  // public, but still an implementation detail

    // These constructors can only be called from SField.cpp
    SField(
        PrivateAccessTagT,
        SerializedTypeID tid,
        int fv,
        char const* fn,
        int meta = kSmdDefault,
        IsSigning signing = IsSigning::Yes);
    explicit SField(PrivateAccessTagT, int fc, char const* fn);

    static SField const&
    getField(int fieldCode);
    static SField const&
    getField(std::string const& fieldName);
    static SField const&
    getField(int type, int value)
    {
        return getField(fieldCode(type, value));
    }

    static SField const&
    getField(SerializedTypeID type, int value)
    {
        return getField(fieldCode(type, value));
    }

    [[nodiscard]] std::string const&
    getName() const
    {
        return fieldName;
    }

    [[nodiscard]] bool
    hasName() const
    {
        return fieldCodeMem > 0;
    }

    [[nodiscard]] json::StaticString const&
    getJsonName() const
    {
        return jsonName;
    }

    operator json::StaticString const&() const
    {
        return jsonName;
    }

    [[nodiscard]] bool
    isInvalid() const
    {
        return fieldCodeMem == -1;
    }

    [[nodiscard]] bool
    isUseful() const
    {
        return fieldCodeMem > 0;
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
        return fieldCodeMem;
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
        return fieldCodeMem == f.fieldCodeMem;
    }

    bool
    operator!=(SField const& f) const
    {
        return fieldCodeMem != f.fieldCodeMem;
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
    explicit TypedField(PrivateAccessTagT pat, Args&&... args);
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

extern SField const sfInvalid;  // NOLINT(readability-identifier-naming)
extern SField const sfGeneric;  // NOLINT(readability-identifier-naming)

#include <xrpl/protocol/detail/sfields.macro>

#undef TYPED_SFIELD
#pragma pop_macro("TYPED_SFIELD")
#undef UNTYPED_SFIELD
#pragma pop_macro("UNTYPED_SFIELD")

}  // namespace xrpl
