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

#ifndef RIPPLE_JSON_JSON_WRITER_H_INCLUDED
#define RIPPLE_JSON_JSON_WRITER_H_INCLUDED

#include <ripple/json/json_forwards.h>
#include <ripple/json/json_value.h>
#include <ostream>
#include <vector>

namespace Json
{

class Value;

/** \brief Abstract class for writers.
 */
class WriterBase
{
public:
    virtual ~WriterBase () {}
    virtual std::string write ( const Value& root ) = 0;
};

/** \brief Outputs a Value in <a HREF="http://www.json.org">JSON</a> format without formatting (not human friendly).
 *
 * The JSON document is written in a single line. It is not intended for 'human' consumption,
 * but may be useful to support feature such as RPC where bandwith is limited.
 * \sa Reader, Value
 */

class FastWriter : public WriterBase
{
public:
    FastWriter ();
    virtual ~FastWriter () {}

public: // overridden from Writer
    virtual std::string write ( const Value& root );

private:
    void writeValue ( const Value& value );

    std::string document_;
};

/** \brief Writes a Value in <a HREF="http://www.json.org">JSON</a> format in a human friendly way.
 *
 * The rules for line break and indent are as follow:
 * - Object value:
 *     - if empty then print {} without indent and line break
 *     - if not empty the print '{', line break & indent, print one value per line
 *       and then unindent and line break and print '}'.
 * - Array value:
 *     - if empty then print [] without indent and line break
 *     - if the array contains no object value, empty array or some other value types,
 *       and all the values fit on one lines, then print the array on a single line.
 *     - otherwise, it the values do not fit on one line, or the array contains
 *       object or non empty array, then print one value per line.
 *
 * If the Value have comments then they are outputed according to their #CommentPlacement.
 *
 * \sa Reader, Value, Value::setComment()
 */
class StyledWriter: public WriterBase
{
public:
    StyledWriter ();
    virtual ~StyledWriter () {}

public: // overridden from Writer
    /** \brief Serialize a Value in <a HREF="http://www.json.org">JSON</a> format.
     * \param root Value to serialize.
     * \return String containing the JSON document that represents the root value.
     */
    virtual std::string write ( const Value& root );

private:
    void writeValue ( const Value& value );
    void writeArrayValue ( const Value& value );
    bool isMultineArray ( const Value& value );
    void pushValue ( std::string const& value );
    void writeIndent ();
    void writeWithIndent ( std::string const& value );
    void indent ();
    void unindent ();

    using ChildValues = std::vector<std::string>;

    ChildValues childValues_;
    std::string document_;
    std::string indentString_;
    int rightMargin_;
    int indentSize_;
    bool addChildValues_;
};

/** \brief Writes a Value in <a HREF="http://www.json.org">JSON</a> format in a human friendly way,
     to a stream rather than to a string.
 *
 * The rules for line break and indent are as follow:
 * - Object value:
 *     - if empty then print {} without indent and line break
 *     - if not empty the print '{', line break & indent, print one value per line
 *       and then unindent and line break and print '}'.
 * - Array value:
 *     - if empty then print [] without indent and line break
 *     - if the array contains no object value, empty array or some other value types,
 *       and all the values fit on one lines, then print the array on a single line.
 *     - otherwise, it the values do not fit on one line, or the array contains
 *       object or non empty array, then print one value per line.
 *
 * If the Value have comments then they are outputed according to their #CommentPlacement.
 *
 * \param indentation Each level will be indented by this amount extra.
 * \sa Reader, Value, Value::setComment()
 */
class StyledStreamWriter
{
public:
    StyledStreamWriter ( std::string indentation = "\t" );
    ~StyledStreamWriter () {}

public:
    /** \brief Serialize a Value in <a HREF="http://www.json.org">JSON</a> format.
     * \param out Stream to write to. (Can be ostringstream, e.g.)
     * \param root Value to serialize.
     * \note There is no point in deriving from Writer, since write() should not return a value.
     */
    void write ( std::ostream& out, const Value& root );

private:
    void writeValue ( const Value& value );
    void writeArrayValue ( const Value& value );
    bool isMultineArray ( const Value& value );
    void pushValue ( std::string const& value );
    void writeIndent ();
    void writeWithIndent ( std::string const& value );
    void indent ();
    void unindent ();

    using ChildValues = std::vector<std::string>;

    ChildValues childValues_;
    std::ostream* document_;
    std::string indentString_;
    int rightMargin_;
    std::string indentation_;
    bool addChildValues_;
};

std::string valueToString ( Int value );
std::string valueToString ( UInt value );
std::string valueToString ( double value );
std::string valueToString ( bool value );
std::string valueToQuotedString ( const char* value );

/// \brief Output using the StyledStreamWriter.
/// \see Json::operator>>()
std::ostream& operator<< ( std::ostream&, const Value& root );

//------------------------------------------------------------------------------

// Helpers for stream
namespace detail {

template <class Write>
void
write_string(Write const& write, std::string const& s)
{
    write(s.data(), s.size());
}

template <class Write>
void
write_value(Write const& write, Value const& value)
{
    switch (value.type())
    {
        case nullValue:
            write("null", 4);
            break;

        case intValue:
            write_string(write, valueToString(value.asInt()));
            break;

        case uintValue:
            write_string(write, valueToString(value.asUInt()));
            break;

        case realValue:
            write_string(write, valueToString(value.asDouble()));
            break;

        case stringValue:
            write_string(write, valueToQuotedString(value.asCString()));
            break;

        case booleanValue:
            write_string(write, valueToString(value.asBool()));
            break;

        case arrayValue:
        {
            write("[", 1);
            int const size = value.size();
            for (int index = 0; index < size; ++index)
            {
                if (index > 0)
                    write(",", 1);
                write_value(write, value[index]);
            }
            write("]", 1);
            break;
        }

        case objectValue:
        {
            Value::Members const members = value.getMemberNames();
            write("{", 1);
            for (auto it = members.begin(); it != members.end(); ++it)
            {
                std::string const& name = *it;
                if (it != members.begin())
                    write(",", 1);

                write_string(write, valueToQuotedString(name.c_str()));
                write(":", 1);
                write_value(write, value[name]);
            }
            write("}", 1);
            break;
        }
    }
}

}  // namespace detail

/** Stream compact JSON to the specified function.

    @param jv The Json::Value to write
    @param write Invocable with signature void(void const*, std::size_t) that
                 is called when output should be written to the stream.
*/
template <class Write>
void
stream(Json::Value const& jv, Write const& write)
{
    detail::write_value(write, jv);
    write("\n", 1);
}

/** Decorator for streaming out compact json

    Use

        Json::Value jv;
        out << Json::Compact{jv}

    to write a single-line, compact version of `jv` to the stream, rather
    than the styled format that comes from undecorated streaming.
*/
class Compact
{
    Json::Value jv_;

public:
    /** Wrap a Json::Value for compact streaming

        @param jv The Json::Value to stream

        @note For now, we do not support wrapping lvalues to avoid
              potentially costly copies. If we find a need, we can consider
              adding support for compact lvalue streaming in the future.
    */
    Compact(Json::Value&& jv) : jv_{std::move(jv)}
    {
    }

    friend std::ostream&
    operator<<(std::ostream& o, Compact const& cJv)
    {
        detail::write_value(
            [&o](void const* data, std::size_t n) {
                o.write(static_cast<char const*>(data), n);
            },
            cJv.jv_);
        return o;
    }
};

}  // namespace Json

#endif // JSON_WRITER_H_INCLUDED
