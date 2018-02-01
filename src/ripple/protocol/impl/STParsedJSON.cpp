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

#include <BeastConfig.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/LedgerFormats.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/STArray.h>
#include <ripple/protocol/STBitString.h>
#include <ripple/protocol/STBlob.h>
#include <ripple/protocol/STVector256.h>
#include <ripple/protocol/STInteger.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/protocol/STPathSet.h>
#include <ripple/protocol/TER.h>
#include <ripple/protocol/TxFormats.h>
#include <ripple/protocol/types.h>
#include <ripple/protocol/impl/STVar.h>
#include <ripple/beast/core/LexicalCast.h>
#include <cassert>
#include <memory>

namespace ripple {

namespace STParsedJSONDetail
{
template <typename U, typename S>
constexpr
std::enable_if_t<std::is_unsigned<U>::value && std::is_signed<S>::value, U>
to_unsigned (S value)
{
    if (value < 0 || std::numeric_limits<U>::max () < value)
        Throw<std::runtime_error> ("Value out of range");
    return static_cast<U> (value);
}

template <typename U1, typename U2>
constexpr
std::enable_if_t<std::is_unsigned<U1>::value && std::is_unsigned<U2>::value, U1>
to_unsigned (U2 value)
{
    if (std::numeric_limits<U1>::max () < value)
        Throw<std::runtime_error> ("Value out of range");
    return static_cast<U1> (value);
}

static std::string make_name (std::string const& object,
    std::string const& field)
{
    if (field.empty ())
        return object;

    return object + "." + field;
}

static Json::Value not_an_object (std::string const& object,
    std::string const& field)
{
    return RPC::make_error (rpcINVALID_PARAMS,
        "Field '" + make_name (object, field) + "' is not a JSON object.");
}

static Json::Value not_an_object (std::string const& object)
{
    return not_an_object (object, "");
}

static Json::Value not_an_array (std::string const& object)
{
    return RPC::make_error (rpcINVALID_PARAMS,
        "Field '" + object + "' is not a JSON array.");
}

static Json::Value unknown_field (std::string const& object,
    std::string const& field)
{
    return RPC::make_error (rpcINVALID_PARAMS,
        "Field '" + make_name (object, field) + "' is unknown.");
}

static Json::Value out_of_range (std::string const& object,
    std::string const& field)
{
    return RPC::make_error (rpcINVALID_PARAMS,
        "Field '" + make_name (object, field) + "' is out of range.");
}

static Json::Value bad_type (std::string const& object,
    std::string const& field)
{
    return RPC::make_error (rpcINVALID_PARAMS,
        "Field '" + make_name (object, field) + "' has bad type.");
}

static Json::Value invalid_data (std::string const& object,
    std::string const& field)
{
    return RPC::make_error (rpcINVALID_PARAMS,
        "Field '" + make_name (object, field) + "' has invalid data.");
}

static Json::Value invalid_data (std::string const& object)
{
    return invalid_data (object, "");
}

static Json::Value array_expected (std::string const& object,
    std::string const& field)
{
    return RPC::make_error (rpcINVALID_PARAMS,
        "Field '" + make_name (object, field) + "' must be a JSON array.");
}

static Json::Value string_expected (std::string const& object,
    std::string const& field)
{
    return RPC::make_error (rpcINVALID_PARAMS,
        "Field '" + make_name (object, field) + "' must be a string.");
}

static Json::Value too_deep (std::string const& object)
{
    return RPC::make_error (rpcINVALID_PARAMS,
        "Field '" + object + "' exceeds nesting depth limit.");
}

static Json::Value singleton_expected (std::string const& object,
    unsigned int index)
{
    return RPC::make_error (rpcINVALID_PARAMS,
        "Field '" + object + "[" + std::to_string (index) +
            "]' must be an object with a single key/object value.");
}

static Json::Value template_mismatch (SField const& sField)
{
    return RPC::make_error (rpcINVALID_PARAMS,
        "Object '" +  sField.getName () +
            "' contents did not meet requirements for that type.");
}

static Json::Value
non_object_in_array (std::string const& item, Json::UInt index)
{
    return RPC::make_error (rpcINVALID_PARAMS,
        "Item '" + item + "' at index " + std::to_string (index) +
            " is not an object.  Arrays may only contain objects.");
}

// This function is used by parseObject to parse any JSON type that doesn't
// recurse.  Everything represented here is a leaf-type.
static boost::optional<detail::STVar> parseLeaf (
    std::string const& json_name,
    std::string const& fieldName,
    SField const* name,
    Json::Value const& value,
    Json::Value& error)
{
    boost::optional <detail::STVar> ret;

    auto const& field = SField::getField (fieldName);

    if (field == sfInvalid)
    {
        error = unknown_field (json_name, fieldName);
        return ret;
    }

    switch (field.fieldType)
    {
    case STI_UINT8:
        try
        {
            constexpr auto minValue = std::numeric_limits<std::uint8_t>::min();
            constexpr auto maxValue = std::numeric_limits<std::uint8_t>::max();
            if (value.isString ())
            {
                std::string const strValue = value.asString();

                if (!strValue.empty() &&
                    ((strValue[0] < '0') || (strValue[0] > '9')))
                {
                    if (field == sfTransactionResult)
                    {
                        auto ter = transCode(strValue);

                        if (!ter || *ter < minValue || *ter > maxValue)
                        {
                            error = out_of_range(json_name, fieldName);
                            return ret;
                        }

                        ret = detail::make_stvar<STUInt8>(field,
                            static_cast<std::uint8_t>(*ter));
                    }
                    else
                    {
                        error = bad_type(json_name, fieldName);
                        return ret;
                    }
                }
                else
                {
                    ret = detail::make_stvar <STUInt8>(field,
                        beast::lexicalCastThrow <std::uint8_t>(strValue));
                }
            }
            else if (value.isInt ())
            {
                if (value.asInt () < minValue || value.asInt () > maxValue)
                {
                    error = out_of_range (json_name, fieldName);
                    return ret;
                }

                ret = detail::make_stvar <STUInt8> (field,
                    static_cast <std::uint8_t> (value.asInt ()));
            }
            else if (value.isUInt ())
            {
                if (value.asUInt () > maxValue)
                {
                    error = out_of_range (json_name, fieldName);
                    return ret;
                }

                ret = detail::make_stvar <STUInt8> (field,
                    static_cast <std::uint8_t> (value.asUInt ()));
            }
            else
            {
                error = bad_type (json_name, fieldName);
                return ret;
            }
        }
        catch (std::exception const&)
        {
            error = invalid_data (json_name, fieldName);
            return ret;
        }
        break;

    case STI_UINT16:
        try
        {
            if (value.isString ())
            {
                std::string const strValue = value.asString ();

                if (! strValue.empty () &&
                    ((strValue[0] < '0') || (strValue[0] > '9')))
                {
                    if (field == sfTransactionType)
                    {
                        TxType const txType (TxFormats::getInstance().
                            findTypeByName (strValue));

                        ret = detail::make_stvar <STUInt16> (field,
                            static_cast <std::uint16_t> (txType));

                        if (*name == sfGeneric)
                            name = &sfTransaction;
                    }
                    else if (field == sfLedgerEntryType)
                    {
                        LedgerEntryType const type (
                            LedgerFormats::getInstance().
                                findTypeByName (strValue));

                        ret = detail::make_stvar <STUInt16> (field,
                            static_cast <std::uint16_t> (type));

                        if (*name == sfGeneric)
                            name = &sfLedgerEntry;
                    }
                    else
                    {
                        error = invalid_data (json_name, fieldName);
                        return ret;
                    }
                }
                else
                {
                    ret = detail::make_stvar <STUInt16> (field,
                        beast::lexicalCastThrow <std::uint16_t> (strValue));
                }
            }
            else if (value.isInt ())
            {
                ret = detail::make_stvar <STUInt16> (field,
                    to_unsigned <std::uint16_t> (value.asInt ()));
            }
            else if (value.isUInt ())
            {
                ret = detail::make_stvar <STUInt16> (field,
                    to_unsigned <std::uint16_t> (value.asUInt ()));
            }
            else
            {
                error = bad_type (json_name, fieldName);
                return ret;
            }
        }
        catch (std::exception const&)
        {
            error = invalid_data (json_name, fieldName);
            return ret;
        }

        break;

    case STI_UINT32:
        try
        {
            if (value.isString ())
            {
                ret = detail::make_stvar <STUInt32> (field,
                    beast::lexicalCastThrow <std::uint32_t> (
                        value.asString ()));
            }
            else if (value.isInt ())
            {
                ret = detail::make_stvar <STUInt32> (field,
                    to_unsigned <std::uint32_t> (value.asInt ()));
            }
            else if (value.isUInt ())
            {
                ret = detail::make_stvar <STUInt32> (field,
                    static_cast <std::uint32_t> (value.asUInt ()));
            }
            else
            {
                error = bad_type (json_name, fieldName);
                return ret;
            }
        }
        catch (std::exception const&)
        {
            error = invalid_data (json_name, fieldName);
            return ret;
        }

        break;

    case STI_UINT64:
        try
        {
            if (value.isString ())
            {
                ret = detail::make_stvar <STUInt64> (field,
                    uintFromHex (value.asString ()));
            }
            else if (value.isInt ())
            {
                ret = detail::make_stvar <STUInt64> (field,
                    to_unsigned<std::uint64_t> (value.asInt ()));
            }
            else if (value.isUInt ())
            {
                ret = detail::make_stvar <STUInt64> (field,
                    static_cast <std::uint64_t> (value.asUInt ()));
            }
            else
            {
                error = bad_type (json_name, fieldName);
                return ret;
            }
        }
        catch (std::exception const&)
        {
            error = invalid_data (json_name, fieldName);
            return ret;
        }

        break;

    case STI_HASH128:
        try
        {
            if (value.isString ())
            {
                ret = detail::make_stvar <STHash128> (field, value.asString ());
            }
            else
            {
                error = bad_type (json_name, fieldName);
                return ret;
            }
        }
        catch (std::exception const&)
        {
            error = invalid_data (json_name, fieldName);
            return ret;
        }

        break;

    case STI_HASH160:
        try
        {
            if (value.isString ())
            {
                ret = detail::make_stvar <STHash160> (field, value.asString ());
            }
            else
            {
                error = bad_type (json_name, fieldName);
                return ret;
            }
        }
        catch (std::exception const&)
        {
            error = invalid_data (json_name, fieldName);
            return ret;
        }

        break;

    case STI_HASH256:
        try
        {
            if (value.isString ())
            {
                ret = detail::make_stvar <STHash256> (field, value.asString ());
            }
            else
            {
                error = bad_type (json_name, fieldName);
                return ret;
            }
        }
        catch (std::exception const&)
        {
            error = invalid_data (json_name, fieldName);
            return ret;
        }

        break;

    case STI_VL:
        if (! value.isString ())
        {
            error = bad_type (json_name, fieldName);
            return ret;
        }

        try
        {
            std::pair<Blob, bool> vBlob (strUnHex (value.asString ()));

            if (! vBlob.second)
                Throw<std::invalid_argument> ("invalid data");

            ret = detail::make_stvar <STBlob> (field, vBlob.first.data (),
                                             vBlob.first.size ());
        }
        catch (std::exception const&)
        {
            error = invalid_data (json_name, fieldName);
            return ret;
        }

        break;

    case STI_AMOUNT:
        try
        {
            ret = detail::make_stvar <STAmount> (amountFromJson (field, value));
        }
        catch (std::exception const&)
        {
            error = invalid_data (json_name, fieldName);
            return ret;
        }

        break;

    case STI_VECTOR256:
        if (! value.isArrayOrNull ())
        {
            error = array_expected (json_name, fieldName);
            return ret;
        }

        try
        {
            STVector256 tail (field);
            for (Json::UInt i = 0; value.isValidIndex (i); ++i)
            {
                uint256 s;
                s.SetHex (value[i].asString ());
                tail.push_back (s);
            }
            ret = detail::make_stvar <STVector256> (std::move (tail));
        }
        catch (std::exception const&)
        {
            error = invalid_data (json_name, fieldName);
            return ret;
        }

        break;

    case STI_PATHSET:
        if (!value.isArrayOrNull ())
        {
            error = array_expected (json_name, fieldName);
            return ret;
        }

        try
        {
            STPathSet tail (field);

            for (Json::UInt i = 0; value.isValidIndex (i); ++i)
            {
                STPath p;

                if (!value[i].isArrayOrNull ())
                {
                    std::stringstream ss;
                    ss << fieldName << "[" << i << "]";
                    error = array_expected (json_name, ss.str ());
                    return ret;
                }

                for (Json::UInt j = 0; value[i].isValidIndex (j); ++j)
                {
                    std::stringstream ss;
                    ss << fieldName << "[" << i << "][" << j << "]";
                    std::string const element_name (
                        json_name + "." + ss.str());

                    // each element in this path has some combination of
                    // account, currency, or issuer

                    Json::Value pathEl = value[i][j];

                    if (!pathEl.isObject())
                    {
                        error = not_an_object (element_name);
                        return ret;
                    }

                    Json::Value const& account  = pathEl["account"];
                    Json::Value const& currency = pathEl["currency"];
                    Json::Value const& issuer   = pathEl["issuer"];
                    bool hasCurrency            = false;
                    AccountID uAccount, uIssuer;
                    Currency uCurrency;

                    if (account)
                    {
                        // human account id
                        if (! account.isString ())
                        {
                            error = string_expected (element_name, "account");
                            return ret;
                        }

                        // If we have what looks like a 160-bit hex value, we
                        // set it, otherwise, we assume it's an AccountID
                        if (!uAccount.SetHexExact (account.asString ()))
                        {
                            auto const a = parseBase58<AccountID>(
                                account.asString());
                            if (! a)
                            {
                                error = invalid_data (element_name, "account");
                                return ret;
                            }
                            uAccount = *a;
                        }
                    }

                    if (currency)
                    {
                        // human currency
                        if (!currency.isString ())
                        {
                            error = string_expected (element_name, "currency");
                            return ret;
                        }

                        hasCurrency = true;

                        if (!uCurrency.SetHexExact (currency.asString ()))
                        {
                            if (!to_currency (uCurrency, currency.asString ()))
                            {
                                error = invalid_data (element_name, "currency");
                                return ret;
                            }
                        }
                    }

                    if (issuer)
                    {
                        // human account id
                        if (!issuer.isString ())
                        {
                            error = string_expected (element_name, "issuer");
                            return ret;
                        }

                        if (!uIssuer.SetHexExact (issuer.asString ()))
                        {
                            auto const a = parseBase58<AccountID>(
                                issuer.asString());
                            if (! a)
                            {
                                error = invalid_data (element_name, "issuer");
                                return ret;
                            }
                            uIssuer = *a;
                        }
                    }

                    p.emplace_back (uAccount, uCurrency, uIssuer, hasCurrency);
                }

                tail.push_back (p);
            }
            ret = detail::make_stvar <STPathSet> (std::move (tail));
        }
        catch (std::exception const&)
        {
            error = invalid_data (json_name, fieldName);
            return ret;
        }

        break;

    case STI_ACCOUNT:
        {
            if (! value.isString ())
            {
                error = bad_type (json_name, fieldName);
                return ret;
            }

            std::string const strValue = value.asString ();

            try
            {
                // VFALCO This needs careful auditing
                auto const account =
                    parseHexOrBase58<AccountID>(
                        strValue);
                if (! account)
                {
                    error = invalid_data (json_name, fieldName);
                    return ret;
                }
                ret = detail::make_stvar <STAccount>(
                    field, *account);
            }
            catch (std::exception const&)
            {
                error = invalid_data (json_name, fieldName);
                return ret;
            }
        }
        break;

    default:
        error = bad_type (json_name, fieldName);
        return ret;
    }

    return ret;
}

static const int maxDepth = 64;

// Forward declaration since parseObject() and parseArray() call each other.
static boost::optional <detail::STVar> parseArray (
    std::string const& json_name,
    Json::Value const& json,
    SField const& inName,
    int depth,
    Json::Value& error);



static boost::optional <STObject> parseObject (
    std::string const& json_name,
    Json::Value const& json,
    SField const& inName,
    int depth,
    Json::Value& error)
{
    if (! json.isObjectOrNull ())
    {
        error = not_an_object (json_name);
        return boost::none;
    }

    if (depth > maxDepth)
    {
        error = too_deep (json_name);
        return boost::none;
    }

    STObject data (inName);

    for (auto const& fieldName : json.getMemberNames ())
    {
        Json::Value const& value = json [fieldName];

        auto const& field = SField::getField (fieldName);

        if (field == sfInvalid)
        {
            error = unknown_field (json_name, fieldName);
            return boost::none;
        }

        switch (field.fieldType)
        {

        // Object-style containers (which recurse).
        case STI_OBJECT:
        case STI_TRANSACTION:
        case STI_LEDGERENTRY:
        case STI_VALIDATION:
            if (! value.isObjectOrNull ())
            {
                error = not_an_object (json_name, fieldName);
                return boost::none;
            }

            try
            {
                auto ret = parseObject (json_name + "." + fieldName,
                    value, field, depth + 1, error);
                if (! ret)
                    return boost::none;
                data.emplace_back (std::move (*ret));
            }
            catch (std::exception const&)
            {
                error = invalid_data (json_name, fieldName);
                return boost::none;
            }

            break;

        // Array-style containers (which recurse).
        case STI_ARRAY:
            try
            {
                auto array = parseArray (json_name + "." + fieldName,
                    value, field, depth + 1, error);
                if (array == boost::none)
                    return boost::none;
                data.emplace_back (std::move (*array));
            }
            catch (std::exception const&)
            {
                error = invalid_data (json_name, fieldName);
                return boost::none;
            }

            break;

        // Everything else (types that don't recurse).
        default:
            {
                auto leaf =
                    parseLeaf (json_name, fieldName, &inName, value, error);

                if (!leaf)
                    return boost::none;

                data.emplace_back (std::move (*leaf));
            }

            break;
        }
    }

    // Some inner object types have templates.  Attempt to apply that.
    if (data.setTypeFromSField (inName) == STObject::typeSetFail)
    {
        error = template_mismatch (inName);
        return boost::none;
    }

    return std::move (data);
}

static boost::optional <detail::STVar> parseArray (
    std::string const& json_name,
    Json::Value const& json,
    SField const& inName,
    int depth,
    Json::Value& error)
{
    if (! json.isArrayOrNull ())
    {
        error = not_an_array (json_name);
        return boost::none;
    }

    if (depth > maxDepth)
    {
        error = too_deep (json_name);
        return boost::none;
    }

    try
    {
        STArray tail (inName);

        for (Json::UInt i = 0; json.isValidIndex (i); ++i)
        {
            bool const isObjectOrNull (json[i].isObjectOrNull());
            bool const singleKey (isObjectOrNull ? json[i].size() == 1 : true);

            if (!isObjectOrNull || !singleKey)
            {
                // null values are !singleKey
                error = singleton_expected (json_name, i);
                return boost::none;
            }

            // TODO: There doesn't seem to be a nice way to get just the
            // first/only key in an object without copying all keys into
            // a vector
            std::string const objectName (json[i].getMemberNames()[0]);;
            auto const&       nameField (SField::getField(objectName));

            if (nameField == sfInvalid)
            {
                error = unknown_field (json_name, objectName);
                return boost::none;
            }

            Json::Value const objectFields (json[i][objectName]);

            std::stringstream ss;
            ss << json_name << "." <<
                "[" << i << "]." << objectName;

            auto ret = parseObject (ss.str (), objectFields,
                nameField, depth + 1, error);
            if (! ret)
            {
                    std::string errMsg = error["error_message"].asString ();
                    error["error_message"] = "Error at '" + ss.str () +
                        "'. " + errMsg;
                    return boost::none;
            }

            if (ret->getFName().fieldType != STI_OBJECT)
            {
                error = non_object_in_array (ss.str(), i);
                return boost::none;
            }

            tail.push_back (std::move (*ret));
        }

        return detail::make_stvar <STArray> (std::move (tail));
    }
    catch (std::exception const&)
    {
        error = invalid_data (json_name);
        return boost::none;
    }
}

} // STParsedJSONDetail

//------------------------------------------------------------------------------

STParsedJSONObject::STParsedJSONObject (
    std::string const& name,
    Json::Value const& json)
{
    using namespace STParsedJSONDetail;
    object = parseObject (name, json, sfGeneric, 0, error);
}

//------------------------------------------------------------------------------

STParsedJSONArray::STParsedJSONArray (
    std::string const& name,
    Json::Value const& json)
{
    using namespace STParsedJSONDetail;
    auto arr = parseArray (name, json, sfGeneric, 0, error);
    if (!arr)
        array = boost::none;
    else
    {
        auto p = dynamic_cast <STArray*> (&arr->get());
        if (p == nullptr)
            array = boost::none;
        else
            array = std::move (*p);
    }
}



} // ripple
