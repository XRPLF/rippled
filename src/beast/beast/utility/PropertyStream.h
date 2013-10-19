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

#ifndef BEAST_UTILITY_PROPERTYSTREAM_H_INCLUDED
#define BEAST_UTILITY_PROPERTYSTREAM_H_INCLUDED

#include "../CStdInt.h"
#include "../Uncopyable.h"
#include "../intrusive/List.h"
#include "../threads/SharedData.h"

#include <sstream>
#include <string>
#include <utility>

namespace beast {

//------------------------------------------------------------------------------

/** Abstract stream with RAII containers that produce a property tree. */
class PropertyStream
{
public:
    class Map;
    class Set;
    class Source;

    PropertyStream ();
    virtual ~PropertyStream ();

protected:
    virtual void map_begin () = 0;
    virtual void map_begin (std::string const& key) = 0;
    virtual void map_end () = 0;

    virtual void add (std::string const& key, std::string const& value) = 0;

    template <typename Value>
    void lexical_add (std::string const &key, Value value)
    {
        std::stringstream ss;
        ss << value;
        add (key, ss.str());
    }
    virtual void add (std::string const& key,   int32 value);
    virtual void add (std::string const& key,  uint32 value);
    virtual void add (std::string const& key,   int64 value);
    virtual void add (std::string const& key,  uint64 value);

    virtual void array_begin () = 0;
    virtual void array_begin (std::string const& key) = 0;
    virtual void array_end () = 0;

    virtual void add (std::string const& value) = 0;

    template <typename Value>
    void lexical_add (Value value)
    {
        std::stringstream ss;
        ss << value;
        add (ss.str());
    }
    virtual void add ( int32 value);
    virtual void add (uint32 value);
    virtual void add ( int64 value);
    virtual void add (uint64 value);

private:
    class Item;
    class Proxy;
};

//------------------------------------------------------------------------------
//
// Item
//

class PropertyStream::Item : public List <Item>::Node
{
public:
    explicit Item (Source* source);
    Source& source() const;
    Source* operator-> () const;
    Source& operator* () const;
private:
    Source* m_source;
};

//------------------------------------------------------------------------------
//
// Proxy
//

class PropertyStream::Proxy
{
private:
	Map const* m_map;
    std::string m_key;

public:
    Proxy (Map const& map, std::string const& key);

    template <typename Value>
    Proxy& operator= (Value value);
};

//------------------------------------------------------------------------------
//
// Map
//

class PropertyStream::Map : public Uncopyable
{
private:
    PropertyStream& m_stream;

public:
    explicit Map (PropertyStream& stream);
    explicit Map (Set& parent);
    Map (std::string const& key, Map& parent);
    Map (std::string const& key, PropertyStream& stream);
    ~Map ();

    PropertyStream& stream();
    PropertyStream const& stream() const;

    template <typename Value>
    void add (std::string const& key, Value value) const
    {
        m_stream.add (key, value);
    }

    template <typename Key, typename Value>
    void add (Key key, Value value) const
    {
        std::stringstream ss;
        ss << key;
        add (ss.str(), value);
    }

    Proxy operator[] (std::string const& key);    

    Proxy operator[] (char const* key)
        { return Proxy (*this, key); }

    template <typename Key>
    Proxy operator[] (Key key) const
    {
        std::stringstream ss;
        ss << key;
        return Proxy (*this, ss.str());
    }
};

//--------------------------------------------------------------------------

template <typename Value>
PropertyStream::Proxy& PropertyStream::Proxy::operator= (Value value)
{
    m_map->add (m_key, value);
    return *this;
}

//--------------------------------------------------------------------------
//
// Set
//

class PropertyStream::Set : public Uncopyable
{
private:
    PropertyStream& m_stream;

public:
    explicit Set (Set& set);
    Set (std::string const& key, Map& map);
    Set (std::string const& key, PropertyStream& stream);
    ~Set ();

    PropertyStream& stream();
    PropertyStream const& stream() const;

    template <typename Value>
    void add (Value value) const
        { m_stream.add (value); }
};

//------------------------------------------------------------------------------
//
// Source
//

/** Subclasses can be called to write to a stream and have children. */
class PropertyStream::Source : public Uncopyable
{
private:
    struct State
    {
        explicit State (Source* source)
            : item (source)
            , parent (nullptr)
            { }

        Item item;
        Source* parent;
        List <Item> children;
    };

    typedef SharedData <State> SharedState;

    std::string const m_name;
    SharedState m_state;

    //--------------------------------------------------------------------------

    void remove    (SharedState::Access& state,
                    SharedState::Access& childState);

    void removeAll (SharedState::Access& state);

    void write     (SharedState::Access& state, PropertyStream& stream);

public:
    explicit Source (std::string const& name);
    ~Source ();

    /** Returns the name of this source. */
    std::string const& name() const;

    /** Add a child source. */
    void add (Source& source);

    /** Add a child source by pointer.
        The source pointer is returned so it can be used in ctor-initializers.
    */
    template <class Derived>
    Derived* add (Derived* child)
    {
        add (*static_cast <Source*>(child));
        return child;
    }

    /** Remove a child source from this Source. */
    void remove (Source& child);

    /** Remove all child sources of this Source. */
    void removeAll ();

    /** Write only this Source to the stream. */
    void write_one  (PropertyStream& stream);

    /** write this source and all its children recursively to the stream. */
    void write      (PropertyStream& stream);

    /** Parse the path and write the corresponding Source and optional children.
        If the source is found, it is written. If the wildcard character '*'
        exists as the last character in the path, then all the children are
        written recursively.
    */
    void write      (PropertyStream& stream, std::string const& path);

    /** Parse the dot-delimited Source path and return the result.
        The first value will be a pointer to the Source object corresponding
        to the given path. If no Source object exists, then the first value
        will be nullptr and the second value will be undefined.
        The second value is a boolean indicating whether or not the path string
        specifies the wildcard character '*' as the last character.
    */
    std::pair <Source*, bool> find (std::string const& path);

    //--------------------------------------------------------------------------

    /** Subclass override.
        The default version does nothing.
    */
    virtual void onWrite (Map&);
};

}

#endif
