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

#ifndef BEAST_NET_DETAIL_PARSE_H_INCLUDED
#define BEAST_NET_DETAIL_PARSE_H_INCLUDED

#include <ios>
#include <string>

namespace beast {
namespace IP {

namespace detail {

/** Require and consume the specified character from the input.
    @return `true` if the character matched.
*/
template <typename InputStream>
bool expect(InputStream& is, char v)
{
    char c;
    if (is.get(c) && v == c)
        return true;
    is.unget();
    is.setstate (std::ios_base::failbit);
    return false;
}

/** Require and consume whitespace from the input.
    @return `true` if the character matched.
*/
template <typename InputStream>
bool expect_whitespace (InputStream& is)
{
    char c;
    if (is.get(c) && isspace(static_cast<unsigned char>(c)))
        return true;
    is.unget();
    is.setstate (std::ios_base::failbit);
    return false;
}

/** Used to disambiguate 8-bit integers from characters. */
template <typename IntType>
struct integer_holder
{
    IntType* pi;
    explicit integer_holder (IntType& i)
        : pi (&i)
    {
    }
    template <typename OtherIntType>
    IntType& operator= (OtherIntType o) const
    {
        *pi = o;
        return *pi;
    }
};

/** Parse 8-bit unsigned integer. */
template <typename InputStream>
InputStream& operator>> (InputStream& is, integer_holder <std::uint8_t> const& i)
{
    std::uint16_t v;
    is >> v;
    if (! (v>=0 && v<=255))
    {
        is.setstate (std::ios_base::failbit);
        return is;
    }
    i = std::uint8_t(v);
    return is;
}

/** Free function for template argument deduction. */
template <typename IntType>
integer_holder <IntType> integer (IntType& i)
{
    return integer_holder <IntType> (i);
}

}

}
}

#endif
