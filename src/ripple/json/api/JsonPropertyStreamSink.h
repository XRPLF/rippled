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

#ifndef RIPPLE_JSONPROPERTYSTREAMSINK_H_INCLUDED
#define RIPPLE_JSONPROPERTYSTREAMSINK_H_INCLUDED

#include "beast/beast/utility/PropertyStream.h"

//#include "json_value.h" // ??

namespace ripple {
using namespace beast;

/** A PropertyStream::Sink which produces a Json::Value. */
class JsonPropertyStreamSink : public PropertyStream::Sink
{
public:
    explicit JsonPropertyStreamSink (Json::Value& root)
    {
        m_stack.push_back (&root);
    }

    void begin_object (std::string const& key)
    {
        m_stack.push_back (&((*m_stack.back())[key] = Json::objectValue));
    }

    void end_object ()
    {
        m_stack.pop_back ();
    }

    void write (std::string const& key, int32 v)
    {
        (*m_stack.back())[key] = v;
    }

    void write (std::string const& key, uint32 v)
    {
        (*m_stack.back())[key] = v;
    }

    void write (std::string const& key, std::string const& v)
    {
        (*m_stack.back())[key] = v;
    }

    void begin_array (std::string const& key)
    {
        m_stack.push_back (&((*m_stack.back())[key] = Json::arrayValue));
    }

    void end_array ()
    {
        m_stack.pop_back ();
    }

    void write (int32 v)
    {
        m_stack.back()->append (v);
    }

    void write (uint32 v)
    {
        m_stack.back()->append (v);
    }

    void write (std::string const& v)
    {
        m_stack.back()->append (v);
    }

private:
    Json::Value m_value;
    std::vector <Json::Value*> m_stack;
};

}

#endif

