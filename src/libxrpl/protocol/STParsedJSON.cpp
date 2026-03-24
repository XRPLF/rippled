#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/STBlob.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/detail/STVar.h>

#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

namespace xrpl {

namespace STParsedJSONDetail {
template <typename U, typename S>
constexpr std::enable_if_t<std::is_unsigned<U>::value && std::is_signed<S>::value, U>
toUnsigned(S value)
{
    if (value < 0 || std::numeric_limits<U>::max() < value)
        Throw<std::runtime_error>("Value out of range");
    return static_cast<U>(value);
}

template <typename U1, typename U2>
constexpr std::enable_if_t<std::is_unsigned<U1>::value && std::is_unsigned<U2>::value, U1>
toUnsigned(U2 value)
{
    if (std::numeric_limits<U1>::max() < value)
        Throw<std::runtime_error>("Value out of range");
    return static_cast<U1>(value);
}

// LCOV_EXCL_START
static inline std::string
makeName(std::string const& object, std::string const& field)
{
    if (field.empty())
        return object;

    return object + "." + field;
}

static inline Json::Value
notAnObject(std::string const& object, std::string const& field)
{
    return RPC::make_error(
        rpcINVALID_PARAMS, "Field '" + makeName(object, field) + "' is not a JSON object.");
}

static inline Json::Value
notAnObject(std::string const& object)
{
    return notAnObject(object, "");
}

static inline Json::Value
notAnArray(std::string const& object)
{
    return RPC::make_error(rpcINVALID_PARAMS, "Field '" + object + "' is not a JSON array.");
}

static inline Json::Value
unknownField(std::string const& object, std::string const& field)
{
    return RPC::make_error(
        rpcINVALID_PARAMS, "Field '" + makeName(object, field) + "' is unknown.");
}

static inline Json::Value
outOfRange(std::string const& object, std::string const& field)
{
    return RPC::make_error(
        rpcINVALID_PARAMS, "Field '" + makeName(object, field) + "' is out of range.");
}

static inline Json::Value
badType(std::string const& object, std::string const& field)
{
    return RPC::make_error(
        rpcINVALID_PARAMS, "Field '" + makeName(object, field) + "' has bad type.");
}

static inline Json::Value
invalidData(std::string const& object, std::string const& field)
{
    return RPC::make_error(
        rpcINVALID_PARAMS, "Field '" + makeName(object, field) + "' has invalid data.");
}

static inline Json::Value
invalidData(std::string const& object)
{
    return invalidData(object, "");
}

static inline Json::Value
arrayExpected(std::string const& object, std::string const& field)
{
    return RPC::make_error(
        rpcINVALID_PARAMS, "Field '" + makeName(object, field) + "' must be a JSON array.");
}

static inline Json::Value
stringExpected(std::string const& object, std::string const& field)
{
    return RPC::make_error(
        rpcINVALID_PARAMS, "Field '" + makeName(object, field) + "' must be a string.");
}

static inline Json::Value
tooDeep(std::string const& object)
{
    return RPC::make_error(
        rpcINVALID_PARAMS, "Field '" + object + "' exceeds nesting depth limit.");
}

static inline Json::Value
singletonExpected(std::string const& object, unsigned int index)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + object + "[" + std::to_string(index) +
            "]' must be an object with a single key/object value.");
}

static inline Json::Value
templateMismatch(SField const& sField)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Object '" + sField.getName() + "' contents did not meet requirements for that type.");
}

static inline Json::Value
nonObjectInArray(std::string const& item, Json::UInt index)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Item '" + item + "' at index " + std::to_string(index) +
            " is not an object.  Arrays may only contain objects.");
}
// LCOV_EXCL_STOP

template <class STResult, class Integer>
static std::optional<detail::STVar>
parseUnsigned(
    SField const& field,
    std::string const& jsonName,
    std::string const& fieldName,
    SField const* name,
    Json::Value const& value,
    Json::Value& error)
{
    std::optional<detail::STVar> ret;

    try
    {
        if (value.isString())
        {
            ret = detail::make_stvar<STResult>(
                field,
                safe_cast<typename STResult::value_type>(
                    beast::lexicalCastThrow<Integer>(value.asString())));
        }
        else if (value.isInt())
        {
            ret = detail::make_stvar<STResult>(
                field, toUnsigned<typename STResult::value_type>(value.asInt()));
        }
        else if (value.isUInt())
        {
            ret = detail::make_stvar<STResult>(
                field, toUnsigned<typename STResult::value_type>(value.asUInt()));
        }
        else
        {
            error = badType(jsonName, fieldName);
            return ret;
        }
    }
    catch (std::exception const&)
    {
        error = invalidData(jsonName, fieldName);
        return ret;
    }

    return ret;
}

