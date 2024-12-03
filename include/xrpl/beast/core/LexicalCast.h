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

#ifndef BEAST_MODULE_CORE_TEXT_LEXICALCAST_H_INCLUDED
#define BEAST_MODULE_CORE_TEXT_LEXICALCAST_H_INCLUDED

#include <xrpl/beast/utility/instrumentation.h>

#include <boost/core/detail/string_view.hpp>
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace beast {

namespace detail {

// These specializatons get called by the non-member functions to do the work
template <class Out, class In>
struct LexicalCast;

// conversion to std::string
template <class In>
struct LexicalCast<std::string, In>
{
    explicit LexicalCast() = default;

    template <class Arithmetic = In>
    std::enable_if_t<std::is_arithmetic_v<Arithmetic>, bool>
    operator()(std::string& out, Arithmetic in)
    {
        out = std::to_string(in);
        return true;
    }

    template <class Enumeration = In>
    std::enable_if_t<std::is_enum_v<Enumeration>, bool>
    operator()(std::string& out, Enumeration in)
    {
        out = std::to_string(
            static_cast<std::underlying_type_t<Enumeration>>(in));
        return true;
    }
};

// Parse a std::string_view into a number
template <typename Out>
struct LexicalCast<Out, std::string_view>
{
    explicit LexicalCast() = default;

    static_assert(
        std::is_integral_v<Out>,
        "beast::LexicalCast can only be used with integral types");

    template <class Integral = Out>
    std::enable_if_t<
        std::is_integral_v<Integral> && !std::is_same_v<Integral, bool>,
        bool>
    operator()(Integral& out, std::string_view in) const
    {
        auto first = in.data();
        auto last = in.data() + in.size();

        if (first != last && *first == '+')
            ++first;

        auto ret = std::from_chars(first, last, out);

        return ret.ec == std::errc() && ret.ptr == last;
    }

    bool
    operator()(bool& out, std::string_view in) const
    {
        std::string result;

        // Convert the input to lowercase
        std::transform(
            in.begin(), in.end(), std::back_inserter(result), [](auto c) {
                return std::tolower(static_cast<unsigned char>(c));
            });

        if (result == "1" || result == "true")
        {
            out = true;
            return true;
        }

        if (result == "0" || result == "false")
        {
            out = false;
            return true;
        }

        return false;
    }
};
//------------------------------------------------------------------------------

// Parse boost library's string_view to number or boolean value
// Note: As of Jan 2024, Boost contains three different types of string_view
// (boost::core::basic_string_view<char>, boost::string_ref and
// boost::string_view). The below template specialization is included because
// it is used in the handshake.cpp file
template <class Out>
struct LexicalCast<Out, boost::core::basic_string_view<char>>
{
    explicit LexicalCast() = default;

    bool
    operator()(Out& out, boost::core::basic_string_view<char> in) const
    {
        return LexicalCast<Out, std::string_view>()(out, in);
    }
};

// Parse std::string to number or boolean value
template <class Out>
struct LexicalCast<Out, std::string>
{
    explicit LexicalCast() = default;

    bool
    operator()(Out& out, std::string in) const
    {
        return LexicalCast<Out, std::string_view>()(out, in);
    }
};

// Conversion from null terminated char const*
template <class Out>
struct LexicalCast<Out, char const*>
{
    explicit LexicalCast() = default;

    bool
    operator()(Out& out, char const* in) const
    {
        ASSERT(
            in != nullptr,
            "beast::detail::LexicalCast(char const*) : non-null input");
        return LexicalCast<Out, std::string_view>()(out, in);
    }
};

// Conversion from null terminated char*
// The string is not modified.
template <class Out>
struct LexicalCast<Out, char*>
{
    explicit LexicalCast() = default;

    bool
    operator()(Out& out, char* in) const
    {
        ASSERT(
            in != nullptr,
            "beast::detail::LexicalCast(char*) : non-null input");
        return LexicalCast<Out, std::string_view>()(out, in);
    }
};

}  // namespace detail

//------------------------------------------------------------------------------

/** Thrown when a conversion is not possible with LexicalCast.
    Only used in the throw variants of lexicalCast.
*/
struct BadLexicalCast : public std::bad_cast
{
    explicit BadLexicalCast() = default;
};

/** Intelligently convert from one type to another.
    @return `false` if there was a parsing or range error
*/
template <class Out, class In>
bool
lexicalCastChecked(Out& out, In in)
{
    return detail::LexicalCast<Out, In>()(out, in);
}

/** Convert from one type to another, throw on error

    An exception of type BadLexicalCast is thrown if the conversion fails.

    @return The new type.
*/
template <class Out, class In>
Out
lexicalCastThrow(In in)
{
    if (Out out; lexicalCastChecked(out, in))
        return out;

    throw BadLexicalCast();
}

/** Convert from one type to another.

    @param defaultValue The value returned if parsing fails
    @return The new type.
*/
template <class Out, class In>
Out
lexicalCast(In in, Out defaultValue = Out())
{
    if (Out out; lexicalCastChecked(out, in))
        return out;

    return defaultValue;
}

}  // namespace beast

#endif
