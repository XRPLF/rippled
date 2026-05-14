/** @file
 *  Explicit template specializations for STInteger<T> covering the five
 *  instantiations used by the XRPL protocol: uint8, uint16, uint32, uint64,
 *  and int32.
 *
 *  The generic template defined in STInteger.h supplies all methods that
 *  behave identically across integer widths (add, isDefault, isEquivalent,
 *  operator=, copy/move plumbing). This file provides the four virtual methods
 *  that must differ per instantiation: the SerialIter deserialization
 *  constructor, getSType(), getText(), and getJson(). Keeping these in a single
 *  translation unit prevents duplicate-symbol ODR violations and avoids
 *  implicitly instantiating all specializations in every TU that includes the
 *  header.
 *
 *  @note getText() and getJson() are not naive integer-to-string converters:
 *      they inspect the field's SField identity and emit semantic strings for
 *      well-known protocol fields (sfTransactionResult, sfLedgerEntryType,
 *      sfTransactionType, sfPermissionValue). This file and STParsedJSON.cpp
 *      form a symmetric round-trip pair — STParsedJSON parses those semantic
 *      strings back to integers during JSON ingestion.
 */
#include <xrpl/protocol/STInteger.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>

#include <charconv>
#include <cstdint>
#include <iterator>
#include <string>
#include <system_error>

