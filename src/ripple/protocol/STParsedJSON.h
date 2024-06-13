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

#ifndef RIPPLE_PROTOCOL_STPARSEDJSON_H_INCLUDED
#define RIPPLE_PROTOCOL_STPARSEDJSON_H_INCLUDED

#include <ripple/plugin/plugin.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/STArray.h>
#include <optional>

namespace ripple {

/** Holds the serialized result of parsing an input JSON object.
    This does validation and checking on the provided JSON.
*/
class STParsedJSONObject
{
public:
    /** Parses and creates an STParsedJSON object.
        The result of the parsing is stored in object and error.
        Exceptions:
            Does not throw.
        @param name The name of the JSON field, used in diagnostics.
        @param json The JSON-RPC to parse.
    */
    STParsedJSONObject(std::string const& name, Json::Value const& json);

    STParsedJSONObject() = delete;
    STParsedJSONObject(STParsedJSONObject const&) = delete;
    STParsedJSONObject&
    operator=(STParsedJSONObject const&) = delete;
    ~STParsedJSONObject() = default;

    /** The STObject if the parse was successful. */
    std::optional<STObject> object;

    /** On failure, an appropriate set of error values. */
    Json::Value error;
};

/** Holds the serialized result of parsing an input JSON array.
    This does validation and checking on the provided JSON.
*/
class STParsedJSONArray
{
public:
    /** Parses and creates an STParsedJSON array.
        The result of the parsing is stored in array and error.
        Exceptions:
            Does not throw.
        @param name The name of the JSON field, used in diagnostics.
        @param json The JSON-RPC to parse.
    */
    STParsedJSONArray(std::string const& name, Json::Value const& json);

    STParsedJSONArray() = delete;
    STParsedJSONArray(STParsedJSONArray const&) = delete;
    STParsedJSONArray&
    operator=(STParsedJSONArray const&) = delete;
    ~STParsedJSONArray() = default;

    /** The STArray if the parse was successful. */
    std::optional<STArray> array;

    /** On failure, an appropriate set of error values. */
    Json::Value error;
};

template <typename U, typename S>
constexpr std::
    enable_if_t<std::is_unsigned<U>::value && std::is_signed<S>::value, U>
    to_unsigned(S value)
{
    if (value < 0 || std::numeric_limits<U>::max() < value)
        Throw<std::runtime_error>("Value out of range");
    return static_cast<U>(value);
}

template <typename U1, typename U2>
constexpr std::
    enable_if_t<std::is_unsigned<U1>::value && std::is_unsigned<U2>::value, U1>
    to_unsigned(U2 value)
{
    if (std::numeric_limits<U1>::max() < value)
        Throw<std::runtime_error>("Value out of range");
    return static_cast<U1>(value);
}

inline std::string
make_name(std::string const& object, std::string const& field)
{
    if (field.empty())
        return object;

    return object + "." + field;
}

inline Json::Value
not_an_object(std::string const& object, std::string const& field)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + make_name(object, field) + "' is not a JSON object.");
}

inline Json::Value
not_an_object(std::string const& object)
{
    return not_an_object(object, "");
}

inline Json::Value
not_an_array(std::string const& object)
{
    return RPC::make_error(
        rpcINVALID_PARAMS, "Field '" + object + "' is not a JSON array.");
}

inline Json::Value
unknown_field(std::string const& object, std::string const& field)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + make_name(object, field) + "' is unknown.");
}

inline Json::Value
out_of_range(std::string const& object, std::string const& field)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + make_name(object, field) + "' is out of range.");
}

inline Json::Value
bad_type(std::string const& object, std::string const& field)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + make_name(object, field) + "' has bad type.");
}

inline Json::Value
unknown_type(std::string const& object, std::string const& field, int fieldType)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + make_name(object, field) + "' has unknown type value " +
            std::to_string(fieldType) + ".");
}

inline Json::Value
invalid_data(std::string const& object, std::string const& field)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + make_name(object, field) + "' has invalid data.");
}

inline Json::Value
invalid_data(std::string const& object)
{
    return invalid_data(object, "");
}

inline Json::Value
array_expected(std::string const& object, std::string const& field)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + make_name(object, field) + "' must be a JSON array.");
}

inline Json::Value
string_expected(std::string const& object, std::string const& field)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + make_name(object, field) + "' must be a string.");
}

inline Json::Value
too_deep(std::string const& object)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + object + "' exceeds nesting depth limit.");
}

inline Json::Value
singleton_expected(std::string const& object, unsigned int index)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + object + "[" + std::to_string(index) +
            "]' must be an object with a single key/object value.");
}

inline Json::Value
template_mismatch(SField const& sField)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Object '" + sField.getName() +
            "' contents did not meet requirements for that type.");
}

inline Json::Value
non_object_in_array(std::string const& item, Json::UInt index)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Item '" + item + "' at index " + std::to_string(index) +
            " is not an object.  Arrays may only contain objects.");
}

template <typename T>
std::optional<detail::STVar>
parseLeafType(
    SField const& field,
    std::string const& json_name,
    std::string const& fieldName,
    SField const* name,
    Json::Value const& value,
    Json::Value& error);

typedef std::optional<detail::STVar> (*parseLeafTypePtr)(
    SField const& field,
    std::string const& json_name,
    std::string const& fieldName,
    SField const* name,
    Json::Value const& value,
    Json::Value& error);

void
registerLeafTypes(std::map<int, parsePluginValuePtr>* pluginLeafParserMap);

}  // namespace ripple

#endif
