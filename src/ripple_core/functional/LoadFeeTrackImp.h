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

namespace ripple {

class LoadFeeTrackImp : public LoadFeeTrack
{
public:
    explicit LoadFeeTrackImp (beast::Journal journal = beast::Journal())
        : m_journal (journal)
        , mLock (this, "LoadFeeTrackImp", __FILE__, __LINE__)
        , mLocalTxnLoadFee (lftNormalFee)
        , mRemoteTxnLoadFee (lftNormalFee)
        , mClusterTxnLoadFee (lftNormalFee)
        , raiseCount (0)
    {
    }

    // Scale using load as well as base rate
    beast::uint64 scaleFeeLoad (beast::uint64 fee, beast::uint64 baseFee, beast::uint32 referenceFeeUnits, bool bAdmin)
    {
        static beast::uint64 midrange (0x00000000FFFFFFFF);

        bool big = (fee > midrange);

        if (big)                // big fee, divide first to avoid overflow
            fee /= baseFee;
        else                    // normal fee, multiply first for accuracy
            fee *= referenceFeeUnits;

        beast::uint32 feeFactor = std::max (mLocalTxnLoadFee, mRemoteTxnLoadFee);

        // Let admins pay the normal fee until the local load exceeds four times the remote
        beast::uint32 uRemFee = std::max(mRemoteTxnLoadFee, mClusterTxnLoadFee);
        if (bAdmin && (feeFactor > uRemFee) && (feeFactor < (4 * uRemFee)))
            feeFactor = uRemFee;

        {
            ScopedLockType sl (mLock, __FILE__, __LINE__);
            fee = mulDiv (fee, feeFactor, lftNormalFee);
        }

        if (big)                // Fee was big to start, must now multiply
            fee *= referenceFeeUnits;
        else                    // Fee was small to start, mst now divide
            fee /= baseFee;

        return fee;
    }

    // Scale from fee units to millionths of a ripple
    beast::uint64 scaleFeeBase (beast::uint64 fee, beast::uint64 baseFee, beast::uint32 referenceFeeUnits)
    {
        return mulDiv (fee, referenceFeeUnits, baseFee);
    }

    beast::uint32 getRemoteFee ()
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);
        return mRemoteTxnLoadFee;
    }

    beast::uint32 getLocalFee ()
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);
        return mLocalTxnLoadFee;
    }

    beast::uint32 getLoadBase ()
    {
        return lftNormalFee;
    }

    beast::uint32 getLoadFactor ()
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);
        return std::max(mClusterTxnLoadFee, std::max (mLocalTxnLoadFee, mRemoteTxnLoadFee));
    }

    void setClusterFee (beast::uint32 fee)
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);
        mClusterTxnLoadFee = fee;
    }

    beast::uint32 getClusterFee ()
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);
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
        ScopedLockType sl (mLock, __FILE__, __LINE__);
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
        ScopedLockType sl (mLock, __FILE__, __LINE__);
        return (raiseCount != 0) || (mLocalTxnLoadFee != lftNormalFee) || (mClusterTxnLoadFee != lftNormalFee);
    }

    void setRemoteFee (beast::uint32 f)
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);
        mRemoteTxnLoadFee = f;
    }

    bool raiseLocalFee ()
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);

        if (++raiseCount < 2)
            return false;

        beast::uint32 origFee = mLocalTxnLoadFee;

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
        ScopedLockType sl (mLock, __FILE__, __LINE__);
        beast::uint32 origFee = mLocalTxnLoadFee;
        raiseCount = 0;

        mLocalTxnLoadFee -= (mLocalTxnLoadFee / lftFeeDecFraction ); // reduce by 1/4

        if (mLocalTxnLoadFee < lftNormalFee)
            mLocalTxnLoadFee = lftNormalFee;

        if (origFee == mLocalTxnLoadFee)
            return false;

        m_journal.debug << "Local load fee lowered from " << origFee << " to " << mLocalTxnLoadFee;
        return true;
    }

    Json::Value getJson (beast::uint64 baseFee, beast::uint32 referenceFeeUnits)
    {
        Json::Value j (Json::objectValue);

        {
            ScopedLockType sl (mLock, __FILE__, __LINE__);

            // base_fee = The cost to send a "reference" transaction under no load, in millionths of a Ripple
            j["base_fee"] = Json::Value::UInt (baseFee);

            // load_fee = The cost to send a "reference" transaction now, in millionths of a Ripple
            j["load_fee"] = Json::Value::UInt (
                                mulDiv (baseFee, std::max (mLocalTxnLoadFee, mRemoteTxnLoadFee), lftNormalFee));
        }

        return j;
    }

private:
    // VFALCO TODO Move this function to some "math utilities" file
    // compute (value)*(mul)/(div) - avoid overflow but keep precision
    beast::uint64 mulDiv (beast::uint64 value, beast::uint32 mul, beast::uint64 div)
    {
        // VFALCO TODO replace with beast::literal64bitUnsigned ()
        //
        static beast::uint64 boundary = (0x00000000FFFFFFFF);

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
    typedef LockType::ScopedLockType ScopedLockType;
    LockType mLock;

    beast::uint32 mLocalTxnLoadFee;        // Scale factor, lftNormalFee = normal fee
    beast::uint32 mRemoteTxnLoadFee;       // Scale factor, lftNormalFee = normal fee
    beast::uint32 mClusterTxnLoadFee;      // Scale factor, lftNormalFee = normal fee
    int raiseCount;
};

} // ripple

#endif
