//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_DETAIL_CI_CHAR_TRAITS_HPP
#define BEAST_DETAIL_CI_CHAR_TRAITS_HPP

#include <boost/range/algorithm/equal.hpp>
#include <boost/utility/string_ref.hpp>
#include <array>
#include <cstdint>

namespace beast {
namespace detail {

inline
char
tolower(char c)
{
    static std::array<std::uint8_t, 256> constexpr tab = {{
          0,   1,   2,   3,    4,   5,   6,   7,    8,   9,  10,  11,   12,  13,  14,  15,
         16,  17,  18,  19,   20,  21,  22,  23,   24,  25,  26,  27,   28,  29,  30,  31,
         32,  33,  34,  35,   36,  37,  38,  39,   40,  41,  42,  43,   44,  45,  46,  47,
         48,  49,  50,  51,   52,  53,  54,  55,   56,  57,  58,  59,   60,  61,  62,  63,
         64,  97,  98,  99,  100, 101, 102, 103,  104, 105, 106, 107,  108, 109, 110, 111,
        112, 113, 114, 115,  116, 117, 118, 119,  120, 121, 122,  91,   92,  93,  94,  95,
         96,  97,  98,  99,  100, 101, 102, 103,  104, 105, 106, 107,  108, 109, 110, 111,
        112, 113, 114, 115,  116, 117, 118, 119,  120, 121, 122, 123,  124, 125, 126, 127,
        128, 129, 130, 131,  132, 133, 134, 135,  136, 137, 138, 139,  140, 141, 142, 143,
        144, 145, 146, 147,  148, 149, 150, 151,  152, 153, 154, 155,  156, 157, 158, 159,
        160, 161, 162, 163,  164, 165, 166, 167,  168, 169, 170, 171,  172, 173, 174, 175,
        176, 177, 178, 179,  180, 181, 182, 183,  184, 185, 186, 187,  188, 189, 190, 191,
        192, 193, 194, 195,  196, 197, 198, 199,  200, 201, 202, 203,  204, 205, 206, 207,
        208, 209, 210, 211,  212, 213, 214, 215,  216, 217, 218, 219,  220, 221, 222, 223,
        224, 225, 226, 227,  228, 229, 230, 231,  232, 233, 234, 235,  236, 237, 238, 239,
        240, 241, 242, 243,  244, 245, 246, 247,  248, 249, 250, 251,  252, 253, 254, 255
    }};
    return static_cast<char>(tab[static_cast<std::uint8_t>(c)]);
}

template<std::size_t N>
inline
boost::string_ref
string_helper(const char (&s)[N])
{
    return boost::string_ref{s, N-1};
}

template<class T>
inline
T const&
string_helper(T const& t)
{
    return t;
}

// Case-insensitive less
struct ci_less
{
    static bool const is_transparent = true;

    template<class S1, class S2>
    bool
    operator()(S1 const& lhs, S2 const& rhs) const noexcept
    {
        using std::begin;
        using std::end;
        auto const s1 = string_helper(lhs);
        auto const s2 = string_helper(rhs);
        return std::lexicographical_compare(
            begin(s1), end(s1), begin(s2), end(s2),
            [](char lhs, char rhs)
            {
                return tolower(lhs) < tolower(rhs);
            }
        );
    }
};

// Case-insensitive equal
struct ci_equal_pred
{
    bool
    operator()(char c1, char c2) const noexcept
    {
        return tolower(c1) == tolower(c2);
    }
};

// Case-insensitive equal
template<class S1, class S2>
bool
ci_equal(S1 const& lhs, S2 const& rhs)
{
    return boost::range::equal(
        string_helper(lhs), string_helper(rhs),
            ci_equal_pred{});
}

} // detail
} // beast

#endif
