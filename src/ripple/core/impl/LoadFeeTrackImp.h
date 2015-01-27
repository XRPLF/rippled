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

#ifndef RIPPLE_CORE_LOADFEETRACKIMP_H_INCLUDED
#define RIPPLE_CORE_LOADFEETRACKIMP_H_INCLUDED

#include <ripple/protocol/JsonFields.h>
#include <ripple/core/LoadFeeTrack.h>
#include <beast/module/core/maths/Muldiv.h>
#include <mutex>

namespace ripple {

class LoadFeeTrackImp : public LoadFeeTrack
{
public:
    explicit LoadFeeTrackImp (beast::Journal journal = beast::Journal())
        : m_journal (journal)
        , mLocalLoadLevel (lftReference)
        , mRemoteLoadLevel (lftReference)
        , mClusterLoadLevel (lftReference)
        , raiseCount (0)
    {
    }

    // Scale using load as well as base rate
    std::uint64_t scaleFeeLoad (
        std::uint64_t fee, // The number of fee units you want to scale
        std::uint64_t baseFee, // Cost of reference transaction in drops
        std::uint32_t referenceFeeUnits, // Cost of reference transaction in fee units
        bool bAdmin) const override
    {
        static std::uint64_t midrange (0x00000000FFFFFFFF);

        bool big = (fee > midrange);

        if (big)                // big fee, divide first to avoid overflow
            fee /= referenceFeeUnits;
        else                    // normal fee, multiply first for accuracy
            fee *= baseFee;

        std::uint32_t feeFactor = std::max (mLocalLoadLevel, mRemoteLoadLevel);

        // Let admins pay the normal fee until the local load exceeds four times the remote
        std::uint32_t uRemFee = std::max(mRemoteLoadLevel, mClusterLoadLevel);
        if (bAdmin && (feeFactor > uRemFee) && (feeFactor < (4 * uRemFee)))
            feeFactor = uRemFee;

        {
            ScopedLockType sl (mLock);
            fee = beast::mulDiv (fee, feeFactor, lftReference);
        }

        if (big)                // Fee was big to start, must now multiply
            fee *= baseFee;
        else                    // Fee was small to start, must now divide
            fee /= referenceFeeUnits;

        return fee;
    }

    std::uint32_t getLocalLevel () override
    {
        ScopedLockType sl (mLock);
        return mLocalLoadLevel;
    }

    std::uint32_t getRemoteLevel ()
    {
        ScopedLockType sl (mLock);
        return mRemoteLoadLevel;
    }

    std::uint32_t getLoadBase () const override
    {
        return lftReference;
    }

    std::uint32_t getLoadFactor ()
    {
        ScopedLockType sl (mLock);
        return std::max(mClusterLoadLevel, std::max (mLocalLoadLevel, mRemoteLoadLevel));
    }

    void setClusterLevel (std::uint32_t level)
    {
        ScopedLockType sl (mLock);
        mClusterLoadLevel = level;
    }

    std::uint32_t getClusterLevel ()
    {
        ScopedLockType sl (mLock);
        return mClusterLoadLevel;
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
        return (raiseCount != 0) || (mLocalLoadLevel != lftReference);
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
        return (raiseCount != 0) || (mLocalLoadLevel != lftReference) || (mClusterLoadLevel != lftReference);
    }

    void setRemoteLevel (std::uint32_t f)
    {
        ScopedLockType sl (mLock);
        mRemoteLoadLevel = f;
    }

    bool raiseLocalLevel ()
    {
        ScopedLockType sl (mLock);

        if (++raiseCount < 2)
            return false;

        std::uint32_t origLevel = mLocalLoadLevel;

        if (mLocalLoadLevel < mRemoteLoadLevel)
            mLocalLoadLevel = mRemoteLoadLevel;

        // increase slowly.
        mLocalLoadLevel += (mLocalLoadLevel / lftLevelIncFraction);

        if (mLocalLoadLevel > lftLevelMax)
            mLocalLoadLevel = lftLevelMax;

        if (origLevel == mLocalLoadLevel)
            return false;

        m_journal.debug << "Local load level raised from " <<
            origLevel << " to " << mLocalLoadLevel;
        return true;
    }

    bool lowerLocalLevel () override
    {
        ScopedLockType sl (mLock);
        std::uint32_t origLevel = mLocalLoadLevel;
        raiseCount = 0;

        // reduce slowly.
        mLocalLoadLevel -= (mLocalLoadLevel / lftLevelDecFraction );

        if (mLocalLoadLevel < lftReference)
            mLocalLoadLevel = lftReference;

        if (origLevel == mLocalLoadLevel)
            return false;

        m_journal.debug << "Local load level lowered from " <<
            origLevel << " to " << mLocalLoadLevel;
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
                beast::mulDiv (baseFee, std::max
                    (mLocalLoadLevel, mRemoteLoadLevel),
                        lftReference));
        }

        return j;
    }

private:
    static const int lftReference = 256;        // 256 means normal (DO NOT CHANGE)
    static const int lftExtraFee = 3;           // boost reported fee by 1/3
    static const int lftMinimumTx = 5;          // don't raise fee for first 5 transactions
    static const int lftMinimumTxSA = 100;      // Allow more in standalone mode

    static const int lftLevelIncFraction = 4;     // increase level by 1/4
    static const int lftLevelDecFraction = 4;     // decrease level by 1/4
    static const int lftLevelMax = lftReference * 1000000;

    beast::Journal m_journal;
    using LockType = std::mutex;
    using ScopedLockType = std::lock_guard <LockType>;
    LockType mutable mLock;

    std::uint32_t mLocalLoadLevel;        // Scale factor, lftReference = normal
    std::uint32_t mRemoteLoadLevel;       // Scale factor, lftReference = normal
    std::uint32_t mClusterLoadLevel;      // Scale factor, lftReference = normal
    int raiseCount;
};

} // ripple

#endif
