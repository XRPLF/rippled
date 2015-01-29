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

#ifndef BEAST_HTTP_DETAIL_HEADER_TRAITS_H_INCLUDED
#define BEAST_HTTP_DETAIL_HEADER_TRAITS_H_INCLUDED

#include <beast/utility/ci_char_traits.h>

#include <boost/utility/string_ref.hpp>

#include <memory>
#include <string>

namespace beast {
namespace http {
namespace detail {

// Utilities for dealing with HTTP headers

template <class Allocator = std::allocator <char>>
using basic_field_string =
    std::basic_string <char, ci_char_traits, Allocator>;

typedef basic_field_string <> field_string;

typedef boost::basic_string_ref <char, ci_char_traits> field_string_ref;

/** Returns `true` if two header fields are the same.
    The comparison is case-insensitive.
*/
template <class Alloc1, class Alloc2>
inline
bool field_eq (
    std::basic_string <char, std::char_traits <char>, Alloc1> const& s1,
    std::basic_string <char, std::char_traits <char>, Alloc2> const& s2)
{
    return field_string_ref (s1.c_str(), s1.size()) ==
           field_string_ref (s2.c_str(), s2.size());
}

/** Returns the string with leading and trailing LWS removed. */

}
}
}

#endif
