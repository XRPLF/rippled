//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/basics/FeeUnits.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/safe_cast.h>
#include <ripple/core/Config.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/jss.h>

#include <cstdint>
#include <numeric>
#include <type_traits>

namespace ripple {

bool
LoadFeeTrack::raiseLocalFee()
{
    std::lock_guard sl(lock_);

    if (++raiseCount_ < 2)
        return false;

    std::uint32_t origFee = localTxnLoadFee_;

    // make sure this fee takes effect
    if (localTxnLoadFee_ < remoteTxnLoadFee_)
        localTxnLoadFee_ = remoteTxnLoadFee_;

    // Increase slowly
    localTxnLoadFee_ += (localTxnLoadFee_ / lftFeeIncFraction);

    if (localTxnLoadFee_ > lftFeeMax)
        localTxnLoadFee_ = lftFeeMax;

    if (origFee == localTxnLoadFee_)
        return false;

    JLOG(j_.debug()) << "Local load fee raised from " << origFee << " to "
                     << localTxnLoadFee_;
    return true;
}

bool
LoadFeeTrack::lowerLocalFee()
{
    std::lock_guard sl(lock_);
    std::uint32_t origFee = localTxnLoadFee_;
    raiseCount_ = 0;

    // Reduce slowly
    localTxnLoadFee_ -= (localTxnLoadFee_ / lftFeeDecFraction);

    if (localTxnLoadFee_ < lftNormalFee)
        localTxnLoadFee_ = lftNormalFee;

    if (origFee == localTxnLoadFee_)
        return false;

    JLOG(j_.debug()) << "Local load fee lowered from " << origFee << " to "
                     << localTxnLoadFee_;
    return true;
}

//------------------------------------------------------------------------------

// Scale using load as well as base rate
XRPAmount
scaleFeeLoad(
    FeeUnit64 fee,
    LoadFeeTrack const& feeTrack,
    Fees const& fees,
    bool bUnlimited)
{
    if (fee == 0)
        return XRPAmount{0};

    // Normally, types with different units wouldn't be mathematically
    // compatible. This function is an exception.
    auto lowestTerms = [](auto& a, auto& b) {
        auto value = [](auto val) {
            if constexpr (std::is_arithmetic_v<decltype(val)>)
                return val;
            else
                return val.value();
        };

        if (auto const g = std::gcd(value(a), value(b)))
        {
            a = value(a) / g;
            b = value(b) / g;
        }
    };

    // Collect the fee rates
    auto [feeFactor, uRemFee] = feeTrack.getScalingFactors();

    // Let privileged users pay the normal fee until
    //   the local load exceeds four times the remote.
    if (bUnlimited && (feeFactor > uRemFee) && (feeFactor < (4 * uRemFee)))
        feeFactor = uRemFee;

    XRPAmount baseFee{fees.base};
    // Compute:
    // fee = fee * baseFee * feeFactor / (fees.units * lftNormalFee);
    // without overflow, and as accurately as possible

    // The denominator of the fraction we're trying to compute.
    // fees.units and lftNormalFee are both 32 bit,
    //  so the multiplication can't overflow.
    auto den = FeeUnit64{fees.units} *
        safe_cast<std::uint64_t>(feeTrack.getLoadBase());
    // Reduce fee * baseFee * feeFactor / (fees.units * lftNormalFee)
    // to lowest terms.
    lowestTerms(fee, den);
    lowestTerms(baseFee, den);
    lowestTerms(feeFactor, den);

    // fee and baseFee are 64 bit, feeFactor is 32 bit
    // Order fee and baseFee largest first
    // Normally, these types wouldn't be comparable or swappable.
    // This function is an exception.
    if (fee.value() < baseFee.value())
    {
        auto tmp = fee.value();
        fee = baseFee.value();
        baseFee = tmp;
    }
    // double check
    assert(fee.value() >= baseFee.value());

    // If baseFee * feeFactor overflows, the final result will overflow
    XRPAmount const baseFeeOverflow{
        std::numeric_limits<XRPAmount::value_type>::max() / feeFactor};
    if (baseFee > baseFeeOverflow)
    {
        Throw<std::overflow_error>("scaleFeeLoad");
    }
    baseFee *= feeFactor;

    auto const result = mulDiv(fee, baseFee, den);
    if (!result.first)
        Throw<std::overflow_error>("scaleFeeLoad");
    return result.second;
}

}  // namespace ripple
