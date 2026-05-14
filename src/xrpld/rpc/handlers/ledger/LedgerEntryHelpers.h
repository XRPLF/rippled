/** @file
 *  Private JSON parsing and error-construction utilities for the
 *  `ledger_entry` RPC handler.
 *
 *  This header is intentionally included only by `LedgerEntry.cpp`.
 *  All functions are `inline` because there is a single consumer and no
 *  ODR risk.  The `Expected<T, Json::Value>` return discipline lets every
 *  per-type parse function propagate failures via the uniform
 *  `if (!result) return Unexpected(result.error())` idiom rather than
 *  throwing.
 */

#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/json/json_errors.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/jss.h>

#include <functional>

/** Low-level JSON parsing and validation vocabulary for `ledger_entry`.
 *
 *  Provides the `parse<T>()` template family, the `required<T>()` combinator,
 *  concrete typed wrappers (`requiredAccountID`, `requiredUInt32`, …), three
 *  error builders (`missingFieldError`, `invalidFieldError`, `malformedError`),
 *  and the `parseBridgeFields` helper used by all XChainBridge-related object
 *  parsers.  All errors carry `rpcINVALID_PARAMS` and return
 *  `Unexpected<Json::Value>` compatible with `Expected<T, Json::Value>`.
 */
namespace xrpl::LedgerEntryHelpers {

/** Build a `rpcINVALID_PARAMS` error for a field that is absent or null.
 *
 *  @param field The JSON field name that was missing.
 *  @param err   Optional error code string; defaults to `"malformedRequest"`.
 *      Pass a domain-specific code (e.g. `"malformedLockingChainDoor"`) when
 *      a more precise code is useful for the caller.
 *  @return An `Unexpected<Json::Value>` carrying the error object.
 */
inline Unexpected<json::Value>
missingFieldError(json::StaticString const field, std::optional<std::string> err = std::nullopt)
{
    json::Value json = json::ValueType::Object;
    json[jss::error] = err.value_or("malformedRequest");
    json[jss::error_code] = RpcInvalidParams;
    json[jss::error_message] = RPC::missingFieldMessage(std::string(field.cStr()));
    return Unexpected(json);
}

/** Build a `rpcINVALID_PARAMS` error for a field whose value failed parsing.
 *
 *  The `error_message` is constructed by `RPC::expectedFieldMessage`, which
 *  produces a human-readable sentence naming `field` and the expected `type`.
 *
 *  @param err   Domain-specific error code string (e.g. `"malformedAddress"`).
 *  @param field The JSON field name whose value was invalid.
 *  @param type  Human-readable expected type (e.g. `"AccountID"`, `"Hash256"`).
 *  @return An `Unexpected<Json::Value>` carrying the error object.
 */
inline Unexpected<json::Value>
invalidFieldError(std::string const& err, json::StaticString const field, std::string const& type)
{
    json::Value json = json::ValueType::Object;
    json[jss::error] = err;
    json[jss::error_code] = RpcInvalidParams;
    json[jss::error_message] = RPC::expectedFieldMessage(field, type);
    return Unexpected(json);
}

/** Build a `rpcINVALID_PARAMS` error with fully caller-defined code and message.
 *
 *  Use when the failure is semantic rather than a simple missing-or-wrong-type
 *  situation — for example, "Cannot have a trustline to self." or "Must have
 *  exactly one of `owner` and `dir_root` fields."
 *
 *  @param err     Error code string (e.g. `"malformedRequest"`).
 *  @param message Full human-readable error_message string.
 *  @return An `Unexpected<Json::Value>` carrying the error object.
 */
inline Unexpected<json::Value>
malformedError(std::string const& err, std::string const& message)
{
    json::Value json = json::ValueType::Object;
    json[jss::error] = err;
    json[jss::error_code] = RpcInvalidParams;
    json[jss::error_message] = message;
    return Unexpected(json);
}

/** Pre-flight check that all listed fields are present and non-null.
 *
 *  Iterates `fields` and short-circuits on the first absent or null entry,
 *  returning the corresponding `missingFieldError`.  Used as a guard before
 *  individual field extraction in parsers that need several fields before any
 *  one of them is useful (e.g. `parseAMM`, `parseRippleState`,
 *  `parseBridgeFields`, `parseAuthorizeCredentials`).
 *
 *  @param params The JSON object to inspect.
 *  @param fields The required field names, checked in order.
 *  @param err    Optional error code override forwarded to `missingFieldError`.
 *  @return `true` wrapped in `Expected` if all fields are present; otherwise
 *      an `Unexpected` carrying the first missing-field error.
 */
inline Expected<bool, json::Value>
hasRequired(
    json::Value const& params,
    std::initializer_list<json::StaticString> fields,
    std::optional<std::string> err = std::nullopt)
{
    for (auto const field : fields)
    {
        if (!params.isMember(field) || params[field].isNull())
        {
            return missingFieldError(field, err);
        }
    }
    return true;
}

/** Try to decode a JSON value into type `T`.
 *
 *  Declared here; only the explicit specializations for `AccountID`,
 *  `uint256`, `uint192`, `std::uint32_t`, and `Asset` are provided.
 *  Returns `std::nullopt` on any parse failure with no error detail, keeping
 *  the function pure and composable for callers that probe multiple types
 *  before committing (e.g. `parseDirectoryNode` tries `uint256` before
 *  `AccountID`).
 *
 *  @tparam T    The target type.  Must have an explicit specialization.
 *  @param param The JSON value to decode.
 *  @return The decoded value, or `std::nullopt` on failure.
 */
template <class T>
std::optional<T>
parse(json::Value const& param);

/** Extract a required field from a JSON object, returning an error on failure.
 *
 *  Combines a presence check with `parse<T>()`: if the field is absent or
 *  null, returns `missingFieldError`; if present but unparseable, returns
 *  `invalidFieldError` using `expectedType` as the human-readable type name.
 *  Concrete wrappers (`requiredAccountID`, `requiredUInt32`, etc.) supply the
 *  `err` and `expectedType` strings so callers need not repeat them.
 *
 *  @tparam T           The target type; must have a `parse<T>` specialization.
 *  @param params       The JSON object containing the field.
 *  @param fieldName    The key to look up in `params`.
 *  @param err          Error code string emitted on a type mismatch.
 *  @param expectedType Human-readable type string (e.g. `"AccountID"`)
 *      embedded in the `error_message` on type mismatch.
 *  @return The decoded value, or an `Unexpected` error object.
 */
template <class T>
Expected<T, json::Value>
required(
    json::Value const& params,
    json::StaticString const fieldName,
    std::string const& err,
    std::string const& expectedType)
{
    if (!params.isMember(fieldName) || params[fieldName].isNull())
    {
        return missingFieldError(fieldName);
    }
    if (auto obj = parse<T>(params[fieldName]))
    {
        return *obj;
    }
    return invalidFieldError(err, fieldName, expectedType);
}

/** Decode a Base58-encoded `AccountID` from a JSON string.
 *
 *  Rejects the all-zero account ID (`isZero()` guard) because that sentinel
 *  value cannot correspond to a real account and would otherwise produce a
 *  silently incorrect keylet.
 *
 *  @param param JSON value expected to be a Base58Check account string.
 *  @return The decoded `AccountID`, or `std::nullopt` if the value is not a
 *      string, fails Base58 decoding, or is the zero account.
 */
template <>
inline std::optional<AccountID>
parse(json::Value const& param)
{
    if (!param.isString())
        return std::nullopt;

    auto const account = parseBase58<AccountID>(param.asString());
    if (!account || account->isZero())
    {
        return std::nullopt;
    }

    return account;
}

/** Extract a required `AccountID` field, returning `rpcINVALID_PARAMS` on failure.
 *
 *  @param params    The JSON object containing the field.
 *  @param fieldName The key to look up.
 *  @param err       Error code string emitted on type mismatch or zero account.
 *  @return The decoded `AccountID`, or an `Unexpected` error object.
 *  @see parse<AccountID>
 */
inline Expected<AccountID, json::Value>
requiredAccountID(
    json::Value const& params,
    json::StaticString const fieldName,
    std::string const& err)
{
    return required<AccountID>(params, fieldName, err, "AccountID");
}

/** Decode a hex-encoded byte blob from a JSON string, enforcing a length cap.
 *
 *  Not part of the `parse<T>()` template family because it takes an extra
 *  `maxLength` parameter.  The credential-type field, for instance, is
 *  length-bounded by protocol rules.
 *
 *  @param param     JSON value expected to be a hex string.
 *  @param maxLength Maximum number of decoded bytes accepted.
 *  @return The decoded blob, or `std::nullopt` if the value is not a string,
 *      fails hex decoding, decodes to an empty blob, or exceeds `maxLength`.
 */
inline std::optional<Blob>
parseHexBlob(json::Value const& param, std::size_t maxLength)
{
    if (!param.isString())
        return std::nullopt;

    auto blob = strUnHex(param.asString());
    if (!blob || blob->empty() || blob->size() > maxLength)
        return std::nullopt;

    return blob;
}

/** Extract a required hex-blob field, returning `rpcINVALID_PARAMS` on failure.
 *
 *  @param params    The JSON object containing the field.
 *  @param fieldName The key to look up.
 *  @param maxLength Maximum number of decoded bytes accepted.
 *  @param err       Error code string emitted when the blob is malformed or
 *      out of range.
 *  @return The decoded blob, or an `Unexpected` error object.
 *  @see parseHexBlob
 */
inline Expected<Blob, json::Value>
requiredHexBlob(
    json::Value const& params,
    json::StaticString const fieldName,
    std::size_t maxLength,
    std::string const& err)
{
    if (!params.isMember(fieldName) || params[fieldName].isNull())
    {
        return missingFieldError(fieldName);
    }
    if (auto blob = parseHexBlob(params[fieldName], maxLength))
    {
        return *blob;
    }
    return invalidFieldError(err, fieldName, "hex string");
}

/** Decode a `uint32_t` from a JSON unsigned, positive signed integer, or string.
 *
 *  Accepts a JSON string form (e.g. `"42"`) via `beast::lexicalCastChecked`
 *  to accommodate clients that serialize numbers as strings.  Negative signed
 *  integers are rejected cleanly.
 *
 *  @param param JSON value: unsigned integer, non-negative signed integer, or
 *      decimal string representation.
 *  @return The decoded value, or `std::nullopt` on type mismatch or negative
 *      value.
 */
template <>
inline std::optional<std::uint32_t>
parse(json::Value const& param)
{
    if (param.isUInt() || (param.isInt() && param.asInt() >= 0))
        return param.asUInt();

    if (param.isString())
    {
        std::uint32_t v = 0;
        if (beast::lexicalCastChecked(v, param.asString()))
            return v;
    }

    return std::nullopt;
}

/** Extract a required `uint32_t` field, returning `rpcINVALID_PARAMS` on failure.
 *
 *  @param params    The JSON object containing the field.
 *  @param fieldName The key to look up.
 *  @param err       Error code string emitted on type mismatch.
 *  @return The decoded value, or an `Unexpected` error object.
 *  @see parse<std::uint32_t>
 */
inline Expected<std::uint32_t, json::Value>
requiredUInt32(
    json::Value const& params,
    json::StaticString const fieldName,
    std::string const& err)
{
    return required<std::uint32_t>(params, fieldName, err, "number");
}

/** Decode a 256-bit hash from a hex JSON string.
 *
 *  @param param JSON string containing a 64-character hex hash.
 *  @return The decoded `uint256`, or `std::nullopt` if the value is not a
 *      string or fails hex parsing.
 */
template <>
inline std::optional<uint256>
parse(json::Value const& param)
{
    uint256 uNodeIndex;
    if (!param.isString() || !uNodeIndex.parseHex(param.asString()))
    {
        return std::nullopt;
    }

    return uNodeIndex;
}

/** Extract a required `uint256` (Hash256) field, returning `rpcINVALID_PARAMS` on failure.
 *
 *  @param params    The JSON object containing the field.
 *  @param fieldName The key to look up.
 *  @param err       Error code string emitted on type mismatch.
 *  @return The decoded hash, or an `Unexpected` error object.
 */
inline Expected<uint256, json::Value>
requiredUInt256(
    json::Value const& params,
    json::StaticString const fieldName,
    std::string const& err)
{
    return required<uint256>(params, fieldName, err, "Hash256");
}

/** Decode a 192-bit value from a hex JSON string.
 *
 *  Used for MPT issuance IDs (`mpt_issuance_id`), which are 24-byte (192-bit)
 *  identifiers.
 *
 *  @param param JSON string containing a 48-character hex value.
 *  @return The decoded `uint192`, or `std::nullopt` if the value is not a
 *      string or fails hex parsing.
 */
template <>
inline std::optional<uint192>
parse(json::Value const& param)
{
    uint192 field;
    if (!param.isString() || !field.parseHex(param.asString()))
    {
        return std::nullopt;
    }

    return field;
}

/** Extract a required `uint192` (Hash192 / MPT issuance ID) field, returning
 *  `rpcINVALID_PARAMS` on failure.
 *
 *  @param params    The JSON object containing the field.
 *  @param fieldName The key to look up.
 *  @param err       Error code string emitted on type mismatch.
 *  @return The decoded value, or an `Unexpected` error object.
 */
inline Expected<uint192, json::Value>
requiredUInt192(
    json::Value const& params,
    json::StaticString const fieldName,
    std::string const& err)
{
    return required<uint192>(params, fieldName, err, "Hash192");
}

/** Decode an `Asset` (XRP, IOU issue, or MPT) from a JSON value.
 *
 *  Delegates to `assetFromJson`, which throws `std::runtime_error` on
 *  malformed input; the exception is caught here and converted to
 *  `std::nullopt` so callers see a uniform optional interface.
 *
 *  @param param JSON object or string describing the asset.
 *  @return The decoded `Asset`, or `std::nullopt` on any parse failure.
 */
template <>
inline std::optional<Asset>
parse(json::Value const& param)
{
    try
    {
        return assetFromJson(param);
    }
    catch (std::runtime_error const&)
    {
        return std::nullopt;
    }
}

/** Extract a required `Asset` field, returning `rpcINVALID_PARAMS` on failure.
 *
 *  @param params    The JSON object containing the field.
 *  @param fieldName The key to look up.
 *  @param err       Error code string emitted on type mismatch.
 *  @return The decoded `Asset`, or an `Unexpected` error object.
 */
inline Expected<Asset, json::Value>
requiredAsset(json::Value const& params, json::StaticString const fieldName, std::string const& err)
{
    return required<Asset>(params, fieldName, err, "Asset");
}

/** Assemble an `STXChainBridge` from a JSON object containing the four
 *  canonical cross-chain bridge sub-fields.
 *
 *  Expects `params` to carry:
 *  - `LockingChainDoor` and `IssuingChainDoor` — `AccountID` strings.
 *  - `LockingChainIssue` and `IssuingChainIssue` — `Issue` objects decoded
 *    via `issueFromJson`, which throws `std::runtime_error` on malformed
 *    input; the exception is caught and converted to `invalidFieldError`.
 *
 *  Reused by `parseBridge`, `parseXChainOwnedClaimID`, and
 *  `parseXChainOwnedCreateAccountClaimID` in `LedgerEntry.cpp`.
 *
 *  @param params JSON object with the four bridge sub-fields.
 *  @return The constructed `STXChainBridge`, or an `Unexpected` error object
 *      if any sub-field is absent, null, or unparseable.
 */
inline Expected<STXChainBridge, json::Value>
parseBridgeFields(json::Value const& params)
{
    if (auto const value = hasRequired(
            params,
            {jss::LockingChainDoor,
             jss::LockingChainIssue,
             jss::IssuingChainDoor,
             jss::IssuingChainIssue});
        !value)
    {
        return Unexpected(value.error());
    }

    auto const lockingChainDoor =
        requiredAccountID(params, jss::LockingChainDoor, "malformedLockingChainDoor");
    if (!lockingChainDoor)
    {
        return Unexpected(lockingChainDoor.error());
    }

    auto const issuingChainDoor =
        requiredAccountID(params, jss::IssuingChainDoor, "malformedIssuingChainDoor");
    if (!issuingChainDoor)
    {
        return Unexpected(issuingChainDoor.error());
    }

    Issue lockingChainIssue;
    try
    {
        lockingChainIssue = issueFromJson(params[jss::LockingChainIssue]);
    }
    catch (std::runtime_error const& ex)
    {
        return invalidFieldError("malformedIssue", jss::LockingChainIssue, "Issue");
    }

    Issue issuingChainIssue;
    try
    {
        issuingChainIssue = issueFromJson(params[jss::IssuingChainIssue]);
    }
    catch (std::runtime_error const& ex)
    {
        return invalidFieldError("malformedIssue", jss::IssuingChainIssue, "Issue");
    }

    return STXChainBridge(
        *lockingChainDoor, lockingChainIssue, *issuingChainDoor, issuingChainIssue);
}

}  // namespace xrpl::LedgerEntryHelpers
