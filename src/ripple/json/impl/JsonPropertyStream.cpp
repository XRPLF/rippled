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

#include <ripple/json/json_value.h>
#include <ripple/json/JsonPropertyStream.h>

namespace ripple {

JsonPropertyStream::JsonPropertyStream ()
    : m_top (Json::objectValue)
{
    m_stack.reserve (64);
    m_stack.push_back (&m_top);
}

Json::Value const& JsonPropertyStream::top() const
{
    return m_top;
}

void JsonPropertyStream::map_begin ()
{
    // top is array
    Json::Value& top (*m_stack.back());
    Json::Value& map (top.append (Json::objectValue));
    m_stack.push_back (&map);
}

void JsonPropertyStream::map_begin (std::string const& key)
{
    // top is a map
    Json::Value& top (*m_stack.back());
    Json::Value& map (top [key] = Json::objectValue);
    m_stack.push_back (&map);
}

void JsonPropertyStream::map_end ()
{
    m_stack.pop_back ();
}

void JsonPropertyStream::add (std::string const& key, short v)
{
    (*m_stack.back())[key] = v;
}

void JsonPropertyStream::add (std::string const& key, unsigned short v)
{
    (*m_stack.back())[key] = v;
}

void JsonPropertyStream::add (std::string const& key, int v)
{
    (*m_stack.back())[key] = v;
}

void JsonPropertyStream::add (std::string const& key, unsigned int v)
{
    (*m_stack.back())[key] = v;
}

void JsonPropertyStream::add (std::string const& key, long v)
{
    (*m_stack.back())[key] = int(v);
}

void JsonPropertyStream::add (std::string const& key, float v)
{
    (*m_stack.back())[key] = v;
}

void JsonPropertyStream::add (std::string const& key, double v)
{
    (*m_stack.back())[key] = v;
}

void JsonPropertyStream::add (std::string const& key, std::string const& v)
{
    (*m_stack.back())[key] = v;
}

void JsonPropertyStream::array_begin ()
{
    // top is array
    Json::Value& top (*m_stack.back());
    Json::Value& vec (top.append (Json::arrayValue));
    m_stack.push_back (&vec);
}

void JsonPropertyStream::array_begin (std::string const& key)
{
    // top is a map
    Json::Value& top (*m_stack.back());
    Json::Value& vec (top [key] = Json::arrayValue);
    m_stack.push_back (&vec);
}

void JsonPropertyStream::array_end ()
{
    m_stack.pop_back ();
}

void JsonPropertyStream::add (short v)
{
    m_stack.back()->append (v);
}

void JsonPropertyStream::add (unsigned short v)
{
    m_stack.back()->append (v);
}

void JsonPropertyStream::add (int v)
{
    m_stack.back()->append (v);
}

void JsonPropertyStream::add (unsigned int v)
{
    m_stack.back()->append (v);
}

void JsonPropertyStream::add (long v)
{
    m_stack.back()->append (int (v));
}

void JsonPropertyStream::add (float v)
{
    m_stack.back()->append (v);
}

void JsonPropertyStream::add (double v)
{
    m_stack.back()->append (v);
}

void JsonPropertyStream::add (std::string const& v)
{
    m_stack.back()->append (v);
}

}

