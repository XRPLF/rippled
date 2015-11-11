//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2015 Ripple Labs Inc.

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

#include <BeastConfig.h>
#include <ripple/basics/mulDiv.h>
#include <ripple/basics/contract.h>
#include <limits>
#include <stdexcept>
#include <utility>

namespace ripple
{

// compute (value)*(mul)/(div) - avoid overflow but keep precision
std::pair<bool, std::uint64_t>
mulDiv(std::uint64_t value, std::uint64_t mul, std::uint64_t div)
{
    if ((value == 0 || mul == 0) && div != 0)
        return{ true, 0 };
    lowestTerms(value, div);
    lowestTerms(mul, div);

    if (value < mul)
        std::swap(value, mul);
    constexpr std::uint64_t max =
        std::numeric_limits<std::uint64_t>::max();
    const auto limit = max / mul;
    if (value > limit)
    {
        value /= div;
        if (value > limit)
            return{ false, max };
        return{ true, value * mul };
    }
    return{ true, value * mul / div };
}

// compute (value)*(mul)/(div) - avoid overflow but keep precision
std::uint64_t
mulDivThrow(std::uint64_t value, std::uint64_t mul, std::uint64_t div)
{
    auto const result = mulDiv(value, mul, div);
    if(!result.first)
        Throw<std::overflow_error>("mulDiv");
    return result.second;
}


} // ripple
