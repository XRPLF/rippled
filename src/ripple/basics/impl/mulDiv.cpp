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
std::uint64_t
mulDiv(std::uint64_t value, std::uint64_t mul, std::uint64_t div)
{
    if ((value == 0 || mul == 0) && div != 0)
        return 0;
    lowestTerms(value, div);
    lowestTerms(mul, div);

    if (value < mul)
        std::swap(value, mul);
    const auto max = std::numeric_limits<std::uint64_t>::max();
    if (value > max / mul)
    {
        value /= div;
        if (value > max / mul)
            Throw<std::overflow_error> ("mulDiv");
        return value * mul;
    }
    return value * mul / div;
}

// compute (value)*(mul)/(div) - avoid overflow but keep precision
std::uint64_t
mulDivNoThrow(std::uint64_t value, std::uint64_t mul, std::uint64_t div)
{
    try
    {
        return mulDiv(value, mul, div);
    }
    catch (std::overflow_error)
    {
        return std::numeric_limits<std::uint64_t>::max();
    }
}

} // ripple
