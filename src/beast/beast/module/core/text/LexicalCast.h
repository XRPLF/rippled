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

#ifndef BEAST_LEXICALCAST_H_INCLUDED
#define BEAST_LEXICALCAST_H_INCLUDED

#include <beast/Config.h>
#include <beast/cxx14/type_traits.h> // <type_traits>

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>

namespace beast {

namespace detail {

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4800)
#pragma warning(disable: 4804)
#endif

template <class Int, class FwdIt, class Accumulator>
bool
parse_integral (Int& num, FwdIt first, FwdIt last, Accumulator accumulator)
{
    num = 0;

    if (first == last)
        return false;
    while (first != last)
    {
        auto const c = *first++;
        if (c < '0' || c > '9')
            return false;
        if (!accumulator(Int(c - '0')))
            return false;
    }
    return true;
}

template <class Int, class FwdIt>
bool
parse_negative_integral (Int& num, FwdIt first, FwdIt last)
{
    Int const limit = std::numeric_limits <Int>::min();

    return parse_integral<Int> (num, first, last,
        [&num,&limit](Int n)
        {
            if ((limit - (10 * num)) > -n)
                return false;
            num = 10 * num - n;
            return true;
        });
}

template <class Int, class FwdIt>
bool
parse_positive_integral (Int& num, FwdIt first, FwdIt last)
{
    Int const limit = std::numeric_limits <Int>::max();

    return parse_integral<Int> (num, first, last,
        [&num,&limit](Int n)
        {
            if (n > (limit - (10 * num)))
                return false;
            num = 10 * num + n;
            return true;
        });
}

template <class IntType, class FwdIt>
bool
parseSigned (IntType& result, FwdIt first, FwdIt last)
{
    static_assert(std::is_signed<IntType>::value,
        "You may only call parseSigned with a signed integral type.");

    if (first != last && *first == '-')
        return parse_negative_integral (result, first + 1, last);

    if (first != last && *first == '+')
        return parse_positive_integral (result, first + 1, last);

    return parse_positive_integral (result, first, last);
}

template <class UIntType, class FwdIt>
bool
parseUnsigned (UIntType& result, FwdIt first, FwdIt last)
{
    static_assert(std::is_unsigned<UIntType>::value,
        "You may only call parseUnsigned with an unsigned integral type.");

    if (first != last && *first == '+')
        return parse_positive_integral (result, first + 1, last);

    return parse_positive_integral (result, first, last);
}

//------------------------------------------------------------------------------

// These specializatons get called by the non-member functions to do the work
template <class Out, class In>
struct LexicalCast;

// conversion to std::string
template <class In>
struct LexicalCast <std::string, In>
{
    bool operator() (std::string& out, short in)                { out = std::to_string(in); return true; }
    bool operator() (std::string& out, int in)                  { out = std::to_string(in); return true; }
    bool operator() (std::string& out, long in)                 { out = std::to_string(in); return true; }
    bool operator() (std::string& out, long long in)            { out = std::to_string(in); return true; }
    bool operator() (std::string& out, unsigned short in)       { out = std::to_string(in); return true; }
    bool operator() (std::string& out, unsigned int in)         { out = std::to_string(in); return true; }
    bool operator() (std::string& out, unsigned long in)        { out = std::to_string(in); return true; }
    bool operator() (std::string& out, unsigned long long in)   { out = std::to_string(in); return true; }
    bool operator() (std::string& out, float in)                { out = std::to_string(in); return true; }
    bool operator() (std::string& out, double in)               { out = std::to_string(in); return true; }
    bool operator() (std::string& out, long double in)          { out = std::to_string(in); return true; }
};

// Parse std::string to number
template <class Out>
struct LexicalCast <Out, std::string>
{
    static_assert (std::is_integral <Out>::value,
        "beast::LexicalCast can only be used with integral types");

    template <class Integral = Out>
    typename std::enable_if <std::is_unsigned <Integral>::value, bool>::type
    operator () (Integral& out, std::string const& in) const
    {
        return parseUnsigned (out, std::begin(in), std::end(in));
    }

    template <class Integral = Out>
    typename std::enable_if <std::is_signed <Integral>::value, bool>::type
    operator () (Integral& out, std::string const& in) const
    {
        return parseSigned (out, std::begin(in), std::end(in));
    }
};

#if 0
template <class Out>
bool
LexicalCast <Out, std::string>::operator() (bool& out, std::string const& in) const
{
    // boost::lexical_cast is very strict, it
    // throws on anything but "1" or "0"
    //
    if (in == "1")
    {
        out = true;
        return true;
    }
    else if (in == "0")
    {
        out = false;
        return true;
    }

    return false;
}
#endif

//------------------------------------------------------------------------------

// Conversion from null terminated char const*
template <class Out>
struct LexicalCast <Out, char const*>
{
    bool operator() (Out& out, char const* in) const
    {
        return LexicalCast <Out, std::string>()(out, in);
    }
};

// Conversion from null terminated char*
// The string is not modified.
template <class Out>
struct LexicalCast <Out, char*>
{
    bool operator() (Out& out, char* in) const
    {
        Out result;

        if (LexicalCast <Out, char const*> () (result, in))
        {
            out = result;
            return true;
        }

        return false;
    }
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

} // detail

//------------------------------------------------------------------------------

/** Thrown when a conversion is not possible with LexicalCast.
    Only used in the throw variants of lexicalCast.
*/
struct BadLexicalCast : public std::bad_cast
{
};

/** Intelligently convert from one type to another.
    @return `false` if there was a parsing or range error
*/
template <class Out, class In>
bool lexicalCastChecked (Out& out, In in)
{
    return detail::LexicalCast <Out, In> () (out, in);
}

/** Convert from one type to another, throw on error

    An exception of type BadLexicalCast is thrown if the conversion fails.

    @return The new type.
*/
template <class Out, class In>
Out lexicalCastThrow (In in)
{
    Out out;

    if (lexicalCastChecked (out, in))
        return out;

    throw BadLexicalCast ();
}

/** Convert from one type to another.

    @param defaultValue The value returned if parsing fails
    @return The new type.
*/
template <class Out, class In>
Out lexicalCast (In in, Out defaultValue = Out ())
{
    Out out;

    if (lexicalCastChecked (out, in))
        return out;

    return defaultValue;
}

} // beast

#endif
