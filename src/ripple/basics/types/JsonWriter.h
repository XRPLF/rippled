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

#ifndef RIPPLED_RIPPLE_BASICS_TYPES_JSONWRITER_H
#define RIPPLED_RIPPLE_BASICS_TYPES_JSONWRITER_H

#include <ripple/basics/types/Writable.h>
#include <ripple/basics/utility/ToString.h>

namespace ripple {
namespace json {

#ifdef DEBUG

/** Many parts of JsonWriter are only needed if you are checking the consistency
    of the datastructure.  For efficiency, they're only enabled in DEBUG modoe.
*/
#define CHECK_JSON_WRITER

#endif

class Writer
{
public:
    explicit Writer (Writable& writable) : writable_(writable) {}
    ~Writer();

    /** @return true if the Writer has written a complete JSON description.

        If CHECK_JSON_WRITER is defined, the write functions will throw an
        exception if they are called and isFinished() is true. */
    bool isFinished() const;

    /** Write a single string as JSON. */
    void write (std::string const&);
    void write (char const* s)
    {
        write (std::string(s));
    }

    /** Write one float to the JSON stream, with trailing zeroes omitted. */
    void write (float);

    /** Write one double to the JSON stream, with trailing zeroes omitted. */
    void write (double);

    /** Write the nullptr to the stream as the JSON symbol null. */
    void write (nullptr_t);

    /** Generic write method for all ints and booleans. */
    template <typename Type>
    void write (Type t)
    {
        auto s = to_string (t);
        rawWrite (s.data(), s.size());
    }

    /** Start a new array at the root level or inside an array. */
    void startArray ()
    {
        return start (CollectionType::array);
    }

    /** Start a new object at the root level or inside an array. */
    void startObject ()
    {
        return start (CollectionType::object);
    }

    /** Start a new array inside an object. */
    void startArray (std::string const& s)
    {
        return start (s, CollectionType::array);
    }

    /** Start a new object inside an object. */
    void startObject (std::string const& s)
    {
        return start (s, CollectionType::object);
    }

    /** Finish the most recent collection started. */
    void finish ();

    /** Finish all collections started. */
    void finishAll ();

    /** Append to an array started with start (CollectionType::array).

        If CHECK_JSON_WRITER is defined, then this function throws an
        exception start was never called or the most recent call was not with
        CollectionType::array.
     */
    template <typename T>
    void append (T t)
    {
        writeCommaBeforeEntry ();
        write (t);
    }

    /** Add a key, value assignment to an object started with start
        (CollectionType::object).  While the JSON spec doesn't explicitly
        disallow this, you should avoid calling this method twice with the same
        tag for the same object.

        If CHECK_JSON_WRITER is defined, then this function throws an exception
        start was never called or the most recent call was not with
        CollectionType::object.

        If CHECK_JSON_WRITER is defined, then it will also throw an exception if
        the tag you use has already been used in this object.
     */
    template <typename T>
    void set (std::string const& tag, T t)
    {
        writeCommaBeforeEntry ();
        writeObjectTag (tag);
        write (t);
    }

    /** JSON collections are either arrrays, or objects. */
    enum class CollectionType {array, object};

private:
    /* Start a new collection at the root level or inside an array. */
    void start (CollectionType);

    /* Start a new collection inside an object. */
    void start (std::string const& tag, CollectionType);

    void rawWrite (char const* data, size_t length);
    void writeCommaBeforeEntry ();
    void writeObjectTag (std::string const&);
    void rawStart(CollectionType);

    Writable& writable_;
    bool isStarted_ = false;

    struct Collection
    {
        /** What type of collection are we in? */
        CollectionType type;

        /** Is this the first entry in a collection?
         *  If false, we have to emit a , before we write the next entry. */
        bool isFirst = true;

    #ifdef CHECK_JSON_WRITER
        /** What tags have we already seen in this collection? */
        std::set <std::string> tags;
    #endif
    };

    std::stack <Collection, std::vector<Collection>> stack_;

};

} // writer
} // ripple

#endif
