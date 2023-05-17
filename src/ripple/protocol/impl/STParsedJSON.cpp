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

#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/safe_cast.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/protocol/LedgerFormats.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/STArray.h>
#include <ripple/protocol/STBitString.h>
#include <ripple/protocol/STBlob.h>
#include <ripple/protocol/STInteger.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/protocol/STVector256.h>
#include <ripple/protocol/TER.h>
#include <ripple/protocol/TxFormats.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/protocol/impl/STVar.h>
#include <cassert>
#include <charconv>
#include <memory>

namespace ripple {

namespace STParsedJSONDetail {

template <typename T>
std::optional<detail::STVar>
parseLeafType(
    SField const& field,
    std::string const& json_name,
    std::string const& fieldName,
    SField const* name,
    Json::Value const& value,
    Json::Value& error);

template <>
std::optional<detail::STVar>
parseLeafType<STUInt8>(
    SField const& field,
    std::string const& json_name,
    std::string const& fieldName,
    SField const* name,
    Json::Value const& value,
    Json::Value& error)
{
    std::optional<detail::STVar> ret;
    try
    {
        constexpr auto minValue = std::numeric_limits<std::uint8_t>::min();
        constexpr auto maxValue = std::numeric_limits<std::uint8_t>::max();
        if (value.isString())
        {
            std::string const strValue = value.asString();

            if (!strValue.empty() &&
                ((strValue[0] < '0') || (strValue[0] > '9')))
            {
                if (field == sfTransactionResult)
                {
                    auto ter = transCode(strValue);

                    if (!ter || TERtoInt(*ter) < minValue ||
                        TERtoInt(*ter) > maxValue)
                    {
                        error = out_of_range(json_name, fieldName);
                        return ret;
                    }

                    ret = detail::make_stvar<STUInt8>(
                        field, static_cast<std::uint8_t>(TERtoInt(*ter)));
                }
                else
                {
                    error = bad_type(json_name, fieldName);
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
            if (value.asInt() < minValue || value.asInt() > maxValue)
            {
                error = out_of_range(json_name, fieldName);
                return ret;
            }

            ret = detail::make_stvar<STUInt8>(
                field, static_cast<std::uint8_t>(value.asInt()));
        }
        else if (value.isUInt())
        {
            if (value.asUInt() > maxValue)
            {
                error = out_of_range(json_name, fieldName);
                return ret;
            }

            ret = detail::make_stvar<STUInt8>(
                field, static_cast<std::uint8_t>(value.asUInt()));
        }
        else
        {
            error = bad_type(json_name, fieldName);
            return ret;
        }
        return ret;
    }
    catch (std::exception const&)
    {
        error = invalid_data(json_name, fieldName);
        return ret;
    }
}

template <>
std::optional<detail::STVar>
parseLeafType<STUInt16>(
    SField const& field,
    std::string const& json_name,
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

            if (!strValue.empty() &&
                ((strValue[0] < '0') || (strValue[0] > '9')))
            {
                if (field == sfTransactionType)
                {
                    ret = detail::make_stvar<STUInt16>(
                        field,
                        static_cast<std::uint16_t>(
                            TxFormats::getInstance().findTypeByName(strValue)));

                    if (*name == sfGeneric)
                        name = &sfTransaction;
                }
                else if (field == sfLedgerEntryType)
                {
                    ret = detail::make_stvar<STUInt16>(
                        field,
                        static_cast<std::uint16_t>(
                            LedgerFormats::getInstance().findTypeByName(
                                strValue)));

                    if (*name == sfGeneric)
                        name = &sfLedgerEntry;
                }
                else
                {
                    error = invalid_data(json_name, fieldName);
                    return ret;
                }
            }
            else
            {
                ret = detail::make_stvar<STUInt16>(
                    field, beast::lexicalCastThrow<std::uint16_t>(strValue));
            }
        }
        else if (value.isInt())
        {
            ret = detail::make_stvar<STUInt16>(
                field, to_unsigned<std::uint16_t>(value.asInt()));
        }
        else if (value.isUInt())
        {
            ret = detail::make_stvar<STUInt16>(
                field, to_unsigned<std::uint16_t>(value.asUInt()));
        }
        else
        {
            error = bad_type(json_name, fieldName);
            return ret;
        }
        return ret;
    }
    catch (std::exception const&)
    {
        error = invalid_data(json_name, fieldName);
        return ret;
    }
}

template <>
std::optional<detail::STVar>
parseLeafType<STUInt32>(
    SField const& field,
    std::string const& json_name,
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
            ret = detail::make_stvar<STUInt32>(
                field,
                beast::lexicalCastThrow<std::uint32_t>(value.asString()));
        }
        else if (value.isInt())
        {
            ret = detail::make_stvar<STUInt32>(
                field, to_unsigned<std::uint32_t>(value.asInt()));
        }
        else if (value.isUInt())
        {
            ret = detail::make_stvar<STUInt32>(
                field, safe_cast<std::uint32_t>(value.asUInt()));
        }
        else
        {
            error = bad_type(json_name, fieldName);
            return ret;
        }
        return ret;
    }
    catch (std::exception const&)
    {
        error = invalid_data(json_name, fieldName);
        return ret;
    }
}

