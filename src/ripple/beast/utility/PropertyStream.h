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

#include <ripple/beast/core/List.h>

#include <cstdint>
#include <mutex>
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

    PropertyStream () = default;
    virtual ~PropertyStream () = default;

protected:
    virtual void map_begin () = 0;
    virtual void map_begin (std::string const& key) = 0;
    virtual void map_end () = 0;

    virtual void add (std::string const& key, std::string const& value) = 0;

    void add (std::string const& key, char const* value)
    {
        add (key, std::string (value));
    }

    template <typename Value>
    void lexical_add (std::string const &key, Value value)
    {
        std::stringstream ss;
        ss << value;
        add (key, ss.str());
    }

    virtual void add (std::string const& key, bool value);
    virtual void add (std::string const& key, char value);
    virtual void add (std::string const& key, signed char value);
    virtual void add (std::string const& key, unsigned char value);
    virtual void add (std::string const& key, wchar_t value);
#if 0
    virtual void add (std::string const& key, char16_t value);
    virtual void add (std::string const& key, char32_t value);
#endif
    virtual void add (std::string const& key, short value);
    virtual void add (std::string const& key, unsigned short value);
    virtual void add (std::string const& key, int value);
    virtual void add (std::string const& key, unsigned int value);
    virtual void add (std::string const& key, long value);
    virtual void add (std::string const& key, unsigned long value);
    virtual void add (std::string const& key, long long value);
    virtual void add (std::string const& key, unsigned long long value);
    virtual void add (std::string const& key, float value);
    virtual void add (std::string const& key, double value);
    virtual void add (std::string const& key, long double value);

    virtual void array_begin () = 0;
    virtual void array_begin (std::string const& key) = 0;
    virtual void array_end () = 0;

    virtual void add (std::string const& value) = 0;

    void add (char const* value)
    {
        add (std::string (value));
    }

    template <typename Value>
    void lexical_add (Value value)
    {
        std::stringstream ss;
        ss << value;
        add (ss.str());
    }

    virtual void add (bool value);
    virtual void add (char value);
    virtual void add (signed char value);
    virtual void add (unsigned char value);
    virtual void add (wchar_t value);
#if 0
    virtual void add (char16_t value);
    virtual void add (char32_t value);
#endif
    virtual void add (short value);
    virtual void add (unsigned short value);
    virtual void add (int value);
    virtual void add (unsigned int value);
    virtual void add (long value);
    virtual void add (unsigned long value);
    virtual void add (long long value);
    virtual void add (unsigned long long value);
    virtual void add (float value);
    virtual void add (double value);
    virtual void add (long double value);

private:
    class Item;
    class Proxy;
};

//------------------------------------------------------------------------------
//
// Item
//
//------------------------------------------------------------------------------

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
//------------------------------------------------------------------------------

class PropertyStream::Proxy
{
private:
    Map const* m_map;
    std::string m_key;
    std::ostringstream mutable m_ostream;

public:
    Proxy (Map const& map, std::string const& key);
    Proxy (Proxy const& other);
    ~Proxy ();

    template <typename Value>
    Proxy& operator= (Value value);

    std::ostream& operator<< (std::ostream& manip (std::ostream&)) const;

    template <typename T>
    std::ostream& operator<< (T const& t) const
    {
        return m_ostream << t;
    }
};

//------------------------------------------------------------------------------
//
// Map
//
//------------------------------------------------------------------------------

class PropertyStream::Map
{
private:
    PropertyStream& m_stream;

public:
    explicit Map (PropertyStream& stream);
    explicit Map (Set& parent);
    Map (std::string const& key, Map& parent);
    Map (std::string const& key, PropertyStream& stream);
    ~Map ();

    Map(Map const&) = delete;
    Map& operator= (Map const&) = delete;

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
//------------------------------------------------------------------------------

class PropertyStream::Set
{
private:
    PropertyStream& m_stream;

public:
    Set (std::string const& key, Map& map);
    Set (std::string const& key, PropertyStream& stream);
    ~Set ();

    Set (Set const&) = delete;
    Set& operator= (Set const&) = delete;

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
//------------------------------------------------------------------------------

/** Subclasses can be called to write to a stream and have children. */
class PropertyStream::Source
{
private:
    std::string const m_name;
    std::recursive_mutex lock_;
    Item item_;
    Source* parent_;
    List <Item> children_;

public:
    explicit Source (std::string const& name);
    virtual ~Source ();

    Source (Source const&) = delete;
    Source& operator= (Source const&) = delete;

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

    /** Remove all child sources from this Source. */
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

        print statement examples
        "parent.child" prints child and all of its children
        "parent.child." start at the parent and print down to child
        "parent.grandchild" prints nothing- grandchild not direct discendent
        "parent.grandchild." starts at the parent and prints down to grandchild
        "parent.grandchild.*" starts at parent, print through grandchild children
    */
    std::pair <Source*, bool> find (std::string path);

    Source* find_one_deep (std::string const& name);
    PropertyStream::Source* find_path(std::string path);
    PropertyStream::Source* find_one(std::string const& name);

    static bool peel_leading_slash (std::string* path);
    static bool peel_trailing_slashstar (std::string* path);
    static std::string peel_name(std::string* path);


    //--------------------------------------------------------------------------

    /** Subclass override.
        The default version does nothing.
    */
    virtual void onWrite (Map&);
};

}

#endif
