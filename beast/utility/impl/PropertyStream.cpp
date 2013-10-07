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

void PropertyStream::Sink::write (std::string const& key, int32 value)
{
    lexical_write (key, value);
}

void PropertyStream::Sink::write (std::string const& key, uint32 value)
{
    lexical_write (key, value);
}

void PropertyStream::Sink::write (std::string const& key, int64 value)
{
    if (value <= std::numeric_limits <int32>::max() &&
        value >= std::numeric_limits <int32>::min())
    {
        write (key, int32(value));
    }
    else
    {
        lexical_write (key, value);
    }
}

void PropertyStream::Sink::write (std::string const& key, uint64 value)
{
    if (value <= std::numeric_limits <uint32>::max() &&
        value >= std::numeric_limits <uint32>::min())
    {
        write (key, uint32(value));
    }
    else
    {
        lexical_write (key, value);
    }
}

void PropertyStream::Sink::write (int32 value)
{
    lexical_write (value);
}

void PropertyStream::Sink::write (uint32 value)
{
    lexical_write (value);
}

void PropertyStream::Sink::write (int64 value)
{
    if (value <= std::numeric_limits <int32>::max() &&
        value >= std::numeric_limits <int32>::min())
    {
        write (int32(value));
    }
    else
    {
        lexical_write (value);
    }
}

void PropertyStream::Sink::write (uint64 value)
{
    if (value <= std::numeric_limits <uint32>::max() &&
        value >= std::numeric_limits <uint32>::min())
    {
        write (uint32(value));
    }
    else
    {
        lexical_write (value);
    }
}

//------------------------------------------------------------------------------

PropertyStream::Proxy::Proxy (PropertyStream stream, std::string const& key)
    : m_stream (stream)
    , m_key (key)
{
}

//------------------------------------------------------------------------------

PropertyStream::ScopedObject::ScopedObject (std::string const& key, PropertyStream stream)
    : m_stream (stream)
{
    m_stream.begin_object (key);
}
       
PropertyStream::ScopedObject::~ScopedObject ()
{
    m_stream.end_object ();
}

//------------------------------------------------------------------------------

PropertyStream::ScopedArray::ScopedArray (std::string const& key, PropertyStream stream)
    : m_stream (stream)
{
    m_stream.begin_array (key);
}
       
PropertyStream::ScopedArray::~ScopedArray ()
{
    m_stream.end_array ();
}

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

void PropertyStream::Source::write (PropertyStream stream, bool includeChildren)
{
    ScopedObject child (m_name, stream);
    onWrite (stream);

    if (includeChildren)
    {
        SharedState::Access state (m_state);
        for (List <Item>::iterator iter (state->children.begin());
            iter != state->children.end(); ++iter)
        {
            (*iter)->write (stream, true);
        }
    }
}

void PropertyStream::Source::write (std::string const& path, PropertyStream stream)
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

    //-----------------------------------------

    if (path.empty ())
    {
        write (stream, true);
        return;
    }

    Parser p (path);
    Source* source (this);
    if (p.next() != source->m_name)
        return;

    for (;;)
    {
        std::string const s (p.next());

        if (s.empty())
        {
            source->write (stream, false);
            break;
        }
        else if (s == "*")
        {
            source->write (stream, true);
            break;
        }
        else
        {
            SharedState::Access state (source->m_state);
            for (List <Item>::iterator iter (state->children.begin());;)
            {
                if (iter->source().m_name == s)
                {
                    source = &iter->source();
                    break;
                }

                if (++iter == state->children.end())
                    return;
            }
        }
    }
}

//------------------------------------------------------------------------------

PropertyStream::PropertyStream ()
    : m_sink (&nullSink())
{
}

PropertyStream::PropertyStream (Sink& sink)
    : m_sink (&sink)
{
}

PropertyStream::PropertyStream (PropertyStream const& other)
    : m_sink (other.m_sink)
{
}

PropertyStream& PropertyStream::operator= (PropertyStream const& other)
{
    m_sink = other.m_sink;
    return *this;
}

PropertyStream::Proxy PropertyStream::operator[] (std::string const& key) const
{
    return Proxy (*this, key);
}

void PropertyStream::begin_object (std::string const& key) const
{
    m_sink->begin_object (key);
}

void PropertyStream::end_object () const
{
    m_sink->end_object ();
}

void PropertyStream::begin_array (std::string const& key) const
{
    m_sink->begin_array (key);
}

void PropertyStream::end_array () const
{
    m_sink->end_array ();
}

PropertyStream::Sink& PropertyStream::nullSink()
{
    struct NullSink : Sink
    {
        void begin_object (std::string const&) { }
        void end_object () { }
        void write (std::string const&, std::string const&) { }
        void begin_array (std::string const&) { }
        void end_array () { }
        void write (std::string const&) { }
    };

    static NullSink sink;

    return sink;
}

}
