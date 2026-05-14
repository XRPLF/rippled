#pragma once

/** @file
 *  Typed JSON extraction with SField keys and structured exceptions.
 *
 *  Provides `json::getOrThrow<T>` and `json::getOptional<T>`: replacements for
 *  raw `Json::Value` access that throw structured exceptions instead of
 *  silently returning defaults or coercing types.  The key type is
 *  `xrpl::SField` rather than a plain string, tying every lookup to a declared
 *  XRPL protocol field and eliminating magic string literals.
 *
 *  @note Additional specializations for `xrpl::AccountID`, `xrpl::PublicKey`,
 *      and `xrpl::STAmount` are defined in their respective headers, following
 *      the same pattern established here.  The primary consumer is
 *      `XChainAttestations.cpp`, which calls `getOrThrow` in constructor
 *      initializer lists to reject partially-initialized attestation objects
 *      before they reach validation logic.
 */

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/contract.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>

#include <charconv>
#include <exception>
#include <optional>

namespace json {

/** Exception thrown when a required JSON key is absent from the object.
 *
 *  The diagnostic message is constructed lazily on the first call to `what()`
 *  to avoid a string allocation at the throw site.
 */
struct JsonMissingKeyError : std::exception
{
    /** The missing key name, pointing into the `StaticString` storage. */
    char const* const key;

    /** Lazily-populated message returned by `what()`. */
    mutable std::string msg;

    /** Construct with the name of the missing key. */
    JsonMissingKeyError(json::StaticString const& k) : key{k.cStr()}
    {
    }

    /** Return a human-readable description of the missing key.
     *
     *  The message string is built on the first call and cached in `msg`.
     */
    char const*
    what() const noexcept override
    {
        if (msg.empty())
        {
            msg = std::string("Missing json key: ") + key;
        }
        return msg.c_str();
    }
};

/** Exception thrown when a JSON key is present but its value has the wrong type.
 *
 *  The diagnostic message is constructed lazily on the first call to `what()`
 *  to avoid a string allocation at the throw site.
 */
struct JsonTypeMismatchError : std::exception
{
    /** The key whose value had an unexpected type. */
    char const* const key;

    /** Human-readable name of the expected type (e.g., `"string"`, `"uint64"`). */
    std::string const expectedType;

    /** Lazily-populated message returned by `what()`. */
    mutable std::string msg;

    /** Construct with the key name and the name of the expected type. */
    JsonTypeMismatchError(json::StaticString const& k, std::string et)
        : key{k.cStr()}, expectedType{std::move(et)}
    {
    }

    /** Return a human-readable description of the type mismatch.
     *
     *  The message string is built on the first call and cached in `msg`.
     */
    char const*
    what() const noexcept override
    {
        if (msg.empty())
        {
            msg = std::string("Type mismatch on json key: ") + key +
                "; expected type: " + expectedType;
        }
        return msg.c_str();
    }
};

/** Extract a typed value from a JSON object using an XRPL protocol field key.
 *
 *  The key is taken from `field.getJsonName()`, a `Json::StaticString` that
 *  avoids dynamic allocation in the JSON library's hash map and ties every
 *  lookup to a declared XRPL protocol field.
 *
 *  This primary template is intentionally unimplementable: the
 *  `static_assert` fires for any `T` that lacks an explicit specialization,
 *  turning unsupported types into a compile error rather than a runtime
 *  surprise.  Specializations exist for `std::string`, `bool`,
 *  `std::uint64_t`, and `xrpl::Buffer` in this header, and for
 *  `xrpl::AccountID`, `xrpl::PublicKey`, and `xrpl::STAmount` in their
 *  respective headers.
 *
 *  @tparam T The target C++ type.  Must have an explicit specialization.
 *  @param v The JSON object to read from.
 *  @param field The SField identifying the key to look up.
 *  @return The extracted and type-checked value.
 *  @throws JsonMissingKeyError if the key is absent from `v`.
 *  @throws JsonTypeMismatchError if the key is present but the value cannot
 *      be interpreted as `T`.
 */
template <class T>
T
getOrThrow(json::Value const& v, xrpl::SField const& field)
{
    static_assert(sizeof(T) == -1, "This function must be specialized");
}

/** Extract a UTF-8 string from a JSON object field.
 *
 *  No type coercion is applied: the value must be a JSON string (`isString()`).
 *  Numeric values are not accepted.
 *
 *  @param v The JSON object to read from.
 *  @param field The SField identifying the key to look up.
 *  @return The string value.
 *  @throws JsonMissingKeyError if the key is absent.
 *  @throws JsonTypeMismatchError if the value is not a JSON string.
 */
template <>
inline std::string
getOrThrow(json::Value const& v, xrpl::SField const& field)
{
    using namespace xrpl;
    json::StaticString const& key = field.getJsonName();
    if (!v.isMember(key))
        Throw<JsonMissingKeyError>(key);

    json::Value const& inner = v[key];
    if (!inner.isString())
        Throw<JsonTypeMismatchError>(key, "string");
    return inner.asString();
}

/** Extract a boolean from a JSON object field.
 *
 *  Accepts either a native JSON boolean or any integral value, where any
 *  non-zero integer maps to `true`.  This mirrors a common XRPL convention
 *  where boolean-semantic fields such as `sfWasLockingChainSend` are encoded
 *  as `0`/`1` integers in JSON rather than as JSON `true`/`false`.
 *
 *  @param v The JSON object to read from.
 *  @param field The SField identifying the key to look up.
 *  @return The boolean value.
 *  @throws JsonMissingKeyError if the key is absent.
 *  @throws JsonTypeMismatchError if the value is neither a JSON boolean nor an
 *      integral type.
 */
template <>
inline bool
getOrThrow(json::Value const& v, xrpl::SField const& field)
{
    using namespace xrpl;
    json::StaticString const& key = field.getJsonName();
    if (!v.isMember(key))
        Throw<JsonMissingKeyError>(key);
    json::Value const& inner = v[key];
    if (inner.isBool())
        return inner.asBool();
    if (!inner.isIntegral())
        Throw<JsonTypeMismatchError>(key, "bool");

    return inner.asInt() != 0;
}

/** Extract a 64-bit unsigned integer from a JSON object field.
 *
 *  Three source formats are accepted, in order of preference:
 *  1. Native JSON unsigned integer (`isUInt()`).
 *  2. Signed JSON integer that is non-negative (negative values throw).
 *  3. Hex-encoded string, parsed via `std::from_chars` in base 16.
 *
 *  The hex string path exists because 64-bit values exceed JavaScript's
 *  safe integer range (`2^53 - 1`), so some XRPL JSON producers encode
 *  large integers as hex strings.  A partial parse — where `from_chars`
 *  succeeds but does not consume the entire string — is treated as a type
 *  error.
 *
 *  @param v The JSON object to read from.
 *  @param field The SField identifying the key to look up.
 *  @return The extracted value as `std::uint64_t`.
 *  @throws JsonMissingKeyError if the key is absent.
 *  @throws JsonTypeMismatchError if the value is a negative integer, a string
 *      that is not valid hex, or a type that does not match any accepted form.
 */
template <>
inline std::uint64_t
getOrThrow(json::Value const& v, xrpl::SField const& field)
{
    using namespace xrpl;
    json::StaticString const& key = field.getJsonName();
    if (!v.isMember(key))
        Throw<JsonMissingKeyError>(key);
    json::Value const& inner = v[key];
    if (inner.isUInt())
        return inner.asUInt();
    if (inner.isInt())
    {
        auto const r = inner.asInt();
        if (r < 0)
            Throw<JsonTypeMismatchError>(key, "uint64");
        return r;
    }
    if (inner.isString())
    {
        auto const s = inner.asString();
        std::uint64_t val = 0;

        auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), val, 16);

