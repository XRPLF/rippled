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

#include <BeastConfig.h>
#include <ripple/basics/contract.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/core/Config.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/JsonFields.h>

namespace ripple {

// Scale from fee units to millionths of a ripple
std::uint64_t
LoadFeeTrack::scaleFeeBase (std::uint64_t fee, std::uint64_t baseFee,
    std::uint32_t referenceFeeUnits) const
{
    return mulDiv (fee, baseFee, referenceFeeUnits);
}

// Scale using load as well as base rate
std::uint64_t
LoadFeeTrack::scaleFeeLoad (std::uint64_t fee, std::uint64_t baseFee,
    std::uint32_t referenceFeeUnits, bool bUnlimited) const
{
    if (fee == 0)
        return fee;
    std::uint32_t feeFactor;
    std::uint32_t uRemFee;
    {
        // Collect the fee rates
        std::lock_guard<std::mutex> sl(mLock);
        feeFactor = std::max(mLocalTxnLoadFee, mRemoteTxnLoadFee);
        uRemFee = std::max(mRemoteTxnLoadFee, mClusterTxnLoadFee);
    }
    // Let privileged users pay the normal fee until
    //   the local load exceeds four times the remote.
    if (bUnlimited && (feeFactor > uRemFee) && (feeFactor < (4 * uRemFee)))
        feeFactor = uRemFee;

    // Compute:
    // fee = fee * baseFee * feeFactor / (referenceFeeUnits * lftNormalFee);
    // without overflow, and as accurately as possible

    // The denominator of the fraction we're trying to compute.
    // referenceFeeUnits and lftNormalFee are both 32 bit,
    //  so the multiplication can't overflow.
    auto den = static_cast<std::uint64_t>(referenceFeeUnits)
             * static_cast<std::uint64_t>(lftNormalFee);
    // Reduce fee * baseFee * feeFactor / (referenceFeeUnits * lftNormalFee)
    // to lowest terms.
    lowestTerms(fee, den);
    lowestTerms(baseFee, den);
    lowestTerms(feeFactor, den);

    // fee and baseFee are 64 bit, feeFactor is 32 bit
    // Order fee and baseFee largest first
    if (fee < baseFee)
        std::swap(fee, baseFee);
    // If baseFee * feeFactor overflows, the final result will overflow
    const auto max = std::numeric_limits<std::uint64_t>::max();
    if (baseFee > max / feeFactor)
        Throw<std::overflow_error> ("scaleFeeLoad");
    baseFee *= feeFactor;
    // Reorder fee and baseFee
    if (fee < baseFee)
        std::swap(fee, baseFee);
    // If fee * baseFee / den might overflow...
    if (fee > max / baseFee)
    {
        // Do the division first, on the larger of fee and baseFee
        fee /= den;
        if (fee > max / baseFee)
            Throw<std::overflow_error> ("scaleFeeLoad");
        fee *= baseFee;
    }
    else
    {
        // Otherwise fee * baseFee won't overflow,
        //   so do it prior to the division.
        fee *= baseFee;
        fee /= den;
    }
    return fee;
}

Json::Value
LoadFeeTrack::getJson (std::uint64_t baseFee,
    std::uint32_t referenceFeeUnits) const
{
    Json::Value j (Json::objectValue);

    {
        ScopedLockType sl (mLock);

        // base_fee = The cost to send a "reference" transaction under
        // no load, in millionths of a Ripple
        j[jss::base_fee] = Json::Value::UInt (baseFee);

        // load_fee = The cost to send a "reference" transaction now,
        // in millionths of a Ripple
        j[jss::load_fee] = Json::Value::UInt (
            mulDiv (baseFee, std::max (mLocalTxnLoadFee,
                mRemoteTxnLoadFee), lftNormalFee));
    }

    return j;
}

bool
LoadFeeTrack::raiseLocalFee ()
{
    ScopedLockType sl (mLock);

    if (++raiseCount < 2)
        return false;

    std::uint32_t origFee = mLocalTxnLoadFee;

    // make sure this fee takes effect
    if (mLocalTxnLoadFee < mRemoteTxnLoadFee)
        mLocalTxnLoadFee = mRemoteTxnLoadFee;

    // Increase slowly
    mLocalTxnLoadFee += (mLocalTxnLoadFee / lftFeeIncFraction);

    if (mLocalTxnLoadFee > lftFeeMax)
        mLocalTxnLoadFee = lftFeeMax;

    if (origFee == mLocalTxnLoadFee)
        return false;

    m_journal.debug << "Local load fee raised from " <<
        origFee << " to " << mLocalTxnLoadFee;
    return true;
}

bool
LoadFeeTrack::lowerLocalFee ()
{
    ScopedLockType sl (mLock);
    std::uint32_t origFee = mLocalTxnLoadFee;
    raiseCount = 0;

    // Reduce slowly
    mLocalTxnLoadFee -= (mLocalTxnLoadFee / lftFeeDecFraction );

    if (mLocalTxnLoadFee < lftNormalFee)
        mLocalTxnLoadFee = lftNormalFee;

    if (origFee == mLocalTxnLoadFee)
        return false;

    m_journal.debug << "Local load fee lowered from " <<
        origFee << " to " << mLocalTxnLoadFee;
    return true;
}

} // ripple
