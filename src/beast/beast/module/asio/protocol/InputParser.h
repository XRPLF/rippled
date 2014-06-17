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

#ifndef BEAST_ASIO_HANDSHAKE_INPUTPARSER_H_INCLUDED
#define BEAST_ASIO_HANDSHAKE_INPUTPARSER_H_INCLUDED

#include <beast/module/asio/basics/FixedInputBuffer.h>
#include <beast/strings/String.h>

#include <cctype>

namespace beast {
namespace asio {

namespace InputParser {

/** Tri-valued parsing state.
    This is convertible to bool which means continue.
    Or you can use stop() to decide if you should return.
    After a stop you can use failed () to determine if parsing failed.
*/
struct State
{
    enum State_t
    {
        pass, // passed the parse
        fail, // failed the parse
        more  // didn't fail but need more bytes
    };

    State () : m_state (more) { }

    State (State_t state) : m_state (state) { }

    /** Implicit construction from bool.
        If condition is true then the parse passes, else need more.
    */
    State (bool condition) : m_state (condition ? pass : more) { }

    State& operator= (State_t state) { m_state = state; return *this; }

    bool eof () const noexcept { return m_state == more; }
    bool stop () const noexcept { return m_state != pass; }
    bool passed () const noexcept { return m_state == pass; }
    bool failed () const noexcept { return m_state == fail; }
    explicit operator bool() const noexcept { return m_state == pass; }

private:
    State_t m_state;
};

//------------------------------------------------------------------------------

// Shortcut to save typing.
typedef FixedInputBuffer& Input;

/** Specializations implement the get() function. */
template <class T>
struct Get;

/** Specializations implement the match() function.
    Default implementation of match tries to read it into a local.
*/
template <class T>
struct Match
{
    static State func (Input in, T other)
    {
        T t;
        State state = Get <T>::func (in, t);
        if (state.passed ())
        {
            if (t == other)
                return State::pass;
            return State::fail;
        }
        return state;
    }
};

/** Specializations implement the peek() function.
    Default implementation of peek reads and rewinds.
*/
template <class T>
struct Peek
{
    static State func (Input in, T& t)
    {
        Input dup (in);
        return Get <T>::func (dup, t);
    }
};

//------------------------------------------------------------------------------
//
// Free Functions
//
//------------------------------------------------------------------------------

// match a block of data in memory
//
static State match_buffer (Input in, void const* buffer, std::size_t bytes)
{
    bassert (bytes > 0);
    if (in.size () <= 0)
        return State::more;

    std::size_t const have = std::min (in.size (), bytes);
    void const* data = in.peek (have);
    bassert (data != nullptr);

    int const compare = memcmp (data, buffer, have);
    if (compare != 0)
        return State::fail;
    in.consume (have);

    return have == bytes;
}

//------------------------------------------------------------------------------
//
// match
//

// Returns the state
template <class T>
State match (Input in, T t)
{
    return Match <T>::func (in, t);
}

// Stores the state in the argument and returns true if its a pass
template <class T>
bool match (Input in, T t, State& state)
{
    return (state = match (in, t)).passed ();
}

//------------------------------------------------------------------------------
//
// peek
//

// Returns the state
template <class T>
State peek (Input in, T& t)
{
    return Peek <T>::func (in, t);
}

// Stores the state in the argument and returns true if its a pass
template <class T>
bool peek (Input in, T& t, State& state)
{
    return (state = peek (in, t)).passed ();
}

//------------------------------------------------------------------------------
//
// read
//

// Returns the state
template <class T>
State read (Input in, T& t)
{
    return Get <T>::func (in, t);
}

// Stores the state in the argument and returns true if its a pass
template <class T>
bool read (Input in, T& t, State& state)
{
    return (state = read (in, t)).passed ();
}

//------------------------------------------------------------------------------
//
// Specializations for basic types
//

template <>
struct Match <char const*>
{
    static State func (Input in, char const* text)
    {
        return InputParser::match_buffer (in, text, strlen (text));
    }
};

//------------------------------------------------------------------------------
//
// Special types and their specializations
//

struct Digit
{
    int value;
};

template <>
struct Get <Digit>
{
    static State func (Input in, Digit& t)
    {
        char c;
        if (! in.peek (&c))
            return State::more;
        if (! std::isdigit (c))
            return State::fail;
        in.consume (1);
        t.value = c - '0';
        return State::pass;
    }
};

//------------------------------------------------------------------------------

// An unsigned 32 bit number expressed as a string
struct UInt32Str
{
    std::uint32_t value;
};

template <>
struct Get <UInt32Str>
{
    static State func (Input in, UInt32Str& t)
    {
        State state;
        std::uint32_t value (0);

        Digit digit;
        // have to have at least one digit
        if (! read (in, digit, state))
            return state;
        value = digit.value;

        for (;;)
        {
            state = peek (in, digit);

            if (state.failed ())
            {
                t.value = value;
                return State::pass;
            }
            else if (state.eof ())
            {
                t.value = value;
                return state;
            }

            // can't have a digit following a zero
            if (value == 0)
                return State::fail;

            std::uint32_t newValue = (value * 10) + digit.value;

            // overflow
            if (newValue < value)
                return State::fail;

            value = newValue;
        }

        return State::fail;
    }
};

//------------------------------------------------------------------------------

// An unsigned 16 bit number expressed as a string
struct UInt16Str
{
    std::uint16_t value;
};

template <>
struct Get <UInt16Str>
{
    static State func (Input in, UInt16Str& t)
    {
        UInt32Str v;
        State state = read (in, v);
        if (state.passed ())
        {
            if (v.value <= 65535)
            {
                t.value = std::uint16_t(v.value);
                return State::pass;
            }
            return State::fail;
        }
        return state;
    }
};

//------------------------------------------------------------------------------

// An unsigned 8 bit number expressed as a string
struct UInt8Str
{
    std::uint8_t value;
};

template <>
struct Get <UInt8Str>
{
    static State func (Input in, UInt8Str& t)
    {
        UInt32Str v;
        State state = read (in, v);
        if (state.passed ())
        {
            if (v.value <= 255)
            {
                t.value = std::uint8_t(v.value);
                return State::pass;
            }
            return State::fail;
        }
        return state;
    }
};

//------------------------------------------------------------------------------

// An dotted IPv4 address
struct IPv4Address
{
    std::uint8_t value [4];

    String toString () const
    {
        return String::fromNumber <int> (value [0]) + "." +
               String::fromNumber <int> (value [1]) + "." +
               String::fromNumber <int> (value [2]) + "." +
               String::fromNumber <int> (value [3]);
    }
};

template <>
struct Get <IPv4Address>
{
    static State func (Input in, IPv4Address& t)
    {
        State state;
        UInt8Str digit [4];
        if (! read (in, digit [0], state))
            return state;
        if (! match (in, ".", state))
            return state;
        if (! read (in, digit [1], state))
            return state;
        if (! match (in, ".", state))
            return state;
        if (! read (in, digit [2], state))
            return state;
        if (! match (in, ".", state))
            return state;
        if (! read (in, digit [3], state))
            return state;

        t.value [0] = digit [0].value;
        t.value [1] = digit [1].value;
        t.value [2] = digit [2].value;
        t.value [3] = digit [3].value;

        return State::pass;
    }
};

}

}
}

#endif
