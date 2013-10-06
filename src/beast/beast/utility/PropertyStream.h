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

#include <string>

namespace beast {

/** An output stream to procedurally generate an abstract property tree. */
class PropertyStream
{
private:
    class Proxy;

public:
    class ScopedArray;
    class ScopedObject;
    class Source;

private:
    class Item : public List <Item>::Node
    {
    public:
        explicit Item (Source* source);
        Source& source() const;
        Source* operator-> () const;
        Source& operator* () const;
    private:
        Source* m_source;
    };

public:
    //--------------------------------------------------------------------------

    class Sink : Uncopyable
    {
    public:
        // Object output
        //
        // Default implementations convert to string
        // Json doesn't support 64 bit so we convert these to string
        // if they are outside the range of the corresponding 32 bit int
        virtual void begin_object (std::string const& key) = 0;
        virtual void end_object () = 0;
        template <typename Value>
        void lexical_write (std::string const &key, Value value)
        {
            std::stringstream ss;
            ss << value;
            write (key, ss.str());
        }
        virtual void write (std::string const& key,  int32 value);
        virtual void write (std::string const& key, uint32 value);
        virtual void write (std::string const& key,  int64 value);
        virtual void write (std::string const& key, uint64 value);
        virtual void write (std::string const& key, std::string const& value) = 0;

        // Array output
        //
        virtual void begin_array (std::string const& key) = 0;
        virtual void end_array () = 0;
        template <typename Value>
        void lexical_write (Value value)
        {
            std::stringstream ss;
            ss << value;
            write (ss.str());
        }
        virtual void write ( int32 value);
        virtual void write (uint32 value);
        virtual void write ( int64 value);
        virtual void write (uint64 value);
        virtual void write (std::string const& value) = 0;
    };

    //--------------------------------------------------------------------------

    PropertyStream ();
    PropertyStream (Sink& sink);
    PropertyStream (PropertyStream const& other);
    PropertyStream& operator= (PropertyStream const& other);

    /** Object output.
    */
    /** @{ */
    void begin_object (std::string const& key) const;
    void end_object () const;

    template <typename Value>
    void write (std::string const& key, Value value) const
    {
        m_sink->write (key, value);
    }

    template <typename Key, typename Value>
    void write (Key key, Value value) const
    {
        std::stringstream ss;
        ss << key;
        write (ss.str(), value);
    }

    Proxy operator[] (std::string const& key) const;

    template <typename Key>
    Proxy operator[] (Key key) const;

    /** @} */

    /** Array output.
    */
    /** @{ */
    void begin_array (std::string const& key) const;
    void end_array () const;

    template <typename Value>
    void append (Value value) const
        { m_sink->write (value); }

    template <typename Value>
    PropertyStream const& operator<< (Value value) const
        { append (value); return &this; }
    /** @} */

private:
    static Sink& nullSink();

    Sink* m_sink;
};

//------------------------------------------------------------------------------

class PropertyStream::Proxy
{
private:
    PropertyStream m_stream;
    std::string m_key;

public:
    Proxy (PropertyStream stream, std::string const& key);

    template <typename Value>
    Proxy& operator= (Value value)
        { m_stream.write (m_key, value); return *this; }
};

//------------------------------------------------------------------------------

template <typename Key>
PropertyStream::Proxy PropertyStream::operator[] (Key key) const
{
    std::stringstream ss;
    ss << key;
    return operator[] (ss.str());
}

//------------------------------------------------------------------------------

class PropertyStream::ScopedObject
{
private:
    PropertyStream m_stream;

public:
    ScopedObject (std::string const& key, PropertyStream stream);
    ~ScopedObject ();
};

//------------------------------------------------------------------------------

class PropertyStream::ScopedArray
{
private:
    PropertyStream m_stream;

public:
    ScopedArray (std::string const& key, PropertyStream stream);
    ~ScopedArray ();
};

//------------------------------------------------------------------------------

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

    void remove (SharedState::Access& state, SharedState::Access& childState);
    void removeAll (SharedState::Access& state);

public:
    explicit Source (std::string const& name);
    ~Source ();

    /** Add a child source. */
    void add (Source& source);

    /** Add a child source by pointer.
        This returns the passed source so it can be conveniently chained
        in ctor-initializer lists.
    */
    template <class Derived>
    Derived* add (Derived* child)
    {
        add (*static_cast <Source*>(child));
        return child;
    }

    /** Remove a child source. */
    void remove (Source& child);

    /** Remove all child sources. */
    void removeAll ();

    void write (PropertyStream stream, bool includeChildren);
    void write (std::string const& path, PropertyStream stream);

    virtual void onWrite (PropertyStream) { }
};

//------------------------------------------------------------------------------

}

#endif
