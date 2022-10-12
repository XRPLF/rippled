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

#include <ripple/protocol/AccountID.h>
#include <cstdint>

namespace ripple {

/** Maintains multiPath_ flag for the payment engine for one-path optimization.
 * Maintains counters of amm offers executed at a payment engine iteration
 * and the number of iterations that include AMM offers.
 * Only one instance of this class is created in Flow.cpp::flow().
 * The reference is percolated through calls to AMMLiquidity class,
 * which handles AMM offer generation.
 */
class AMMOfferCounter
{
private:
    // Tx account owner is required to get the AMM trading fee in BookStep
    AccountID account_;
    // true if payment has multiple paths
    bool multiPath_{false};
    // Counter of consumed AMM at payment engine iteration
    std::uint16_t ammCounter_{0};
    // Counter of payment engine iterations with consumed AMM
    std::uint16_t ammIters_{0};

public:
    AMMOfferCounter(AccountID const& account, bool multiPath)
        : account_(account), multiPath_(multiPath)
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
    setMultiPath(bool fs)
    {
        multiPath_ = fs;
    }

    void
    incrementCounter()
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

    std::uint16_t
    curIters() const
    {
        return ammIters_;
    }

    AccountID
    account() const
    {
        return account_;
    }
};

}  // namespace ripple

#endif  // RIPPLE_APP_PATHS_IMPL_AMMOFFERCOUNTER_H_INCLUDED