template <>
std::optional<detail::STVar>
parseLeafType<STUInt64>(
    SField const& field,
    std::string const& json_name,
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
            auto const str = value.asString();

            std::uint64_t val;

            auto [p, ec] =
                std::from_chars(str.data(), str.data() + str.size(), val, 16);

            if (ec != std::errc() || (p != str.data() + str.size()))
                Throw<std::invalid_argument>("invalid data");

            ret = detail::make_stvar<STUInt64>(field, val);
        }
        else if (value.isInt())
        {
            ret = detail::make_stvar<STUInt64>(
                field, to_unsigned<std::uint64_t>(value.asInt()));
        }
        else if (value.isUInt())
        {
            ret = detail::make_stvar<STUInt64>(
                field, safe_cast<std::uint64_t>(value.asUInt()));
        }
        else
        {
            error = bad_type(json_name, fieldName);
            return ret;
        }
        return ret;
    }
    catch (std::exception const&)
    {
        error = invalid_data(json_name, fieldName);
        return ret;
    }
}

template <>
std::optional<detail::STVar>
parseLeafType<STUInt128>(
    SField const& field,
    std::string const& json_name,
    std::string const& fieldName,
    SField const* name,
    Json::Value const& value,
    Json::Value& error)
{
    std::optional<detail::STVar> ret;
    if (!value.isString())
    {
        error = bad_type(json_name, fieldName);
        return ret;
    }

    uint128 num;

    if (auto const s = value.asString(); !num.parseHex(s))
    {
        if (!s.empty())
        {
            error = invalid_data(json_name, fieldName);
            return ret;
        }

        num.zero();
    }

    ret = detail::make_stvar<STUInt128>(field, num);
    return ret;
}

template <>
std::optional<detail::STVar>
parseLeafType<STUInt160>(
    SField const& field,
    std::string const& json_name,
    std::string const& fieldName,
    SField const* name,
    Json::Value const& value,
    Json::Value& error)
{
    std::optional<detail::STVar> ret;
    if (!value.isString())
    {
        error = bad_type(json_name, fieldName);
        return ret;
    }

    uint160 num;

    if (auto const s = value.asString(); !num.parseHex(s))
    {
        if (!s.empty())
        {
            error = invalid_data(json_name, fieldName);
            return ret;
        }

        num.zero();
    }

    ret = detail::make_stvar<STUInt160>(field, num);
    return ret;
}

template <>
std::optional<detail::STVar>
parseLeafType<STUInt256>(
    SField const& field,
    std::string const& json_name,
    std::string const& fieldName,
    SField const* name,
    Json::Value const& value,
    Json::Value& error)
{
    std::optional<detail::STVar> ret;
    if (!value.isString())
    {
        error = bad_type(json_name, fieldName);
        return ret;
    }

    uint256 num;

    if (auto const s = value.asString(); !num.parseHex(s))
    {
        if (!s.empty())
        {
            error = invalid_data(json_name, fieldName);
            return ret;
        }

        num.zero();
    }

    ret = detail::make_stvar<STUInt256>(field, num);
    return ret;
}

