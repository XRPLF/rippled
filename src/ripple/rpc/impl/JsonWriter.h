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

#include <ripple/basics/ToString.h>
#include <ripple/rpc/Output.h>

namespace ripple {
namespace RPC {
namespace New {

/**
 *  Writer implements an O(1)-space, O(1)-granular output JSON writer.
 *
 *  O(1)-space means that it uses a fixed amount of memory, and that there are
 *  no heap allocations at each step of the way.
 *
 *  O(1)-granular output means the writer only outputs in small segments of a
 *  bounded size, using a bounded number of CPU cycles in doing so.  This is
 *  very helpful in scheduling long jobs.
 *
 *  The tradeoff is that you have to fill items in the JSON tree as you go,
 *  and you can never go backward.
 *
 *  Writer can write single JSON tokens, but the typical use is to write out an
 *  entire JSON object.  For example:
 *
 *      {
 *          Writer w (out);
 *
 *          w.startObject ();          // Start the root object.
 *          w.set ("hello", "world");
 *          w.set ("goodbye", 23);
 *          w.finishObject ();         // Finish the root object.
 *      }
 *
 *  which outputs the string
 *
 *      {"hello":"world","goodbye":23}
 *
 *  There can be an object inside an object:
 *
 *      {
 *          Writer w (out);
 *
 *          w.startObject ();                // Start the root object.
 *          w.set ("hello", "world");
 *
 *          w.startObjectSet ("subobject");  // Start a sub-object.
 *          w.set ("goodbye", 23);           // Add a key, value assignment.
 *          w.finishObject ();               // Finish the sub-object.
 *
 *          w.finishObject ();               // Finish the root-object.
 *      }
 *
 *  which outputs the string
 *
 *     {"hello":"world","subobject":{"goodbye":23}}.
 *
 *  Arrays work similarly
 *
 *      {
 *          Writer w (out);
 *          w.startObject ();           // Start the root object.
 *
 *          w.startArraySet ("hello");  // Start an array.
 *          w.append (23)               // Append some items.
 *          w.append ("skidoo")
 *          w.finishArray ();           // Finish the array.
 *
 *          w.finishObject ();          // Finish the root object.
 *      }
 *
 *  which outputs the string
 *
 *      {"hello":[23,"skidoo"]}.
 *
 *
 *  If you've reached the end of a long object, you can just use finishAll()
 *  which finishes all arrays and objects that you have started.
 *
 *      {
 *          Writer w (out);
 *          w.startObject ();           // Start the root object.
 *
 *          w.startArraySet ("hello");  // Start an array.
 *          w.append (23)               // Append an item.
 *
 *          w.startArrayAppend ()       // Start a sub-array.
 *          w.append ("one");
 *          w.append ("two");
 *
 *          w.startObjectAppend ();     // Append a sub-object.
 *          w.finishAll ();             // Finish everything.
 *      }
 *
 *  which outputs the string
 *
 *      {"hello":[23,["one","two",{}]]}.
 *
 *  For convenience, the destructor of Writer calls w.finishAll() which makes
 *  sure that all arrays and objects are closed.  This means that you can throw
 *  an exception, or have a coroutine simply clean up the stack, and be sure
 *  that you do in fact generate a complete JSON object.
 */

class Writer
{
public:
    enum CollectionType {array, object};

    explicit Writer (Output& output);
    Writer(Writer&&);
    Writer& operator=(Writer&&);

    ~Writer();

    /** Start a new collection at the root level. */
    void startRoot (CollectionType);

    /** Start a new collection inside an array. */
    void startAppend (CollectionType);

    /** Start a new collection inside an object. */
    void startSet (CollectionType, std::string const& key);

    /** Finish the collection most recently started. */
    void finish ();

    /** Finish all objects and arrays.  After finishArray() has been called, no
     *  more operations can be performed. */
    void finishAll ();

    /** Append a value to an array.
     *
     *  Scalar must be a scalar - that is, a number, boolean, string, string
     *  literal, or nullptr.
     */
    template <typename Scalar>
    void append (Scalar);

    /** Add a comma before this next item if not the first item in an array.
        Useful if you are writing the actual array yourself. */
    void rawAppend();

    /** Add a key, value assignment to an object.
     *
     *  Scalar must be a scalar - that is, a number, boolean, string, string
     *  literal, or nullptr.
     *
     *  While the JSON spec doesn't explicitly disallow this, you should avoid
     *  calling this method twice with the same tag for the same object.
     *
     *  If CHECK_JSON_WRITER is defined, this function throws an exception if if
     *  the tag you use has already been used in this object.
     */
    template <typename Scalar>
    void set (std::string const& key, Scalar value);

    /** Emit just "tag": as part of an object.  Useful if you are writing the
        actual value data yourself. */
    void rawSet (std::string const& key);

    // You won't need to call anything below here until you are writing single
    // items (numbers, strings, bools, null) to a JSON stream.

    /*** Output a string. */
    void output (std::string const&);

    /*** Output a literal constant or C string. */
    void output (char const*);

    /** Output numbers, booleans, or nullptr. */
    template <typename Scalar>
    void output (Scalar t);

private:
    class Impl;
    std::unique_ptr <Impl> impl_;
};

class JsonException : public std::exception
{
public:
    explicit JsonException (std::string const& name) : name_(name) {}
    const char* what() const throw() override
    {
        return name_.c_str();
    }

private:
    std::string const name_;
};

inline void check (bool condition, std::string const& message)
{
    if (!condition)
        throw JsonException (message);
}

} // New
} // RPC
} // ripple

#endif
