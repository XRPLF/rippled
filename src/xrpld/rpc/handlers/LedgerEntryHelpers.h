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

namespace ripple {

namespace LedgerEntryHelpers {

Unexpected<Json::Value>
missingFieldError(
    Json::StaticString const field,
    std::optional<std::string> err = std::nullopt)
{
    Json::Value json = Json::objectValue;
    auto error = RPC::missing_field_message(std::string(field.c_str()));
    json[jss::error] = err.value_or("malformedRequest");
    json[jss::error_code] = rpcINVALID_PARAMS;
    json[jss::error_message] = std::move(error);
    return Unexpected(json);
}

Unexpected<Json::Value>
invalidFieldError(
    std::string const& err,
    Json::StaticString const field,
    std::string const& type)
{
    Json::Value json = Json::objectValue;
    auto error = RPC::expected_field_message(field, type);
    json[jss::error] = err;
    json[jss::error_code] = rpcINVALID_PARAMS;
    json[jss::error_message] = std::move(error);
    return Unexpected(json);
}

Unexpected<Json::Value>
malformedError(std::string const& err, std::string const& message)
{
    Json::Value json = Json::objectValue;
    json[jss::error] = err;
    json[jss::error_code] = rpcINVALID_PARAMS;
    json[jss::error_message] = message;
    return Unexpected(json);
}

Expected<bool, Json::Value>
hasRequired(
    Json::Value const& params,
    std::initializer_list<Json::StaticString> fields,
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
parse(Json::Value const& param);

template <class T>
Expected<T, Json::Value>
required(
    Json::Value const& params,
    Json::StaticString const fieldName,
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
std::optional<AccountID>
parse(Json::Value const& param)
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

Expected<AccountID, Json::Value>
requiredAccountID(
    Json::Value const& params,
    Json::StaticString const fieldName,
    std::string const& err)
{
    return required<AccountID>(params, fieldName, err, "AccountID");
}

std::optional<Blob>
parseHexBlob(Json::Value const& param, std::size_t maxLength)
{
    if (!param.isString())
        return std::nullopt;

    auto const blob = strUnHex(param.asString());
    if (!blob || blob->empty() || blob->size() > maxLength)
        return std::nullopt;

    return blob;
}

Expected<Blob, Json::Value>
requiredHexBlob(
    Json::Value const& params,
    Json::StaticString const fieldName,
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
std::optional<std::uint32_t>
parse(Json::Value const& param)
{
    if (param.isUInt() || (param.isInt() && param.asInt() >= 0))
        return param.asUInt();

    if (param.isString())
    {
        std::uint32_t v;
        if (beast::lexicalCastChecked(v, param.asString()))
            return v;
    }

    return std::nullopt;
}

Expected<std::uint32_t, Json::Value>
requiredUInt32(
    Json::Value const& params,
    Json::StaticString const fieldName,
    std::string const& err)
{
    return required<std::uint32_t>(params, fieldName, err, "number");
}

template <>
std::optional<uint256>
parse(Json::Value const& param)
{
    uint256 uNodeIndex;
    if (!param.isString() || !uNodeIndex.parseHex(param.asString()))
    {
        return std::nullopt;
    }

    return uNodeIndex;
}

Expected<uint256, Json::Value>
requiredUInt256(
    Json::Value const& params,
    Json::StaticString const fieldName,
    std::string const& err)
{
    return required<uint256>(params, fieldName, err, "Hash256");
}

template <>
std::optional<uint192>
parse(Json::Value const& param)
{
    uint192 field;
    if (!param.isString() || !field.parseHex(param.asString()))
    {
        return std::nullopt;
    }

    return field;
}

Expected<uint192, Json::Value>
requiredUInt192(
    Json::Value const& params,
    Json::StaticString const fieldName,
    std::string const& err)
{
    return required<uint192>(params, fieldName, err, "Hash192");
}

Expected<STXChainBridge, Json::Value>
parseBridgeFields(Json::Value const& params)
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

    auto const lockingChainDoor = requiredAccountID(
        params, jss::LockingChainDoor, "malformedLockingChainDoor");
    if (!lockingChainDoor)
    {
        return Unexpected(lockingChainDoor.error());
    }

    auto const issuingChainDoor = requiredAccountID(
        params, jss::IssuingChainDoor, "malformedIssuingChainDoor");
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
        return invalidFieldError(
            "malformedIssue", jss::LockingChainIssue, "Issue");
    }

    Issue issuingChainIssue;
    try
    {
        issuingChainIssue = issueFromJson(params[jss::IssuingChainIssue]);
    }
    catch (std::runtime_error const& ex)
    {
        return invalidFieldError(
            "malformedIssue", jss::IssuingChainIssue, "Issue");
    }

    return STXChainBridge(
        *lockingChainDoor,
        lockingChainIssue,
        *issuingChainDoor,
        issuingChainIssue);
}

}  // namespace LedgerEntryHelpers

}  // namespace ripple