template <>
std::optional<detail::STVar>
parseLeafType<STBlob>(
    SField const& field,
    std::string const& json_name,
    std::string const& fieldName,
    SField const* name,
    Json::Value const& value,
    Json::Value& error)
{
    std::optional<detail::STVar> ret;
    if (!value.isString())
    {
        error = bad_type(json_name, fieldName);
        return ret;
    }

    try
    {
        if (auto vBlob = strUnHex(value.asString()))
        {
            ret =
                detail::make_stvar<STBlob>(field, vBlob->data(), vBlob->size());
        }
        else
        {
            Throw<std::invalid_argument>("invalid data");
        }
        return ret;
    }
    catch (std::exception const&)
    {
        error = invalid_data(json_name, fieldName);
        return ret;
    }
}

template <>
std::optional<detail::STVar>
parseLeafType<STAmount>(
    SField const& field,
    std::string const& json_name,
    std::string const& fieldName,
    SField const* name,
    Json::Value const& value,
    Json::Value& error)
{
    std::optional<detail::STVar> ret;
    try
    {
        ret = detail::make_stvar<STAmount>(amountFromJson(field, value));
        return ret;
    }
    catch (std::exception const&)
    {
        error = invalid_data(json_name, fieldName);
        return ret;
    }
}

template <>
std::optional<detail::STVar>
parseLeafType<STVector256>(
    SField const& field,
    std::string const& json_name,
    std::string const& fieldName,
    SField const* name,
    Json::Value const& value,
    Json::Value& error)
{
    std::optional<detail::STVar> ret;
    if (!value.isArrayOrNull())
    {
        error = array_expected(json_name, fieldName);
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
        return ret;
    }
    catch (std::exception const&)
    {
        error = invalid_data(json_name, fieldName);
        return ret;
    }
}

template <>
std::optional<detail::STVar>
parseLeafType<STAccount>(
    SField const& field,
    std::string const& json_name,
    std::string const& fieldName,
    SField const* name,
    Json::Value const& value,
    Json::Value& error)
{
    std::optional<detail::STVar> ret;
    if (!value.isString())
    {
        error = bad_type(json_name, fieldName);
        return ret;
    }

    std::string const strValue = value.asString();

    try
    {
        if (AccountID account; account.parseHex(strValue))
            return detail::make_stvar<STAccount>(field, account);

        if (auto result = parseBase58<AccountID>(strValue))
            return detail::make_stvar<STAccount>(field, *result);

        error = invalid_data(json_name, fieldName);
        return ret;
    }
    catch (std::exception const&)
    {
        error = invalid_data(json_name, fieldName);
        return ret;
    }
}

std::map<int, parseLeafTypePtr2> leafParserMap{
    {STI_UINT8, parseLeafType<STUInt8>},
    {STI_UINT16, parseLeafType<STUInt16>},
    {STI_UINT32, parseLeafType<STUInt32>},
    {STI_UINT64, parseLeafType<STUInt64>},
    {STI_UINT128, parseLeafType<STUInt128>},
    {STI_UINT160, parseLeafType<STUInt160>},
    {STI_UINT256, parseLeafType<STUInt256>},
    {STI_VL, parseLeafType<STBlob>},
    {STI_AMOUNT, parseLeafType<STAmount>},
    {STI_VECTOR256, parseLeafType<STVector256>},
    {STI_ACCOUNT, parseLeafType<STAccount>},
};

std::map<int, parseLeafTypePtr> pluginLeafParserMap{};

// This function is used by parseObject to parse any JSON type that doesn't
// recurse.  Everything represented here is a leaf-type.
std::optional<detail::STVar>
parseLeaf(
    std::string const& json_name,
    std::string const& fieldName,
    SField const* name,
    Json::Value const& value,
    Json::Value& error)
{
    auto const& field = SField::getField(fieldName);

    if (field == sfInvalid)
    {
        std::optional<detail::STVar> ret;
        error = unknown_field(json_name, fieldName);
        return ret;
    }

    if (auto it = leafParserMap.find(field.fieldType);
        it != leafParserMap.end())
    {
        return it->second(field, json_name, fieldName, name, value, error);
    }

    if (auto it = pluginLeafParserMap.find(field.fieldType);
        it != pluginLeafParserMap.end())
    {
        const std::optional<detail::STVar>* ret =
            it->second(field, json_name, fieldName, name, value, error);
        return *ret;
    }

    std::optional<detail::STVar> ret;
    error = unknown_type(json_name, fieldName, field.fieldType);
    return ret;
}

static const int maxDepth = 64;

// Forward declaration since parseObject() and parseArray() call each other.
std::optional<detail::STVar>
parseArray(
    std::string const& json_name,
    Json::Value const& json,
    SField const& inName,
    int depth,
    Json::Value& error);

