//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2022 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_PATHS_IMPL_AMMOFFERCOUNTER_H_INCLUDED
#define RIPPLE_APP_PATHS_IMPL_AMMOFFERCOUNTER_H_INCLUDED

#include <cstdint>

namespace ripple {

/** Maintains multipath flag for the payment engine for one-path optimization.
 * Maintains counters of amm offers executed at a payment engine iteration
 * and the number of iterations that include AMM offers.
 * Only one instance of this class is created in Flow.cpp::flow().
 * The reference is percolated through calls to AMMLiquidity class,
 * which handles AMM offer generation.
 */
class AMMOfferCounter
{
private:
    bool multiPath_{false};
    mutable std::uint16_t ammCounter_{0};
    std::uint16_t ammIters_{0};

public:
    AMMOfferCounter(bool multiPath) : multiPath_(multiPath)
    {
    }
    ~AMMOfferCounter() = default;
    AMMOfferCounter(AMMOfferCounter const&) = delete;
    AMMOfferCounter&
    operator=(AMMOfferCounter const&) = delete;

    bool
    multiPath() const
    {
        return multiPath_;
    }

    void
    incrementCounter() const
    {
        if (multiPath_)
            ++ammCounter_;
    }

    void
    updateIters()
    {
        if (ammCounter_ > 0)
            ++ammIters_;
        ammCounter_ = 0;
    }

    bool
    maxItersReached() const
    {
        return ammIters_ >= 4;
    }
};

}  // namespace ripple

#endif  // RIPPLE_APP_PATHS_IMPL_AMMOFFERCOUNTER_H_INCLUDED