template <class STResult, class Integer = std::uint16_t>
static std::optional<detail::STVar>
parseUint16(
    SField const& field,
    std::string const& jsonName,
    std::string const& fieldName,
    SField const* name,
    Json::Value const& value,
    Json::Value& error)
{
    std::optional<detail::STVar> ret;

    try
    {
        if (value.isString())
        {
            std::string const strValue = value.asString();

            if (!strValue.empty() && ((strValue[0] < '0') || (strValue[0] > '9')))
            {
                if (field == sfTransactionType)
                {
                    ret = detail::make_stvar<STResult>(
                        field,
                        safe_cast<typename STResult::value_type>(static_cast<Integer>(
                            TxFormats::getInstance().findTypeByName(strValue))));

                    if (*name == sfGeneric)
                        name = &sfTransaction;
                }
                else if (field == sfLedgerEntryType)
                {
                    ret = detail::make_stvar<STResult>(
                        field,
                        safe_cast<typename STResult::value_type>(static_cast<Integer>(
                            LedgerFormats::getInstance().findTypeByName(strValue))));

                    if (*name == sfGeneric)
                        name = &sfLedgerEntry;
                }
                else
                {
                    error = invalidData(jsonName, fieldName);
                    return ret;
                }
            }
        }
        if (!ret)
            return parseUnsigned<STResult, Integer>(field, jsonName, fieldName, name, value, error);
    }
    catch (std::exception const&)
    {
        error = invalidData(jsonName, fieldName);
        return ret;
    }

    return ret;
}

template <class STResult, class Integer = std::uint32_t>
static std::optional<detail::STVar>
parseUint32(
    SField const& field,
    std::string const& jsonName,
    std::string const& fieldName,
    SField const* name,
    Json::Value const& value,
    Json::Value& error)
{
    std::optional<detail::STVar> ret;

    try
    {
        if (value.isString())
        {
            if (field == sfPermissionValue)
            {
                std::string const strValue = value.asString();
                auto const granularPermission =
                    Permission::getInstance().getGranularValue(strValue);
                if (granularPermission)
                {
                    ret = detail::make_stvar<STResult>(field, *granularPermission);
                }
                else
                {
                    auto const& txType = TxFormats::getInstance().findTypeByName(strValue);
                    ret = detail::make_stvar<STResult>(
                        field, Permission::getInstance().txToPermissionType(txType));
                }
            }
            else
            {
                ret = detail::make_stvar<STResult>(
                    field,
                    safe_cast<typename STResult::value_type>(
                        beast::lexicalCastThrow<Integer>(value.asString())));
            }
        }
        if (!ret)
        {
            return parseUnsigned<STResult, Integer>(field, jsonName, fieldName, name, value, error);
        }
    }
    catch (std::exception const&)
    {
        error = invalidData(jsonName, fieldName);
        return ret;
    }

    return ret;
}

