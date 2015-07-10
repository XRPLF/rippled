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
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/core/Config.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/JsonFields.h>

namespace ripple {

// TODO REMOVE!
std::uint64_t mulDiv (std::uint64_t value,
    std::uint32_t mul, std::uint64_t div)
{
    static std::uint64_t boundary = (0x00000000FFFFFFFF);

    if (value > boundary)                           // Large value, avoid overflow
        return (value / div) * mul;
    else                                            // Normal value, preserve accuracy
        return (value * mul) / div;
}

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
    std::uint32_t referenceFeeUnits, bool bAdmin) const
{
    static std::uint64_t midrange (0x00000000FFFFFFFF);

    bool big = (fee > midrange);

    if (big)                // big fee, divide first to avoid overflow
        fee /= referenceFeeUnits;
    else                    // normal fee, multiply first for accuracy
        fee *= baseFee;

    std::uint32_t feeFactor = std::max (mLocalTxnLoadFee, mRemoteTxnLoadFee);

    // Let admins pay the normal fee until the local load exceeds four times the remote
    std::uint32_t uRemFee = std::max(mRemoteTxnLoadFee, mClusterTxnLoadFee);
    if (bAdmin && (feeFactor > uRemFee) && (feeFactor < (4 * uRemFee)))
        feeFactor = uRemFee;

    {
        ScopedLockType sl (mLock);
        fee = mulDiv (fee, feeFactor, lftNormalFee);
    }

    if (big)                // Fee was big to start, must now multiply
        fee *= baseFee;
    else                    // Fee was small to start, mst now divide
        fee /= referenceFeeUnits;

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
