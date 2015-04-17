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

#ifndef BEAST_UTILITY_CI_CHAR_TRAITS_H_INCLUDED
#define BEAST_UTILITY_CI_CHAR_TRAITS_H_INCLUDED

#include <beast/cxx14/algorithm.h> // <algorithm>
#include <beast/cxx14/type_traits.h> // <type_traits>
#include <cctype>
#include <iterator>
#include <string>
#include <utility>

namespace beast {

/** Case-insensitive function object for performing less than comparisons. */
struct ci_less
{
    static bool const is_transparent = true;

    template <class String>
    bool
    operator() (String const& lhs, String const& rhs) const
    {
        using std::begin;
        using std::end;
        using char_type = typename String::value_type;
        return std::lexicographical_compare (
            begin(lhs), end(lhs), begin(rhs), end(rhs),
            [] (char_type lhs, char_type rhs)
            {
                return std::tolower(lhs) < std::tolower(rhs);
            }
        );
    }
};

namespace detail {

inline
bool
ci_equal(std::pair<const char*, std::size_t> lhs,
         std::pair<const char*, std::size_t> rhs)
{
    return std::equal (lhs.first, lhs.first + lhs.second,
                       rhs.first, rhs.first + rhs.second,
        [] (char lhs, char rhs)
        {
            return std::tolower(lhs) == std::tolower(rhs);
        }
    );
}

template <size_t N>
inline
std::pair<const char*, std::size_t>
view(const char (&s)[N])
{
    return {s, N-1};
}

inline
std::pair<const char*, std::size_t>
view(std::string const& s)
{
    return {s.data(), s.size()};
}

}

/** Returns `true` if strings are case-insensitive equal. */
template <class String1, class String2>
inline
bool
ci_equal(String1 const& lhs, String2 const& rhs)
{
    using detail::view;
    using detail::ci_equal;
    return ci_equal(view(lhs), view(rhs));
}

}

#endif
