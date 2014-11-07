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

#ifndef RIPPLED_RIPPLE_RPC_IMPL_JSONCOLLECTIONS_H
#define RIPPLED_RIPPLE_RPC_IMPL_JSONCOLLECTIONS_H

namespace ripple {
namespace RPC {
namespace New {

class Writer;

/**
    Collection is a base class for Array and Object, classes which provide the
    facade of JSON collections for the O(1) JSON writer, while still using no
    heap memory and only a very small amount of stack.

    From http://json.org, JSON has two types of collection: array, and object.
    Everything else is a *scalar* - a number, a string, a boolean or the special
    value null.

    Collections must write JSON "as-it-goes" in order to get the strong
    performance guarantees.  This puts restrictions upon API users:

    1. Only one collection can be open for change at any one time.

       This condition is enforced automatically and a JsonException thrown if it
       is violated.

    2. A tag may only be used once in an Object.

       Some objects have many tags, so this condition might be a little
       expensive. Enforcement of this condition is turned on in debug builds and
       a JsonException is thrown when the tag is added for a second time.

    Code samples:

        Writer writer;

        // An empty object.
        {
            Object::Root (writer);
        }
        // Outputs {}

        // An object with one scalar value.
        {
            Object::Root root (writer);
            write["hello"] = "world";
        }
        // Outputs {"hello":"world"}

        // Same, using chaining.
        {
            Object::Root (writer)["hello"] = "world";
        }
        // Output is the same.

        // Add several scalars, with chaining.
        {
            Object::Root (writer)
                .set ("hello", "world")
                .set ("flag", false)
                .set ("x", 42);
        }
        // Outputs {"hello":"world","flag":false,"x":42}

        // Add an array.
        {
            Object::Root root (writer);
            {
               auto array = root.makeArray ("hands");
               array.append ("left");
               array.append ("right");
            }
        }
        // Outputs {"hands":["left", "right"]}

        // Same, using chaining.
        {
            Object::Root (writer)
                .makeArray ("hands")
                .append ("left")
                .append ("right");
        }
        // Output is the same.

        // Add an object.
        {
            Object::Root root (writer);
            {
               auto object = root.makeObject ("hands");
               object["left"] = false;
               object["right"] = true;
            }
        }
        // Outputs {"hands":{"left":false,"right":true}}

        // Same, using chaining.
        {
            Object::Root (writer)
                .makeObject ("hands")
                .set ("left", false)
                .set ("right", true);
            }
        }
        // Outputs {"hands":{"left":false,"right":true}}


   Typical ways to make mistakes and get a JsonException:

        Writer writer;
        Object::Root root (writer);

        // Repeat a tag.
        {
            root ["hello"] = "world";
            root ["hello"] = "there";  // THROWS! in a debug build.
        }

        // Open a subcollection, then set something else.
        {
            auto object = root.makeObject ("foo");
            root ["hello"] = "world";  // THROWS!
        }

        // Open two subcollections at a time.
        {
            auto object = root.makeObject ("foo");
            auto array = root.makeArray ("bar");  // THROWS!!
        }

   For more examples, check the unit tests.
 */

class Collection
{
public:
    Collection (Collection&& c);
    Collection& operator= (Collection&& c);
    Collection() = delete;

    ~Collection();

protected:
    // A null parent means "no parent at all".
    // Writers cannot be null.
    Collection (Collection* parent, Writer*);
    void checkWritable (std::string const& label);

    Collection* parent_;
    Writer* writer_;
    bool enabled_;
};

class Array;

//------------------------------------------------------------------------------

/** Represents a JSON object being written to a Writer. */
class Object : protected Collection
{
public:
    /** Object::Root is the only Collection that has a public constructor. */
    class Root;

    /** Set a scalar value in the Object for a key.

        A JSON scalar is a single value - a number, string, boolean or null.

        `set()` throws an exception if this object is disabled (which means that
        one of its children is enabled).

        In a debug build, `set()` also throws an exception if the key has
        already been set() before.

        An operator[] is provided to allow writing `object["key"] = scalar;`.
     */
    template <typename Scalar>
    Object& set (std::string const& key, Scalar);

    // Detail class and method used to implement operator[].
    class Proxy;
    Proxy operator[] (std::string const& key);

    /** Make a new Object at a key and return it.

        This Object is disabled until that sub-object is destroyed.
        Throws an exception if this Object was already disabled.
     */
    Object makeObject (std::string const& key);

    /** Make a new Array at a key and return it.

        This Object is disabled until that sub-array is destroyed.
        Throws an exception if this Object was already disabled.
     */
    Array makeArray (std::string const& key);

protected:
    friend class Array;
    Object (Collection* parent, Writer* w) : Collection (parent, w) {}
};

//------------------------------------------------------------------------------

class Object::Root : public Object
{
  public:
    /** Each Object::Root must be constructed with its own unique Writer. */
    Root (Writer&);
};

//------------------------------------------------------------------------------

/** Represents a JSON array being written to a Writer. */
class Array : private Collection
{
public:
    /** Append a scalar to the Arrary.

        Throws an exception if this array is disabled (which means that one of
        its sub-collections is enabled).
    */
    template <typename Scalar>
    Array& append (Scalar);

    /** Append a new Object and return it.

        This Array is disabled until that sub-object is destroyed.
        Throws an exception if this Array was already disabled.
     */
    Object makeObject ();

    /** Append a new Array and return it.

        This Array is disabled until that sub-array is destroyed.
        Throws an exception if this Array was already disabled.
     */
    Array makeArray ();

  protected:
    friend class Object;
    Array (Collection* parent, Writer* w) : Collection (parent, w) {}
};

//------------------------------------------------------------------------------

// Detail class for Object::operator[].
class Object::Proxy
{
private:
    Object& object_;
    std::string const& key_;

public:
    Proxy (Object& object, std::string const& key);

    template <class T>
    Object& operator= (T const& t)
    {
        object_.set (key_, t);
        return object_;
    }
};

} // New
} // RPC
} // ripple

#endif
