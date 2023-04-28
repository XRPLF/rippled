//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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

#include <ripple/protocol/Quality.h>
#include <ripple/protocol/QualityFunction.h>

namespace ripple {

QualityFunction::QualityFunction() : m_(0), b_(0)
{
}

QualityFunction::QualityFunction(Quality const& quality)
{
    if (quality.rate() <= beast::zero)
        Throw<std::runtime_error>("QualityFunction invalid initialization.");
    m_ = 0;
    b_ = 1 / quality.rate();
}

QualityFunction::QualityFunction(Amounts const& amounts)
{
    if (amounts.in <= beast::zero || amounts.out <= beast::zero)
        Throw<std::runtime_error>("QualityFunction invalid initialization.");
    m_ = -1 / amounts.in;
    b_ = amounts.out / amounts.in;
}

void
QualityFunction::combineWithNext(QualityFunction const& qf)
{
    if (m_ == 0 && b_ == 0)
    {
        m_ = qf.m_;
        b_ = qf.b_;
    }
    else
    {
        m_ += b_ * qf.m_;
        b_ *= qf.b_;
    }
}

std::optional<Number>
QualityFunction::outFromAvgQ(Quality const& quality)
{
    if (m_ != 0 && quality.rate() != beast::zero)
    {
        auto const out = (b_ - 1 / quality.rate()) / m_;
        if (out <= 0)
            return std::nullopt;
        return out;
    }
    return std::nullopt;
}

}  // namespace ripple