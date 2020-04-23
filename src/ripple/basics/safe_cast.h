//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

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

#ifndef RIPPLE_BASICS_SAFE_CAST_H_INCLUDED
#define RIPPLE_BASICS_SAFE_CAST_H_INCLUDED

#include <ripple/beast/cxx17/type_traits.h>
#include <type_traits>

namespace ripple {

// safe_cast adds compile-time checks to a static_cast to ensure that
// the destination can hold all values of the source.  This is particularly
// handy when the source or destination is an enumeration type.

template <class Dest, class Src>
static constexpr bool is_safetocasttovalue_v =
    (std::is_integral_v<Src> && std::is_integral_v<Dest>)&&(
        std::is_signed<Src>::value || std::is_unsigned<Dest>::value) &&
    (std::is_signed<Src>::value != std::is_signed<Dest>::value
         ? sizeof(Dest) > sizeof(Src)
         : sizeof(Dest) >= sizeof(Src));

template <class Dest, class Src>
inline constexpr std::
    enable_if_t<std::is_integral_v<Dest> && std::is_integral_v<Src>, Dest>
    safe_cast(Src s) noexcept
{
    static_assert(
        std::is_signed_v<Dest> || std::is_unsigned_v<Src>,
        "Cannot cast signed to unsigned");
    constexpr unsigned not_same =
        std::is_signed_v<Dest> != std::is_signed_v<Src>;
    static_assert(
        sizeof(Dest) >= sizeof(Src) + not_same,
        "Destination is too small to hold all values of source");
    return static_cast<Dest>(s);
}

template <class Dest, class Src>
inline constexpr std::
    enable_if_t<std::is_enum_v<Dest> && std::is_integral_v<Src>, Dest>
    safe_cast(Src s) noexcept
{
    return static_cast<Dest>(safe_cast<std::underlying_type_t<Dest>>(s));
}

template <class Dest, class Src>
inline constexpr std::
    enable_if_t<std::is_integral_v<Dest> && std::is_enum_v<Src>, Dest>
    safe_cast(Src s) noexcept
{
    return safe_cast<Dest>(static_cast<std::underlying_type_t<Src>>(s));
}

// unsafe_cast explicitly flags a static_cast as not necessarily able to hold
// all values of the source. It includes a compile-time check so that if
// underlying types become safe, it can be converted to a safe_cast.

template <class Dest, class Src>
inline constexpr std::
    enable_if_t<std::is_integral_v<Dest> && std::is_integral_v<Src>, Dest>
    unsafe_cast(Src s) noexcept
{
    static_assert(
        !is_safetocasttovalue_v<Dest, Src>,
        "Only unsafe if casting signed to unsigned or "
        "destination is too small");
    return static_cast<Dest>(s);
}

template <class Dest, class Src>
inline constexpr std::
    enable_if_t<std::is_enum_v<Dest> && std::is_integral_v<Src>, Dest>
    unsafe_cast(Src s) noexcept
{
    return static_cast<Dest>(unsafe_cast<std::underlying_type_t<Dest>>(s));
}

template <class Dest, class Src>
inline constexpr std::
    enable_if_t<std::is_integral_v<Dest> && std::is_enum_v<Src>, Dest>
    unsafe_cast(Src s) noexcept
{
    return unsafe_cast<Dest>(static_cast<std::underlying_type_t<Src>>(s));
}

}  // namespace ripple

#endif
