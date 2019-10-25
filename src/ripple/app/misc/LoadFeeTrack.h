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
#include <ripple/beast/utility/Journal.h>
#include <algorithm>
#include <cstdint>
#include <mutex>

namespace ripple {

struct Fees;

/** Manages the current fee schedule.

    The "base" fee is the cost to send a reference transaction under no load,
    expressed in millionths of one XRP.

    The "load" fee is how much the local server currently charges to send a
    reference transaction. This fee fluctuates based on the load of the
    server.
*/
class LoadFeeTrack final
{
public:
    explicit LoadFeeTrack (beast::Journal journal =
        beast::Journal(beast::Journal::getNullSink()))
        : j_ (journal)
        , localTxnLoadFee_ (lftNormalFee)
        , remoteTxnLoadFee_ (lftNormalFee)
        , clusterTxnLoadFee_ (lftNormalFee)
        , raiseCount_ (0)
    {
    }

    ~LoadFeeTrack() = default;

    void setRemoteFee (std::uint32_t f)
    {
        std::lock_guard sl (lock_);
        remoteTxnLoadFee_ = f;
    }

    std::uint32_t getRemoteFee () const
    {
        std::lock_guard sl (lock_);
        return remoteTxnLoadFee_;
    }

    std::uint32_t getLocalFee () const
    {
        std::lock_guard sl (lock_);
        return localTxnLoadFee_;
    }

    std::uint32_t getClusterFee () const
    {
        std::lock_guard sl (lock_);
        return clusterTxnLoadFee_;
    }

    std::uint32_t getLoadBase () const
    {
        return lftNormalFee;
    }

    std::uint32_t getLoadFactor () const
    {
        std::lock_guard sl (lock_);
        return std::max({ clusterTxnLoadFee_, localTxnLoadFee_, remoteTxnLoadFee_ });
    }

    std::pair<std::uint32_t, std::uint32_t>
    getScalingFactors() const
    {
        std::lock_guard sl(lock_);

        return std::make_pair(
            std::max(localTxnLoadFee_, remoteTxnLoadFee_),
            std::max(remoteTxnLoadFee_, clusterTxnLoadFee_));
    }


    void setClusterFee (std::uint32_t fee)
    {
        std::lock_guard sl (lock_);
        clusterTxnLoadFee_ = fee;
    }

    bool raiseLocalFee ();
    bool lowerLocalFee ();

    bool isLoadedLocal () const
    {
        std::lock_guard sl (lock_);
        return (raiseCount_ != 0) || (localTxnLoadFee_ != lftNormalFee);
    }

    bool isLoadedCluster () const
    {
        std::lock_guard sl (lock_);
        return (raiseCount_ != 0) || (localTxnLoadFee_ != lftNormalFee) ||
            (clusterTxnLoadFee_ != lftNormalFee);
    }

private:
    static std::uint32_t constexpr lftNormalFee = 256;        // 256 is the minimum/normal load factor
    static std::uint32_t constexpr lftFeeIncFraction = 4;     // increase fee by 1/4
    static std::uint32_t constexpr lftFeeDecFraction = 4;     // decrease fee by 1/4
    static std::uint32_t constexpr lftFeeMax = lftNormalFee * 1000000;

    beast::Journal const j_;
    std::mutex mutable lock_;

    std::uint32_t localTxnLoadFee_;        // Scale factor, lftNormalFee = normal fee
    std::uint32_t remoteTxnLoadFee_;       // Scale factor, lftNormalFee = normal fee
    std::uint32_t clusterTxnLoadFee_;      // Scale factor, lftNormalFee = normal fee
    std::uint32_t raiseCount_;
};

//------------------------------------------------------------------------------

// Scale using load as well as base rate
std::uint64_t scaleFeeLoad(std::uint64_t fee, LoadFeeTrack const& feeTrack,
    Fees const& fees, bool bUnlimited);

} // ripple

#endif
