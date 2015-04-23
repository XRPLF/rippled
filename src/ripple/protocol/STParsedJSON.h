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

#include <ripple/protocol/STArray.h>
#include <boost/optional.hpp>

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
    STParsedJSONObject (std::string const& name, Json::Value const& json);

    STParsedJSONObject () = delete;
    STParsedJSONObject (STParsedJSONObject const&) = delete;
    STParsedJSONObject& operator= (STParsedJSONObject const&) = delete;
    ~STParsedJSONObject () = default;

    /** The STObject if the parse was successful. */
    boost::optional <STObject> object;

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
    STParsedJSONArray (std::string const& name, Json::Value const& json);

    STParsedJSONArray () = delete;
    STParsedJSONArray (STParsedJSONArray const&) = delete;
    STParsedJSONArray& operator= (STParsedJSONArray const&) = delete;
    ~STParsedJSONArray () = default;

    /** The STArray if the parse was successful. */
    boost::optional <STArray> array;

    /** On failure, an appropriate set of error values. */
    Json::Value error;
};



} // ripple

#endif
