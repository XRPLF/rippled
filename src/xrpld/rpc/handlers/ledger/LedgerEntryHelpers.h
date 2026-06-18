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

#include <expected>
#include <functional>

namespace xrpl::LedgerEntryHelpers {

inline std::unexpected<json::Value>
missingFieldError(json::StaticString const field, std::optional<std::string> err = std::nullopt)
{
    json::Value json = json::ValueType::Object;
    json[jss::error] = err.value_or("malformedRequest");
    json[jss::error_code] = RpcInvalidParams;
    json[jss::error_message] = RPC::missingFieldMessage(std::string(field.cStr()));
    return std::unexpected(json);
}

inline std::unexpected<json::Value>
invalidFieldError(std::string const& err, json::StaticString const field, std::string const& type)
{
    json::Value json = json::ValueType::Object;
    json[jss::error] = err;
    json[jss::error_code] = RpcInvalidParams;
    json[jss::error_message] = RPC::expectedFieldMessage(field, type);
    return std::unexpected(json);
}

inline std::unexpected<json::Value>
malformedError(std::string const& err, std::string const& message)
{
    json::Value json = json::ValueType::Object;
    json[jss::error] = err;
    json[jss::error_code] = RpcInvalidParams;
    json[jss::error_message] = message;
    return std::unexpected(json);
}

inline std::expected<bool, json::Value>
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

template <class T>
std::optional<T>
parse(json::Value const& param);

template <class T>
std::expected<T, json::Value>
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

inline std::expected<AccountID, json::Value>
requiredAccountID(
    json::Value const& params,
    json::StaticString const fieldName,
    std::string const& err)
{
    return required<AccountID>(params, fieldName, err, "AccountID");
}

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

inline std::expected<Blob, json::Value>
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

inline std::expected<std::uint32_t, json::Value>
requiredUInt32(
    json::Value const& params,
    json::StaticString const fieldName,
    std::string const& err)
{
    return required<std::uint32_t>(params, fieldName, err, "number");
}

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

inline std::expected<uint256, json::Value>
requiredUInt256(
    json::Value const& params,
    json::StaticString const fieldName,
    std::string const& err)
{
    return required<uint256>(params, fieldName, err, "Hash256");
}

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

inline std::expected<uint192, json::Value>
requiredUInt192(
    json::Value const& params,
    json::StaticString const fieldName,
    std::string const& err)
{
    return required<uint192>(params, fieldName, err, "Hash192");
}

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

inline std::expected<Asset, json::Value>
requiredAsset(json::Value const& params, json::StaticString const fieldName, std::string const& err)
{
    return required<Asset>(params, fieldName, err, "Asset");
}

inline std::expected<STXChainBridge, json::Value>
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
        return std::unexpected(value.error());
    }

    auto const lockingChainDoor =
        requiredAccountID(params, jss::LockingChainDoor, "malformedLockingChainDoor");
    if (!lockingChainDoor)
    {
        return std::unexpected(lockingChainDoor.error());
    }

    auto const issuingChainDoor =
        requiredAccountID(params, jss::IssuingChainDoor, "malformedIssuingChainDoor");
    if (!issuingChainDoor)
    {
        return std::unexpected(issuingChainDoor.error());
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
