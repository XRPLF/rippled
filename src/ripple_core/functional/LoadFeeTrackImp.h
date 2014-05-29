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

#ifndef RIPPLE_LOADFEETRACKIMP_H_INCLUDED
#define RIPPLE_LOADFEETRACKIMP_H_INCLUDED

#include <ripple/common/jsonrpc_fields.h>

namespace ripple {

class LoadFeeTrackImp : public LoadFeeTrack
{
public:
    explicit LoadFeeTrackImp (beast::Journal journal = beast::Journal())
        : m_journal (journal)
        , mLocalTxnLoadFee (lftNormalFee)
        , mRemoteTxnLoadFee (lftNormalFee)
        , mClusterTxnLoadFee (lftNormalFee)
        , raiseCount (0)
    {
    }

    // Scale using load as well as base rate
    std::uint64_t scaleFeeLoad (std::uint64_t fee, std::uint64_t baseFee, std::uint32_t referenceFeeUnits, bool bAdmin)
    {
        static std::uint64_t midrange (0x00000000FFFFFFFF);

        bool big = (fee > midrange);

        if (big)                // big fee, divide first to avoid overflow
            fee /= baseFee;
        else                    // normal fee, multiply first for accuracy
            fee *= referenceFeeUnits;

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
            fee *= referenceFeeUnits;
        else                    // Fee was small to start, mst now divide
            fee /= baseFee;

        return fee;
    }

    // Scale from fee units to millionths of a ripple
    std::uint64_t scaleFeeBase (std::uint64_t fee, std::uint64_t baseFee, std::uint32_t referenceFeeUnits)
    {
        return mulDiv (fee, referenceFeeUnits, baseFee);
    }

    std::uint32_t getRemoteFee ()
    {
        ScopedLockType sl (mLock);
        return mRemoteTxnLoadFee;
    }

    std::uint32_t getLocalFee ()
    {
        ScopedLockType sl (mLock);
        return mLocalTxnLoadFee;
    }

    std::uint32_t getLoadBase ()
    {
        return lftNormalFee;
    }

    std::uint32_t getLoadFactor ()
    {
        ScopedLockType sl (mLock);
        return std::max(mClusterTxnLoadFee, std::max (mLocalTxnLoadFee, mRemoteTxnLoadFee));
    }

    void setClusterFee (std::uint32_t fee)
    {
        ScopedLockType sl (mLock);
        mClusterTxnLoadFee = fee;
    }

    std::uint32_t getClusterFee ()
    {
        ScopedLockType sl (mLock);
        return mClusterTxnLoadFee;
    }

    bool isLoadedLocal ()
    {
        // VFALCO TODO This could be replaced with a SharedData and
        //             using a read/write lock instead of a critical section.
        //
        //        NOTE This applies to all the locking in this class.
        //
        //
        ScopedLockType sl (mLock);
        return (raiseCount != 0) || (mLocalTxnLoadFee != lftNormalFee);
    }

    bool isLoadedCluster ()
    {
        // VFALCO TODO This could be replaced with a SharedData and
        //             using a read/write lock instead of a critical section.
        //
        //        NOTE This applies to all the locking in this class.
        //
        //
        ScopedLockType sl (mLock);
        return (raiseCount != 0) || (mLocalTxnLoadFee != lftNormalFee) || (mClusterTxnLoadFee != lftNormalFee);
    }

    void setRemoteFee (std::uint32_t f)
    {
        ScopedLockType sl (mLock);
        mRemoteTxnLoadFee = f;
    }

    bool raiseLocalFee ()
    {
        ScopedLockType sl (mLock);

        if (++raiseCount < 2)
            return false;

        std::uint32_t origFee = mLocalTxnLoadFee;

        if (mLocalTxnLoadFee < mRemoteTxnLoadFee) // make sure this fee takes effect
            mLocalTxnLoadFee = mRemoteTxnLoadFee;

        mLocalTxnLoadFee += (mLocalTxnLoadFee / lftFeeIncFraction); // increment by 1/16th

        if (mLocalTxnLoadFee > lftFeeMax)
            mLocalTxnLoadFee = lftFeeMax;

        if (origFee == mLocalTxnLoadFee)
            return false;

        m_journal.debug << "Local load fee raised from " << origFee << " to " << mLocalTxnLoadFee;
        return true;
    }

    bool lowerLocalFee ()
    {
        ScopedLockType sl (mLock);
        std::uint32_t origFee = mLocalTxnLoadFee;
        raiseCount = 0;

        mLocalTxnLoadFee -= (mLocalTxnLoadFee / lftFeeDecFraction ); // reduce by 1/4

        if (mLocalTxnLoadFee < lftNormalFee)
            mLocalTxnLoadFee = lftNormalFee;

        if (origFee == mLocalTxnLoadFee)
            return false;

        m_journal.debug << "Local load fee lowered from " << origFee << " to " << mLocalTxnLoadFee;
        return true;
    }

    Json::Value getJson (std::uint64_t baseFee, std::uint32_t referenceFeeUnits)
    {
        Json::Value j (Json::objectValue);

        {
            ScopedLockType sl (mLock);

            // base_fee = The cost to send a "reference" transaction under no load, in millionths of a Ripple
            j[jss::base_fee] = Json::Value::UInt (baseFee);

            // load_fee = The cost to send a "reference" transaction now, in millionths of a Ripple
            j[jss::load_fee] = Json::Value::UInt (
                                mulDiv (baseFee, std::max (mLocalTxnLoadFee, mRemoteTxnLoadFee), lftNormalFee));
        }

        return j;
    }

private:
    // VFALCO TODO Move this function to some "math utilities" file
    // compute (value)*(mul)/(div) - avoid overflow but keep precision
    std::uint64_t mulDiv (std::uint64_t value, std::uint32_t mul, std::uint64_t div)
    {
        // VFALCO TODO replace with beast::literal64bitUnsigned ()
        //
        static std::uint64_t boundary = (0x00000000FFFFFFFF);

        if (value > boundary)                           // Large value, avoid overflow
            return (value / div) * mul;
        else                                            // Normal value, preserve accuracy
            return (value * mul) / div;
    }

private:
    static const int lftNormalFee = 256;        // 256 is the minimum/normal load factor
    static const int lftFeeIncFraction = 4;     // increase fee by 1/4
    static const int lftFeeDecFraction = 4;     // decrease fee by 1/4
    static const int lftFeeMax = lftNormalFee * 1000000;

    beast::Journal m_journal;
    typedef RippleMutex LockType;
    typedef std::lock_guard <LockType> ScopedLockType;
    LockType mLock;

    std::uint32_t mLocalTxnLoadFee;        // Scale factor, lftNormalFee = normal fee
    std::uint32_t mRemoteTxnLoadFee;       // Scale factor, lftNormalFee = normal fee
    std::uint32_t mClusterTxnLoadFee;      // Scale factor, lftNormalFee = normal fee
    int raiseCount;
};

} // ripple

#endif