namespace xrpl {

// --- STUInt8 (unsigned char) ---

/** Deserialize an STUInt8 from a wire byte stream. */
template <>
STInteger<unsigned char>::STInteger(SerialIter& sit, SField const& name)
    : STInteger(name, sit.get8())
{
}

/** @return STI_UINT8 */
template <>
SerializedTypeID
STUInt8::getSType() const
{
    return STI_UINT8;
}

/** Return a human-readable representation of the value.
 *
 *  For sfTransactionResult, resolves the raw byte to the long-form TER
 *  description (e.g., "The transaction was applied. Only final in a
 *  validated ledger.") via transResultInfo(). Unrecognized codes — which
 *  should be unreachable under correct operation — log an error and fall
 *  through to the raw decimal string.
 *
 *  @return Human-readable TER description for sfTransactionResult fields;
 *      decimal string for all other fields.
 */
template <>
std::string
STUInt8::getText() const
{
    if (getFName() == sfTransactionResult)
    {
        std::string token, human;

        if (transResultInfo(TER::fromInt(value_), token, human))
            return human;

        // LCOV_EXCL_START
        JLOG(debugLog().error()) << "Unknown result code in metadata: " << value_;
        // LCOV_EXCL_STOP
    }

    return std::to_string(value_);
}

/** Return a JSON representation suitable for API clients.
 *
 *  For sfTransactionResult, resolves the raw byte to its short token
 *  string (e.g., "tesSUCCESS") via transResultInfo(). This token is the
 *  form consumed by JSON API clients and recorded in transaction metadata.
 *  Unrecognized codes — unreachable under correct operation — log an error
 *  and fall through to the raw integer.
 *
 *  @return JSON string token for sfTransactionResult fields (e.g.,
 *      "tesSUCCESS"); raw integer for all other fields.
 */
template <>
json::Value
STUInt8::getJson(JsonOptions) const
{
    if (getFName() == sfTransactionResult)
    {
        std::string token, human;

        if (transResultInfo(TER::fromInt(value_), token, human))
            return token;

        // LCOV_EXCL_START
        JLOG(debugLog().error()) << "Unknown result code in metadata: " << value_;
        // LCOV_EXCL_STOP
    }

    return value_;
}

// --- STUInt16 (uint16_t) ---

/** Deserialize an STUInt16 from a wire byte stream. */
template <>
STInteger<std::uint16_t>::STInteger(SerialIter& sit, SField const& name)
    : STInteger(name, sit.get16())
{
}

/** @return STI_UINT16 */
template <>
SerializedTypeID
STUInt16::getSType() const
{
    return STI_UINT16;
}

/** Return a human-readable representation of the value.
 *
 *  For sfLedgerEntryType, looks up the name in the LedgerFormats singleton
 *  (e.g., value 0x61 → "AccountRoot"). For sfTransactionType, looks up the
 *  name in TxFormats (e.g., 0 → "Payment"). The cast through safeCast<> is
 *  deliberate — it documents the intentional enum reinterpretation of the
 *  raw wire integer. Falls back to decimal for unrecognized values.
 *
 *  @return Registered format name for sfLedgerEntryType and
 *      sfTransactionType fields; decimal string for all other fields.
 */
template <>
std::string
STUInt16::getText() const
{
    if (getFName() == sfLedgerEntryType)
    {
        auto item = LedgerFormats::getInstance().findByType(safeCast<LedgerEntryType>(value_));

        if (item != nullptr)
            return item->getName();
    }

    if (getFName() == sfTransactionType)
    {
        auto item = TxFormats::getInstance().findByType(safeCast<TxType>(value_));

        if (item != nullptr)
            return item->getName();
    }

    return std::to_string(value_);
}

/** Return a JSON representation suitable for API clients.
 *
 *  Semantics mirror getText(): sfLedgerEntryType yields a string like
 *  "Offer", sfTransactionType yields a string like "Payment". Falls back to
 *  the raw integer for unrecognized codes so that old ledger entries with
 *  deprecated types remain representable.
 *
 *  @return JSON string name for sfLedgerEntryType and sfTransactionType
 *      fields; raw integer for all other fields.
 */
template <>
json::Value
STUInt16::getJson(JsonOptions) const
{
    if (getFName() == sfLedgerEntryType)
    {
        auto item = LedgerFormats::getInstance().findByType(safeCast<LedgerEntryType>(value_));

        if (item != nullptr)
            return item->getName();
    }

    if (getFName() == sfTransactionType)
    {
        auto item = TxFormats::getInstance().findByType(safeCast<TxType>(value_));

        if (item != nullptr)
            return item->getName();
    }

    return value_;
}

// --- STUInt32 (uint32_t) ---

/** Deserialize an STUInt32 from a wire byte stream. */
template <>
STInteger<std::uint32_t>::STInteger(SerialIter& sit, SField const& name)
    : STInteger(name, sit.get32())
{
}

/** @return STI_UINT32 */
template <>
SerializedTypeID
STUInt32::getSType() const
{
    return STI_UINT32;
}

/** Return a human-readable representation of the value.
 *
 *  For sfPermissionValue, delegates to Permission::getPermissionName(),
 *  which tries granular permission lookup first (values ≥65537), then falls
 *  back to transaction-type-based permission resolution. Falls back to the
 *  raw decimal string if the value is not recognized.
 *
 *  @return Permission name string for sfPermissionValue fields; decimal
 *      string for all other fields.
 */
template <>
std::string
STUInt32::getText() const
{
    if (getFName() == sfPermissionValue)
    {
        auto const permissionName = Permission::getInstance().getPermissionName(value_);
        if (permissionName)
            return *permissionName;
    }
    return std::to_string(value_);
}

/** Return a JSON representation suitable for API clients.
 *
 *  Semantics mirror getText(): sfPermissionValue yields a string like
 *  "Payment" or "PaymentMint". Falls back to the raw integer for
 *  unrecognized values.
 *
 *  @return JSON string name for sfPermissionValue fields; raw integer for
 *      all other fields.
 */
template <>
json::Value
STUInt32::getJson(JsonOptions) const
{
    if (getFName() == sfPermissionValue)
    {
        auto const permissionName = Permission::getInstance().getPermissionName(value_);
        if (permissionName)
            return *permissionName;
    }

    return value_;
}

// --- STUInt64 (uint64_t) ---

/** Deserialize an STUInt64 from a wire byte stream. */
template <>
STInteger<std::uint64_t>::STInteger(SerialIter& sit, SField const& name)
    : STInteger(name, sit.get64())
{
}

/** @return STI_UINT64 */
template <>
SerializedTypeID
STUInt64::getSType() const
{
    return STI_UINT64;
}

/** Return a decimal string for diagnostic and log use.
 *
 *  Always decimal regardless of field identity. Use getJson() when
 *  producing output for API consumers.
 */
template <>
std::string
STUInt64::getText() const
{
    return std::to_string(value_);
}

/** Return a JSON string representation of the 64-bit value.
 *
 *  Always returns a JSON string (never a JSON number) to avoid precision
 *  loss from IEEE 754 double, which cannot represent all uint64_t values
 *  exactly. The base is determined by the SField::kSMD_BASE_TEN metadata
 *  flag set on the field's SField at registration time:
 *
 *  - Fields annotated sMD_BaseTen (e.g., sequence-like counters) render in
 *    decimal (e.g., sfMaximumAmount → "18446744073709551615").
 *  - All other fields render in lowercase hexadecimal (e.g., quality or
 *    rate fields → "ffffffffffffffff").
 *
 *  Conversion uses std::to_chars — locale-independent and allocation-free.
 *
 *  @return Decimal or hexadecimal string depending on the field's
 *      kSMD_BASE_TEN metadata flag.
 */
template <>
json::Value
STUInt64::getJson(JsonOptions) const
{
    auto convertToString = [](uint64_t const value, int const base) {
        XRPL_ASSERT(base == 10 || base == 16, "xrpl::STUInt64::getJson : base 10 or 16");
        std::string str(base == 10 ? 20 : 16, 0);
        auto ret = std::to_chars(str.data(), str.data() + str.size(), value, base);
        XRPL_ASSERT(ret.ec == std::errc(), "xrpl::STUInt64::getJson : to_chars succeeded");
        str.resize(std::distance(str.data(), ret.ptr));
        return str;
    };

    if (auto const& fName = getFName(); fName.shouldMeta(SField::kSMD_BASE_TEN))
        return convertToString(value_, 10);

    return convertToString(value_, 16);
}

// --- STInt32 (int32_t) ---

/** Deserialize an STInt32 from a wire byte stream.
 *
 *  @note Reads via sit.get32() (the same call as STUInt32) and relies on
 *      the implicit bit-pattern reinterpretation when the unsigned result is
 *      stored in value_ (int32_t). The wire format treats integer fields as
 *      type-agnostic bytes.
 */
template <>
STInteger<std::int32_t>::STInteger(SerialIter& sit, SField const& name)
    : STInteger(name, sit.get32())
{
}

/** @return STI_INT32 */
template <>
SerializedTypeID
STInt32::getSType() const
{
    return STI_INT32;
}

/** @return Decimal string representation of the signed value. */
template <>
std::string
STInt32::getText() const
{
    return std::to_string(value_);
}

/** @return Raw signed integer as a JSON number. */
template <>
json::Value
STInt32::getJson(JsonOptions) const
{
    return value_;
}

}  // namespace xrpl
