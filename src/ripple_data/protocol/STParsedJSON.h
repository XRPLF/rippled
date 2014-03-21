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

#ifndef RIPPLE_DATA_STPARSEDJSON_H
#define RIPPLE_DATA_STPARSEDJSON_H

namespace ripple {

/** Holds the serialized result of parsing input JSON.
    This does validation and checking on the provided JSON.
*/
class STParsedJSON
{
public:
    /** Parses and creates an STParsedJSON object.
        The result of the parsing is stored in object and error.
        Exceptions:
            Does not throw.
        @param name The name of the JSON field, used in diagnostics.
        @param json The JSON-RPC to parse.
    */
    STParsedJSON (std::string const& name,
        Json::Value const& json);

    /** The STObject if the parse was successful. */
    std::unique_ptr <STObject> object;

    /** On failure, an appropriate set of error values. */
    Json::Value error;

private:
    static std::string make_name (std::string const& object,
        std::string const& field);

    static Json::Value not_an_object (std::string const& object,
        std::string const& field = std::string());

    static Json::Value unknown_field (std::string const& object,
        std::string const& field = std::string());

    static Json::Value out_of_range (std::string const& object,
        std::string const& field = std::string());

    static Json::Value bad_type (std::string const& object,
        std::string const& field = std::string());

    static Json::Value invalid_data (std::string const& object,
        std::string const& field = std::string());

    static Json::Value array_expected (std::string const& object,
        std::string const& field = std::string());

    static Json::Value string_expected (std::string const& object,
        std::string const& field = std::string());

    static Json::Value too_deep (std::string const& object,
        std::string const& field = std::string());

    static Json::Value singleton_expected (
        std::string const& object);

    bool parse (std::string const& json_name, Json::Value const& json,
        SField::ref inName, int depth, std::unique_ptr <STObject>& sub_object);
};

} // ripple

#endif
