//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#include "../PropertyStream.h"

#include <limits>

namespace beast {

//------------------------------------------------------------------------------
//
// Item
//
//------------------------------------------------------------------------------

PropertyStream::Item::Item (Source* source)
    : m_source (source)
{
}

PropertyStream::Source& PropertyStream::Item::source() const
{
    return *m_source;
}

PropertyStream::Source* PropertyStream::Item::operator-> () const
{
    return &source();
}

PropertyStream::Source& PropertyStream::Item::operator* () const
{
    return source();
}

//------------------------------------------------------------------------------
//
// Proxy
//
//------------------------------------------------------------------------------

PropertyStream::Proxy::Proxy (
    Map const& map, std::string const& key)
    : m_map (&map)
    , m_key (key)
{
}

//------------------------------------------------------------------------------
//
// Map
//
//------------------------------------------------------------------------------

PropertyStream::Map::Map (PropertyStream& stream)
    : m_stream (stream)
{
}

PropertyStream::Map::Map (Set& parent)
    : m_stream (parent.stream())
{
    m_stream.map_begin ();
}

PropertyStream::Map::Map (std::string const& key, Map& map)
    : m_stream (map.stream())
{
    m_stream.map_begin (key);
}

PropertyStream::Map::Map (std::string const& key, PropertyStream& stream)
    : m_stream (stream)
{
    m_stream.map_begin (key);
}

PropertyStream::Map::~Map ()
{
    m_stream.map_end ();
}

PropertyStream& PropertyStream::Map::stream()
{
    return m_stream;
}

PropertyStream const& PropertyStream::Map::stream() const
{
    return m_stream;
}

PropertyStream::Proxy PropertyStream::Map::operator[] (std::string const& key)
{
    return Proxy (*this, key);
}

//------------------------------------------------------------------------------
//
// Set
//
//------------------------------------------------------------------------------

PropertyStream::Set::Set (Set& set)
    : m_stream (set.m_stream)
{
    m_stream.array_begin ();
}

PropertyStream::Set::Set (std::string const& key, Map& map)
    : m_stream (map.stream())
{
    m_stream.array_begin (key);
}

PropertyStream::Set::Set (std::string const& key, PropertyStream& stream)
    : m_stream (stream)
{
    m_stream.array_begin (key);
}

PropertyStream::Set::~Set ()
{
    m_stream.array_end ();
}

PropertyStream& PropertyStream::Set::stream()
{
    return m_stream;
}

PropertyStream const& PropertyStream::Set::stream() const
{
    return m_stream;
}

//------------------------------------------------------------------------------
//
// Source
//
//------------------------------------------------------------------------------

PropertyStream::Source::Source (std::string const& name)
    : m_name (name)
    , m_state (this)
{
}

PropertyStream::Source::~Source ()
{
    SharedState::Access state (m_state);
    if (state->parent != nullptr)
        state->parent->remove (*this);
    removeAll (state);
}

std::string const& PropertyStream::Source::name () const
{
    return m_name;
}

void PropertyStream::Source::add (Source& source)
{
    SharedState::Access state (m_state);
    SharedState::Access childState (source.m_state);
    bassert (childState->parent == nullptr);
    state->children.push_back (childState->item);
    childState->parent = this;
}

void PropertyStream::Source::remove (Source& child)
{
    SharedState::Access state (m_state);
    SharedState::Access childState (child.m_state);
    remove (state, childState);
}

void PropertyStream::Source::removeAll ()
{
    SharedState::Access state (m_state);
    removeAll (state);
}

//------------------------------------------------------------------------------

void PropertyStream::Source::write (
    SharedState::Access& state, PropertyStream &stream)
{
    for (List <Item>::iterator iter (state->children.begin());
        iter != state->children.end(); ++iter)
    {
        Source& source (iter->source());
        Map map (source.name(), stream);
        source.write (stream);
    }
}

//------------------------------------------------------------------------------

void PropertyStream::Source::write_one (PropertyStream& stream)
{
    Map map (m_name, stream);
    //onWrite (map);
}

void PropertyStream::Source::write (PropertyStream& stream)
{
    Map map (m_name, stream);
    onWrite (map);

    SharedState::Access state (m_state);

    for (List <Item>::iterator iter (state->children.begin());
        iter != state->children.end(); ++iter)
    {
        Source& source (iter->source());
        source.write (stream);
    }
}

void PropertyStream::Source::write (PropertyStream& stream, std::string const& path)
{
    std::pair <Source*, bool> result (find (path));

    if (result.first == nullptr)
        return;

    if (result.second)
        result.first->write (stream);
    else
        result.first->write_one (stream);
}

std::pair <PropertyStream::Source*, bool> PropertyStream::Source::find (std::string const& path)
{
    struct Parser
    {
        Parser (std::string const& path)
            : m_first (path.begin())
            , m_last (path.end())
        {
        }

        std::string next ()
        {
            std::string::const_iterator pos (
                std::find (m_first, m_last, '.'));
            std::string const s (m_first, pos);
            if (pos != m_last)
                m_first = pos + 1;
            else
                m_first = pos;
            return s;
        }

        std::string::const_iterator m_first;
        std::string::const_iterator m_last;
    };

    if (path.empty ())
        return std::make_pair (this, false);

    Parser p (path);
    Source* source (this);
    if (p.next() != this->m_name)
        return std::make_pair (nullptr, false);

    for (;;)
    {
        std::string const s (p.next());

        if (s.empty())
            return std::make_pair (source, false);

        if (s == "*")
            return std::make_pair (source, true);

        SharedState::Access state (source->m_state);
        for (List <Item>::iterator iter (state->children.begin());;)
        {
            if (iter->source().m_name == s)
            {
                source = &iter->source();
                break;
            }

            if (++iter == state->children.end())
                return std::make_pair (nullptr, false);
        }
    }
}

void PropertyStream::Source::onWrite (Map&)
{
}

//------------------------------------------------------------------------------

void PropertyStream::Source::remove (
    SharedState::Access& state, SharedState::Access& childState)
{
    bassert (childState->parent == this);
    state->children.erase (
        state->children.iterator_to (
            childState->item));
    childState->parent = nullptr;
}

void PropertyStream::Source::removeAll (SharedState::Access& state)
{
    for (List <Item>::iterator iter (state->children.begin());
        iter != state->children.end();)
    {
        SharedState::Access childState ((*iter)->m_state);
        remove (state, childState);
    }
}

//------------------------------------------------------------------------------
//
// PropertyStream
//
//------------------------------------------------------------------------------

PropertyStream::PropertyStream ()
{
}

PropertyStream::~PropertyStream ()
{
}

void PropertyStream::add (std::string const& key, int32 value)
{
    lexical_add (key, value);
}

void PropertyStream::add (std::string const& key, uint32 value)
{
    lexical_add (key, value);
}

void PropertyStream::add (std::string const& key, int64 value)
{
    if (value <= std::numeric_limits <int32>::max() &&
        value >= std::numeric_limits <int32>::min())
    {
        add (key, int32(value));
    }
    else
    {
        lexical_add(key, value);
    }
}

void PropertyStream::add (std::string const& key, uint64 value)
{
    if (value <= std::numeric_limits <uint32>::max() &&
        value >= std::numeric_limits <uint32>::min())
    {
        add (key, uint32(value));
    }
    else
    {
        lexical_add (key, value);
    }
}

void PropertyStream::add (int32 value)
{
    lexical_add (value);
}

void PropertyStream::add (uint32 value)
{
    lexical_add (value);
}

void PropertyStream::add (int64 value)
{
    if (value <= std::numeric_limits <int32>::max() &&
        value >= std::numeric_limits <int32>::min())
    {
        add (int32(value));
    }
    else
    {
        lexical_add (value);
    }
}

void PropertyStream::add (uint64 value)
{
    if (value <= std::numeric_limits <uint32>::max() &&
        value >= std::numeric_limits <uint32>::min())
    {
        add (uint32(value));
    }
    else
    {
        lexical_add (value);
    }
}


}
