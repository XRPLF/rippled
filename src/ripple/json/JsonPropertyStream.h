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

#ifndef RIPPLE_JSON_JSONPROPERTYSTREAM_H_INCLUDED
#define RIPPLE_JSON_JSONPROPERTYSTREAM_H_INCLUDED

#include <ripple/json/json_value.h>
#include <ripple/beast/utility/PropertyStream.h>

namespace ripple {

/** A PropertyStream::Sink which produces a Json::Value of type objectValue. */
class JsonPropertyStream : public beast::PropertyStream
{
public:
    Json::Value m_top;
    std::vector <Json::Value*> m_stack;

public:
    JsonPropertyStream ();
    Json::Value const& top() const;

protected:

    void map_begin ();
    void map_begin (std::string const& key);
    void map_end ();
    void add (std::string const& key, short value);
    void add (std::string const& key, unsigned short value);
    void add (std::string const& key, int value);
    void add (std::string const& key, unsigned int value);
    void add (std::string const& key, long value);
    void add (std::string const& key, float v);
    void add (std::string const& key, double v);
    void add (std::string const& key, std::string const& v);
    void array_begin ();
    void array_begin (std::string const& key);
    void array_end ();

    void add (short value);
    void add (unsigned short value);
    void add (int value);
    void add (unsigned int value);
    void add (long value);
    void add (float v);
    void add (double v);
    void add (std::string const& v);
};

}

#endif
