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

#ifndef RIPPLE_CORE_LOADFEETRACK_H_INCLUDED
#define RIPPLE_CORE_LOADFEETRACK_H_INCLUDED

#include <ripple/json/json_value.h>
#include <beast/utility/Journal.h>
#include <cstdint>
#include <mutex>

namespace ripple {

/** Manages the current fee schedule.

    The "base" fee is the cost to send a reference transaction under no load,
    expressed in millionths of one XRP.

    The "load" fee is how much the local server currently charges to send a
    reference transaction. This fee fluctuates based on the load of the
    server.
*/
// VFALCO TODO Rename "load" to "current".
class LoadFeeTrack
{
public:
    explicit LoadFeeTrack (beast::Journal journal = beast::Journal())
        : m_journal (journal)
        , mLocalTxnLoadFee (lftNormalFee)
        , mRemoteTxnLoadFee (lftNormalFee)
        , mClusterTxnLoadFee (lftNormalFee)
        , raiseCount (0)
    {
    }

    virtual ~LoadFeeTrack () { }

    // Scale from fee units to millionths of a ripple
    std::uint64_t scaleFeeBase (std::uint64_t fee, std::uint64_t baseFee,
        std::uint32_t referenceFeeUnits) const;

    // Scale using load as well as base rate
    std::uint64_t scaleFeeLoad (std::uint64_t fee, std::uint64_t baseFee,
        std::uint32_t referenceFeeUnits, bool bUnlimited) const;

    void setRemoteFee (std::uint32_t f)
    {
        ScopedLockType sl (mLock);
        mRemoteTxnLoadFee = f;
    }

    std::uint32_t getRemoteFee () const
    {
        ScopedLockType sl (mLock);
        return mRemoteTxnLoadFee;
    }

    std::uint32_t getLocalFee () const
    {
        ScopedLockType sl (mLock);
        return mLocalTxnLoadFee;
    }

    std::uint32_t getClusterFee () const
    {
        ScopedLockType sl (mLock);
        return mClusterTxnLoadFee;
    }

    std::uint32_t getLoadBase () const
    {
        return lftNormalFee;
    }

    std::uint32_t getLoadFactor () const
    {
        ScopedLockType sl (mLock);
        return std::max({ mClusterTxnLoadFee, mLocalTxnLoadFee, mRemoteTxnLoadFee });
    }


    Json::Value getJson (std::uint64_t baseFee, std::uint32_t referenceFeeUnits) const;

    void setClusterFee (std::uint32_t fee)
    {
        ScopedLockType sl (mLock);
        mClusterTxnLoadFee = fee;
    }

    bool raiseLocalFee ();
    bool lowerLocalFee ();

    bool isLoadedLocal () const
    {
        ScopedLockType sl (mLock);
        return (raiseCount != 0) || (mLocalTxnLoadFee != lftNormalFee);
    }

    bool isLoadedCluster () const
    {
        ScopedLockType sl (mLock);
        return (raiseCount != 0) || (mLocalTxnLoadFee != lftNormalFee) || (mClusterTxnLoadFee != lftNormalFee);
    }

private:
    static const int lftNormalFee = 256;        // 256 is the minimum/normal load factor
    static const int lftFeeIncFraction = 4;     // increase fee by 1/4
    static const int lftFeeDecFraction = 4;     // decrease fee by 1/4
    static const int lftFeeMax = lftNormalFee * 1000000;

    beast::Journal m_journal;
    using LockType = std::mutex;
    using ScopedLockType = std::lock_guard <LockType>;
    LockType mutable mLock;

    std::uint32_t mLocalTxnLoadFee;        // Scale factor, lftNormalFee = normal fee
    std::uint32_t mRemoteTxnLoadFee;       // Scale factor, lftNormalFee = normal fee
    std::uint32_t mClusterTxnLoadFee;      // Scale factor, lftNormalFee = normal fee
    int raiseCount;
};

} // ripple

#endif
