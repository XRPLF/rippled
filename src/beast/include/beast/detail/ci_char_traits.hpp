//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_DETAIL_CI_CHAR_TRAITS_HPP
#define BEAST_DETAIL_CI_CHAR_TRAITS_HPP

#include <boost/utility/string_ref.hpp>
#include <algorithm>
#include <type_traits>
#include <cctype>
#include <iterator>
#include <string>
#include <utility>

namespace beast {
namespace detail {

/** Case-insensitive function object for performing less than comparisons. */
struct ci_less
{
    static bool const is_transparent = true;

    bool
    operator()(boost::string_ref const& lhs,
        boost::string_ref const& rhs) const noexcept
    {
        using std::begin;
        using std::end;
        return std::lexicographical_compare(
            begin(lhs), end(lhs), begin(rhs), end(rhs),
            [](char lhs, char rhs)
            {
                return std::tolower(lhs) < std::tolower(rhs);
            }
        );
    }
};

inline
bool
ci_equal(std::pair<const char*, std::size_t> lhs,
         std::pair<const char*, std::size_t> rhs)
{
    if(lhs.second != rhs.second)
        return false;
    return std::equal (lhs.first, lhs.first + lhs.second,
                       rhs.first,
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

/** Returns `true` if strings are case-insensitive equal. */
template <class String1, class String2>
inline
bool
ci_equal(String1 const& lhs, String2 const& rhs)
{
    return ci_equal(view(lhs), view(rhs));
}

} // detail
} // beast

#endif
