//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#ifndef RIPPLE_BASICS_MATHUTILITIES_H_INCLUDED
#define RIPPLE_BASICS_MATHUTILITIES_H_INCLUDED

#include <algorithm>
#include <assert.h>
#include <cstddef>

namespace ripple {

/** Calculate one number divided by another number in percentage.
 * The result is rounded up to the next integer, and capped in the range [0,100]
 * E.g. calculatePercent(1, 100) = 1 because 1/100 = 0.010000
 *      calculatePercent(1, 99) = 2 because 1/99 = 0.010101
 *      calculatePercent(0, 100) = 0
 *      calculatePercent(100, 100) = 100
 *      calculatePercent(200, 100) = 100 because the result is capped to 100
 *
 * @param count  dividend
 * @param total  divisor
 * @return the percentage, in [0, 100]
 *
 * @note total cannot be zero.
 * */
constexpr std::size_t
calculatePercent(std::size_t count, std::size_t total)
{
    assert(total != 0);
    return ((std::min(count, total) * 100) + total - 1) / total;
}
}  // namespace ripple

#endif
