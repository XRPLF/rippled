//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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

#ifndef RIPPLE_JSON_JSON_CONVERSIONS_H_INCLUDED
#define RIPPLE_JSON_JSON_CONVERSIONS_H_INCLUDED

#include <ripple/basics/safe_cast.h>
#include <ripple/basics/tagged_integer.h>

namespace ripple
{
// Forward declaration - defined in ReadView.h
struct DropsTag;
}

namespace Json
{

namespace detail
{

template <class Integer>
UInt
maybe_toUInt(ripple::tagged_integer<Integer, ripple::DropsTag> drops,
    std::false_type)
{
    using namespace ripple;
    return unsafe_cast<UInt>(drops.value());
}

template <class Integer>
UInt
maybe_toUInt(ripple::tagged_integer<Integer, ripple::DropsTag> drops,
    std::true_type)
{
    using namespace ripple;
    return safe_cast<UInt>(drops.value());
}

}

template<class Integer,
    class = typename std::enable_if<
        std::is_integral<Integer>::value >::type>
UInt
toUInt(ripple::tagged_integer<Integer, ripple::DropsTag> drops)
{
    return detail::maybe_toUInt(drops, std::integral_constant<bool,
        sizeof(Integer) <= sizeof(UInt)>{});
}

} // namespace Json


#endif // RIPPLE_JSON_JSON_CONVERSIONS_H_INCLUDED