        if (ec != std::errc() || (p != s.data() + s.size()))
            Throw<JsonTypeMismatchError>(key, "uint64");
        return val;
    }
    Throw<JsonTypeMismatchError>(key, "uint64");
}

/** Extract a raw byte buffer from a JSON object field encoded as a hex string.
 *
 *  Delegates to `getOrThrow<std::string>` to fetch the raw hex string, then
 *  decodes it with `strUnHex`.  A decode failure (e.g., odd length, non-hex
 *  characters) throws `JsonTypeMismatchError`.
 *
 *  @note There is a conceptual mismatch between `xrpl::Buffer` (raw bytes) and
 *      the `STBlob` wire type.  This specialization bridges that gap for fields
 *      like `sfSignature` when deserializing witness-server JSON.
 *
 *  @param v The JSON object to read from.
 *  @param field The SField identifying the key to look up.
 *  @return The decoded byte buffer.
 *  @throws JsonMissingKeyError if the key is absent.
 *  @throws JsonTypeMismatchError if the value is not a string or contains
 *      invalid hex data.
 */
template <>
inline xrpl::Buffer
getOrThrow(json::Value const& v, xrpl::SField const& field)
{
    using namespace xrpl;
    std::string const hex = getOrThrow<std::string>(v, field);
    if (auto const r = strUnHex(hex))
    {
        // TODO: mismatch between a buffer and a blob
        return Buffer{r->data(), r->size()};
    }
    Throw<JsonTypeMismatchError>(field.getJsonName(), "Buffer");
}

/** Extract a typed value from a JSON object, returning `std::nullopt` on any
 *  error.
 *
 *  Wraps `getOrThrow<T>` in a catch-all handler so that a missing key, a type
 *  mismatch, or any other exception simply yields `std::nullopt`.  The
 *  catch-all is intentional: the caller's only question is whether the field is
 *  present and valid, not which specific error occurred.
 *
 *  @note This function is part of the public API consumed by external projects
 *      such as the witness server, which runs outside the rippled process and
 *      needs to parse XRPL JSON payloads without depending on rippled internals.
 *
 *  @tparam T The target C++ type.  Must have a `getOrThrow` specialization.
 *  @param v The JSON object to read from.
 *  @param field The SField identifying the key to look up.
 *  @return The extracted value, or `std::nullopt` if the field is absent or
 *      cannot be decoded as `T`.
 */
template <class T>
std::optional<T>
getOptional(json::Value const& v, xrpl::SField const& field)
{
    try
    {
        return getOrThrow<T>(v, field);
    }
    catch (...)  // NOLINT(bugprone-empty-catch)
    {
    }
    return {};
}

}  // namespace json
