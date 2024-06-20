//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#include <ripple/protocol/QualityFunction.h>

#include <ripple/protocol/Quality.h>

namespace ripple {

QualityFunction::QualityFunction(
    Quality const& quality,
    QualityFunction::CLOBLikeTag)
    : m_(0), b_(0), quality_(quality)
{
    if (quality.rate() <= beast::zero)
        Throw<std::runtime_error>("QualityFunction quality rate is 0.");
    b_ = 1 / quality.rate();
}

void
QualityFunction::combine(QualityFunction const& qf)
{
    m_ += b_ * qf.m_;
    b_ *= qf.b_;
    if (m_ != 0)
        quality_ = std::nullopt;
}

std::optional<Number>
QualityFunction::outFromAvgQ(Quality const& quality)
{
    if (m_ != 0 && quality.rate() != beast::zero)
    {
        saveNumberRoundMode rm(Number::setround(Number::rounding_mode::upward));
        auto const out = (1 / quality.rate() - b_) / m_;
        if (out <= 0)
            return std::nullopt;
        return out;
    }
    return std::nullopt;
}

}  // namespace ripple
