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

#include <string>
#include <beast/cxx14/type_traits.h> // <type_traits>

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <utility>

namespace beast {

namespace detail {

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4800)
#pragma warning(disable: 4804)
#endif

template <class IntType>
bool
parseSigned (IntType& result, char const* begin, char const* end)
{
    static_assert(std::is_signed<IntType>::value, "");
    char* ptr;
    auto errno_save = errno;
    errno = 0;
    long long r = std::strtoll(begin, &ptr, 10);
    std::swap(errno, errno_save);
    errno_save = ptr != end;
    if (errno_save == 0)
    {
        if (std::numeric_limits<IntType>::min() <= r &&
            r <= std::numeric_limits<IntType>::max())
            result = static_cast<IntType>(r);
        else
            errno_save = 1;
    }
    return errno_save == 0;
}

template <class UIntType>
bool
parseUnsigned (UIntType& result, char const* begin, char const* end)
{
    static_assert(std::is_unsigned<UIntType>::value, "");
    char* ptr;
    auto errno_save = errno;
    errno = 0;
    unsigned long long r = std::strtoull(begin, &ptr, 10);
    std::swap(errno, errno_save);
    errno_save = ptr != end;
    if (errno_save == 0)
    {
        if (r <= std::numeric_limits<UIntType>::max())
            result = static_cast<UIntType>(r);
        else
            errno_save = 1;
    }
    return errno_save == 0;
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
    bool operator() (short& out,                std::string const& in) const { return parseSigned (out, in.data(), in.data()+in.size()); }
    bool operator() (int& out,                  std::string const& in) const { return parseSigned (out, in.data(), in.data()+in.size()); }
    bool operator() (long& out,                 std::string const& in) const { return parseSigned (out, in.data(), in.data()+in.size()); }
    bool operator() (long long& out,            std::string const& in) const { return parseSigned (out, in.data(), in.data()+in.size()); }
    bool operator() (unsigned short& out,       std::string const& in) const { return parseUnsigned (out, in.data(), in.data()+in.size()); }
    bool operator() (unsigned int& out,         std::string const& in) const { return parseUnsigned (out, in.data(), in.data()+in.size()); }
    bool operator() (unsigned long& out,        std::string const& in) const { return parseUnsigned (out, in.data(), in.data()+in.size()); }
    bool operator() (unsigned long long& out,   std::string const& in) const { return parseUnsigned (out, in.data(), in.data()+in.size()); }
    bool operator() (float& out,                std::string const& in) const { bassertfalse; return false; /* UNIMPLEMENTED! */ }
    bool operator() (double& out,               std::string const& in) const { bassertfalse; return false; /* UNIMPLEMENTED! */ }
    bool operator() (long double& out,          std::string const& in) const { bassertfalse; return false; /* UNIMPLEMENTED! */ }
#if 0
    bool operator() (bool& out,                 std::string const& in) const;
#else
    bool operator() (bool& out,                 std::string const& in) const { return parseUnsigned (out, in.data(), in.data()+in.size()); }
#endif
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