// This function is used by parseObject to parse any JSON type that doesn't
// recurse.  Everything represented here is a leaf-type.
static std::optional<detail::STVar>
parseLeaf(
    std::string const& jsonName,
    std::string const& fieldName,
    SField const* name,
    Json::Value const& value,
    Json::Value& error)
{
    std::optional<detail::STVar> ret;

    auto const& field = SField::getField(fieldName);

    // checked in parseObject
    if (field == sfInvalid)
    {
        // LCOV_EXCL_START
        error = unknownField(jsonName, fieldName);
        return ret;
        // LCOV_EXCL_STOP
    }

    switch (field.fieldType)
    {
        case STI_UINT8:
            try
            {
                constexpr auto kMIN_VALUE = std::numeric_limits<std::uint8_t>::min();
                constexpr auto kMAX_VALUE = std::numeric_limits<std::uint8_t>::max();
                if (value.isString())
                {
                    std::string const strValue = value.asString();

                    if (!strValue.empty() && ((strValue[0] < '0') || (strValue[0] > '9')))
                    {
                        if (field == sfTransactionResult)
                        {
                            auto ter = transCode(strValue);

                            if (!ter || TERtoInt(*ter) < kMIN_VALUE || TERtoInt(*ter) > kMAX_VALUE)
                            {
                                error = outOfRange(jsonName, fieldName);
                                return ret;
                            }

                            ret = detail::make_stvar<STUInt8>(
                                field, static_cast<std::uint8_t>(TERtoInt(*ter)));
                        }
                        else
                        {
                            error = badType(jsonName, fieldName);
                            return ret;
                        }
                    }
                    else
                    {
                        ret = detail::make_stvar<STUInt8>(
                            field, beast::lexicalCastThrow<std::uint8_t>(strValue));
                    }
                }
                else if (value.isInt())
                {
                    if (value.asInt() < kMIN_VALUE || value.asInt() > kMAX_VALUE)
                    {
                        error = outOfRange(jsonName, fieldName);
                        return ret;
                    }

                    ret = detail::make_stvar<STUInt8>(
                        field, static_cast<std::uint8_t>(value.asInt()));
                }
                else if (value.isUInt())
                {
                    if (value.asUInt() > kMAX_VALUE)
                    {
                        error = outOfRange(jsonName, fieldName);
                        return ret;
                    }

                    ret = detail::make_stvar<STUInt8>(
                        field, static_cast<std::uint8_t>(value.asUInt()));
                }
                else
                {
                    error = badType(jsonName, fieldName);
                    return ret;
                }
            }
            catch (std::exception const&)
            {
                error = invalidData(jsonName, fieldName);
                return ret;
            }
            break;

        case STI_UINT16:
            ret = parseUint16<STUInt16>(field, jsonName, fieldName, name, value, error);
            if (!ret)
                return ret;

            break;

        case STI_UINT32:
            ret = parseUint32<STUInt32>(field, jsonName, fieldName, name, value, error);
            if (!ret)
                return ret;

            break;

        case STI_UINT64:
            try
            {
                if (value.isString())
                {
                    auto const str = value.asString();

                    std::uint64_t val = 0;

                    bool const useBase10 = field.shouldMeta(SField::sMD_BaseTen);

                    // if the field is amount, serialize as base 10
                    auto [p, ec] = std::from_chars(
                        str.data(), str.data() + str.size(), val, useBase10 ? 10 : 16);

                    if (ec != std::errc() || (p != str.data() + str.size()))
                        Throw<std::invalid_argument>("invalid data");

                    ret = detail::make_stvar<STUInt64>(field, val);
                }
                else if (value.isInt())
                {
                    ret = detail::make_stvar<STUInt64>(
                        field, toUnsigned<std::uint64_t>(value.asInt()));
                }
                else if (value.isUInt())
                {
                    ret = detail::make_stvar<STUInt64>(
                        field, safe_cast<std::uint64_t>(value.asUInt()));
                }
                else
                {
                    error = badType(jsonName, fieldName);
                    return ret;
                }
            }
            catch (std::exception const&)
            {
                error = invalidData(jsonName, fieldName);
                return ret;
            }

            break;

        case STI_UINT128: {
            if (!value.isString())
            {
                error = badType(jsonName, fieldName);
                return ret;
            }

            uint128 num;

            if (auto const s = value.asString(); !num.parseHex(s))
            {
                if (!s.empty())
                {
                    error = invalidData(jsonName, fieldName);
                    return ret;
                }

                num.zero();
            }

            ret = detail::make_stvar<STUInt128>(field, num);
            break;
        }

        case STI_UINT160: {
            if (!value.isString())
            {
                error = badType(jsonName, fieldName);
                return ret;
            }

            uint160 num;

            if (auto const s = value.asString(); !num.parseHex(s))
            {
                if (!s.empty())
                {
                    error = invalidData(jsonName, fieldName);
                    return ret;
                }

                num.zero();
            }

            ret = detail::make_stvar<STUInt160>(field, num);
            break;
        }

        case STI_UINT192: {
            if (!value.isString())
            {
                error = badType(jsonName, fieldName);
                return ret;
            }

            uint192 num;

            if (auto const s = value.asString(); !num.parseHex(s))
            {
                if (!s.empty())
                {
                    error = invalidData(jsonName, fieldName);
                    return ret;
                }

                num.zero();
            }

            ret = detail::make_stvar<STUInt192>(field, num);
            break;
        }

        case STI_UINT256: {
            if (!value.isString())
            {
                error = badType(jsonName, fieldName);
                return ret;
            }

            uint256 num;

            if (auto const s = value.asString(); !num.parseHex(s))
            {
                if (!s.empty())
                {
                    error = invalidData(jsonName, fieldName);
                    return ret;
                }

                num.zero();
            }

            ret = detail::make_stvar<STUInt256>(field, num);
            break;
        }

        case STI_INT32:
            try
            {
                if (value.isString())
                {
                    ret = detail::make_stvar<STInt32>(
                        field, beast::lexicalCastThrow<std::int32_t>(value.asString()));
                }
                else if (value.isInt())
                {
                    // future-proofing - a static assert failure if the JSON
                    // library ever supports larger ints
                    // In such case, we will need additional bounds checks here
                    static_assert(std::is_same_v<decltype(value.asInt()), std::int32_t>);
                    ret = detail::make_stvar<STInt32>(field, value.asInt());
                }
                else if (value.isUInt())
                {
                    auto const uintValue = value.asUInt();
                    if (uintValue >
                        static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
                    {
                        error = outOfRange(jsonName, fieldName);
                        return ret;
                    }
                    ret = detail::make_stvar<STInt32>(field, static_cast<std::int32_t>(uintValue));
                }
                else
                {
                    error = badType(jsonName, fieldName);
                    return ret;
                }
            }
            catch (std::exception const&)
            {
                error = invalidData(jsonName, fieldName);
                return ret;
            }

            break;

        case STI_VL:
            if (!value.isString())
            {
                error = badType(jsonName, fieldName);
                return ret;
            }

            try
            {
                if (auto vBlob = strUnHex(value.asString()))
                {
                    ret = detail::make_stvar<STBlob>(field, vBlob->data(), vBlob->size());
                }
                else
                {
                    Throw<std::invalid_argument>("invalid data");
                }
            }
            catch (std::exception const&)
            {
                error = invalidData(jsonName, fieldName);
                return ret;
            }

            break;

        case STI_AMOUNT:
            try
            {
                ret = detail::make_stvar<STAmount>(amountFromJson(field, value));
            }
            catch (std::exception const&)
            {
                error = invalidData(jsonName, fieldName);
                return ret;
            }

            break;

        case STI_NUMBER:
            try
            {
                ret = detail::make_stvar<STNumber>(numberFromJson(field, value));
            }
            catch (std::exception const&)
            {
                error = invalidData(jsonName, fieldName);
                return ret;
            }

            break;

        case STI_VECTOR256:
            if (!value.isArrayOrNull())
            {
                error = arrayExpected(jsonName, fieldName);
                return ret;
            }

            try
            {
                STVector256 tail(field);
                for (Json::UInt i = 0; value.isValidIndex(i); ++i)
                {
                    uint256 s;
                    if (!s.parseHex(value[i].asString()))
                        Throw<std::invalid_argument>("invalid data");
                    tail.push_back(s);
                }
                ret = detail::make_stvar<STVector256>(std::move(tail));
            }
            catch (std::exception const&)
            {
                error = invalidData(jsonName, fieldName);
                return ret;
            }

            break;

        case STI_PATHSET:
            if (!value.isArrayOrNull())
            {
                error = arrayExpected(jsonName, fieldName);
                return ret;
            }

            try
            {
                STPathSet tail(field);

                for (Json::UInt i = 0; value.isValidIndex(i); ++i)
                {
                    STPath p;

                    if (!value[i].isArrayOrNull())
                    {
                        std::stringstream ss;
                        ss << fieldName << "[" << i << "]";
                        error = arrayExpected(jsonName, ss.str());
                        return ret;
                    }

                    for (Json::UInt j = 0; value[i].isValidIndex(j); ++j)
                    {
                        std::stringstream ss;
                        ss << fieldName << "[" << i << "][" << j << "]";
                        std::string const elementName(jsonName + "." + ss.str());

                        // each element in this path has some combination of
                        // account, currency, or issuer

                        Json::Value pathEl = value[i][j];

                        if (!pathEl.isObject())
                        {
                            error = notAnObject(elementName);
                            return ret;
                        }

                        Json::Value const& account = pathEl["account"];
                        Json::Value const& currency = pathEl["currency"];
                        Json::Value const& issuer = pathEl["issuer"];
                        bool hasCurrency = false;
                        AccountID uAccount, uIssuer;
                        Currency uCurrency;

                        if (!account && !currency && !issuer)
                        {
                            error = invalidData(elementName);
                            return ret;
                        }

                        if (account)
                        {
                            // human account id
                            if (!account.isString())
                            {
                                error = stringExpected(elementName, "account");
                                return ret;
                            }

                            // If we have what looks like a 160-bit hex value,
                            // we set it, otherwise, we assume it's an AccountID
                            if (!uAccount.parseHex(account.asString()))
                            {
                                auto const a = parseBase58<AccountID>(account.asString());
                                if (!a)
                                {
                                    error = invalidData(elementName, "account");
                                    return ret;
                                }
                                uAccount = *a;
                            }
                        }

                        if (currency)
                        {
                            // human currency
                            if (!currency.isString())
                            {
                                error = stringExpected(elementName, "currency");
                                return ret;
                            }

                            hasCurrency = true;

                            if (!uCurrency.parseHex(currency.asString()))
                            {
                                if (!to_currency(uCurrency, currency.asString()))
                                {
                                    error = invalidData(elementName, "currency");
                                    return ret;
                                }
                            }
                        }

                        if (issuer)
                        {
                            // human account id
                            if (!issuer.isString())
                            {
                                error = stringExpected(elementName, "issuer");
                                return ret;
                            }

                            if (!uIssuer.parseHex(issuer.asString()))
                            {
                                auto const a = parseBase58<AccountID>(issuer.asString());
                                if (!a)
                                {
                                    error = invalidData(elementName, "issuer");
                                    return ret;
                                }
                                uIssuer = *a;
                            }
                        }

                        p.emplace_back(uAccount, uCurrency, uIssuer, hasCurrency);
                    }

                    tail.push_back(p);
                }
                ret = detail::make_stvar<STPathSet>(std::move(tail));
            }
            catch (std::exception const&)
            {
                error = invalidData(jsonName, fieldName);
                return ret;
            }

            break;

        case STI_ACCOUNT: {
            if (!value.isString())
            {
                error = badType(jsonName, fieldName);
                return ret;
            }

            std::string const strValue = value.asString();

            try
            {
                if (AccountID account; account.parseHex(strValue))
                    return detail::make_stvar<STAccount>(field, account);

                if (auto result = parseBase58<AccountID>(strValue))
                    return detail::make_stvar<STAccount>(field, *result);

                error = invalidData(jsonName, fieldName);
                return ret;
            }
            catch (std::exception const&)
            {
                error = invalidData(jsonName, fieldName);
                return ret;
            }
        }
        break;

        case STI_ISSUE:
            try
            {
                ret = detail::make_stvar<STIssue>(issueFromJson(field, value));
            }
            catch (std::exception const&)
            {
                error = invalidData(jsonName, fieldName);
                return ret;
            }
            break;

        case STI_XCHAIN_BRIDGE:
            try
            {
                ret = detail::make_stvar<STXChainBridge>(STXChainBridge(field, value));
            }
            catch (std::exception const&)
            {
                error = invalidData(jsonName, fieldName);
                return ret;
            }
            break;

        case STI_CURRENCY:
            try
            {
                ret = detail::make_stvar<STCurrency>(currencyFromJson(field, value));
            }
            catch (std::exception const&)
            {
                error = invalidData(jsonName, fieldName);
                return ret;
            }
            break;

        default:
            error = badType(jsonName, fieldName);
            return ret;
    }

    return ret;
}

static int const kMAX_DEPTH = 64;

// Forward declaration since parseObject() and parseArray() call each other.
static std::optional<detail::STVar>
parseArray(
    std::string const& jsonName,
    Json::Value const& json,
    SField const& inName,
    int depth,
    Json::Value& error);

static std::optional<STObject>
parseObject(
    std::string const& jsonName,
    Json::Value const& json,
    SField const& inName,
    int depth,
    Json::Value& error)
{
    if (!json.isObjectOrNull())
    {
        error = notAnObject(jsonName);
        return std::nullopt;
    }

    if (depth > kMAX_DEPTH)
    {
        error = tooDeep(jsonName);
        return std::nullopt;
    }

    try
    {
        STObject data(inName);

        for (auto const& fieldName : json.getMemberNames())
        {
            Json::Value const& value = json[fieldName];

            auto const& field = SField::getField(fieldName);

            if (field == sfInvalid)
            {
                error = unknownField(jsonName, fieldName);
                return std::nullopt;
            }

            switch (field.fieldType)
            {
                // Object-style containers (which recurse).
                case STI_OBJECT:
                case STI_TRANSACTION:
                case STI_LEDGERENTRY:
                case STI_VALIDATION:
                    if (!value.isObject())
                    {
                        error = notAnObject(jsonName, fieldName);
                        return std::nullopt;
                    }

                    try
                    {
                        auto ret =
                            parseObject(jsonName + "." + fieldName, value, field, depth + 1, error);
                        if (!ret)
                            return std::nullopt;
                        data.emplace_back(std::move(*ret));
                    }
                    catch (std::exception const&)
                    {
                        error = invalidData(jsonName, fieldName);
                        return std::nullopt;
                    }

                    break;

                // Array-style containers (which recurse).
                case STI_ARRAY:
                    try
                    {
                        auto array =
                            parseArray(jsonName + "." + fieldName, value, field, depth + 1, error);
                        if (!array.has_value())
                            return std::nullopt;
                        data.emplace_back(std::move(*array));
                    }
                    catch (std::exception const&)
                    {
                        error = invalidData(jsonName, fieldName);
                        return std::nullopt;
                    }

                    break;

                // Everything else (types that don't recurse).
                default: {
                    auto leaf = parseLeaf(jsonName, fieldName, &inName, value, error);

                    if (!leaf)
                        return std::nullopt;

                    data.emplace_back(std::move(*leaf));
                }

                break;
            }
        }

        // Some inner object types have templates.  Attempt to apply that.
        data.applyTemplateFromSField(inName);  // May throw

        return data;
    }
    catch (STObject::FieldErr const& e)
    {
        std::cerr << "template_mismatch: " << e.what() << "\n";
        error = templateMismatch(inName);
    }
    catch (std::exception const&)
    {
        error = invalidData(jsonName);
    }
    return std::nullopt;
}

static std::optional<detail::STVar>
parseArray(
    std::string const& jsonName,
    Json::Value const& json,
    SField const& inName,
    int depth,
    Json::Value& error)
{
    if (!json.isArrayOrNull())
    {
        error = notAnArray(jsonName);
        return std::nullopt;
    }

    if (depth > kMAX_DEPTH)
    {
        error = tooDeep(jsonName);
        return std::nullopt;
    }

    try
    {
        STArray tail(inName);

        for (Json::UInt i = 0; json.isValidIndex(i); ++i)
        {
            bool const isObjectOrNull(json[i].isObjectOrNull());
            bool const singleKey(isObjectOrNull ? json[i].size() == 1 : true);

            if (!isObjectOrNull || !singleKey)
            {
                // null values are !singleKey
                error = singletonExpected(jsonName, i);
                return std::nullopt;
            }

            // TODO: There doesn't seem to be a nice way to get just the
            // first/only key in an object without copying all keys into
            // a vector
            std::string const memberName(json[i].getMemberNames()[0]);
            auto const& nameField(SField::getField(memberName));

            if (nameField == sfInvalid)
            {
                error = unknownField(jsonName, memberName);
                return std::nullopt;
            }

            Json::Value const objectFields(json[i][memberName]);

            std::stringstream ss;
            ss << jsonName << "." << "[" << i << "]." << memberName;

            auto ret = parseObject(ss.str(), objectFields, nameField, depth + 1, error);
            if (!ret)
            {
                std::string errMsg = error["error_message"].asString();
                error["error_message"] = "Error at '" + ss.str() + "'. " + errMsg;
                return std::nullopt;
            }

            if (ret->getFName().fieldType != STI_OBJECT)
            {
                ss << "Field type: " << ret->getFName().fieldType << " ";
                error = nonObjectInArray(ss.str(), i);
                return std::nullopt;
            }

            tail.push_back(std::move(*ret));
        }

        return detail::make_stvar<STArray>(std::move(tail));
    }
    catch (std::exception const&)
    {
        error = invalidData(jsonName);
        return std::nullopt;
    }
}

}  // namespace STParsedJSONDetail

//------------------------------------------------------------------------------

STParsedJSONObject::STParsedJSONObject(std::string const& name, Json::Value const& json)
{
    using namespace STParsedJSONDetail;
    object = parseObject(name, json, sfGeneric, 0, error);
}

}  // namespace xrpl