static std::optional<STObject>
parseObject(
    std::string const& json_name,
    Json::Value const& json,
    SField const& inName,
    int depth,
    Json::Value& error)
{
    if (!json.isObjectOrNull())
    {
        error = not_an_object(json_name);
        return std::nullopt;
    }

    if (depth > maxDepth)
    {
        error = too_deep(json_name);
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
                error = unknown_field(json_name, fieldName);
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
                        error = not_an_object(json_name, fieldName);
                        return std::nullopt;
                    }

                    try
                    {
                        auto ret = parseObject(
                            json_name + "." + fieldName,
                            value,
                            field,
                            depth + 1,
                            error);
                        if (!ret)
                            return std::nullopt;
                        data.emplace_back(std::move(*ret));
                    }
                    catch (std::exception const&)
                    {
                        error = invalid_data(json_name, fieldName);
                        return std::nullopt;
                    }

                    break;

                // Array-style containers (which recurse).
                case STI_ARRAY:
                    try
                    {
                        auto array = parseArray(
                            json_name + "." + fieldName,
                            value,
                            field,
                            depth + 1,
                            error);
                        if (!array.has_value())
                            return std::nullopt;
                        data.emplace_back(std::move(*array));
                    }
                    catch (std::exception const&)
                    {
                        error = invalid_data(json_name, fieldName);
                        return std::nullopt;
                    }

                    break;

                // Everything else (types that don't recurse).
                default: {
                    auto leaf =
                        parseLeaf(json_name, fieldName, &inName, value, error);

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
        error = template_mismatch(inName);
    }
    catch (std::exception const&)
    {
        error = invalid_data(json_name);
    }
    return std::nullopt;
}

std::optional<detail::STVar>
parseArray(
    std::string const& json_name,
    Json::Value const& json,
    SField const& inName,
    int depth,
    Json::Value& error)
{
    if (!json.isArrayOrNull())
    {
        error = not_an_array(json_name);
        return std::nullopt;
    }

    if (depth > maxDepth)
    {
        error = too_deep(json_name);
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
                error = singleton_expected(json_name, i);
                return std::nullopt;
            }

            // TODO: There doesn't seem to be a nice way to get just the
            // first/only key in an object without copying all keys into
            // a vector
            std::string const objectName(json[i].getMemberNames()[0]);
            ;
            auto const& nameField(SField::getField(objectName));

            if (nameField == sfInvalid)
            {
                error = unknown_field(json_name, objectName);
                return std::nullopt;
            }

            Json::Value const objectFields(json[i][objectName]);

            std::stringstream ss;
            ss << json_name << "."
               << "[" << i << "]." << objectName;

            auto ret = parseObject(
                ss.str(), objectFields, nameField, depth + 1, error);
            if (!ret)
            {
                std::string errMsg = error["error_message"].asString();
                error["error_message"] =
                    "Error at '" + ss.str() + "'. " + errMsg;
                return std::nullopt;
            }

            if (ret->getFName().fieldType != STI_OBJECT)
            {
                error = non_object_in_array(ss.str(), i);
                return std::nullopt;
            }

            tail.push_back(std::move(*ret));
        }

        return detail::make_stvar<STArray>(std::move(tail));
    }
    catch (std::exception const&)
    {
        error = invalid_data(json_name);
        return std::nullopt;
    }
}

}  // namespace STParsedJSONDetail

//------------------------------------------------------------------------------

STParsedJSONObject::STParsedJSONObject(
    std::string const& name,
    Json::Value const& json)
{
    using namespace STParsedJSONDetail;
    object = parseObject(name, json, sfGeneric, 0, error);
}

//------------------------------------------------------------------------------

STParsedJSONArray::STParsedJSONArray(
    std::string const& name,
    Json::Value const& json)
{
    using namespace STParsedJSONDetail;
    auto arr = parseArray(name, json, sfGeneric, 0, error);
    if (!arr)
        array.reset();
    else
    {
        auto p = dynamic_cast<STArray*>(&arr->get());
        if (p == nullptr)
            array.reset();
        else
            array = std::move(*p);
    }
}

void
registerLeafType(int type, parseLeafTypePtr functionPtr)
{
    STParsedJSONDetail::pluginLeafParserMap.insert({type, functionPtr});
}

}  // namespace ripple
