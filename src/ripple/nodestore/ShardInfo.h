//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#ifndef RIPPLE_NODESTORE_SHARDINFO_H_INCLUDED
#define RIPPLE_NODESTORE_SHARDINFO_H_INCLUDED

#include <ripple/basics/RangeSet.h>
#include <ripple/nodestore/Types.h>
#include <ripple/protocol/messages.h>

namespace ripple {
namespace NodeStore {

/* Contains information on the status of shards for a node
 */
class ShardInfo
{
private:
    class Incomplete
    {
    public:
        Incomplete() = delete;
        Incomplete(ShardState state, std::uint32_t percentProgress)
            : state_(state), percentProgress_(percentProgress)
        {
        }

        [[nodiscard]] ShardState
        state() const noexcept
        {
            return state_;
        }

        [[nodiscard]] std::uint32_t
        percentProgress() const noexcept
        {
            return percentProgress_;
        }

    private:
        ShardState state_;
        std::uint32_t percentProgress_;
    };

public:
    [[nodiscard]] NetClock::time_point const&
    msgTimestamp() const
    {
        return msgTimestamp_;
    }

    void
    setMsgTimestamp(NetClock::time_point const& timestamp)
    {
        msgTimestamp_ = timestamp;
    }

    [[nodiscard]] std::string
    finalizedToString() const;

    [[nodiscard]] bool
    setFinalizedFromString(std::string const& str)
    {
        return from_string(finalized_, str);
    }

    [[nodiscard]] RangeSet<std::uint32_t> const&
    finalized() const
    {
        return finalized_;
    }

    [[nodiscard]] std::string
    incompleteToString() const;

    [[nodiscard]] std::map<std::uint32_t, Incomplete> const&
    incomplete() const
    {
        return incomplete_;
    }

    // Returns true if successful or false because of a duplicate index
    bool
    update(
        std::uint32_t shardIndex,
        ShardState state,
        std::uint32_t percentProgress);

    [[nodiscard]] protocol::TMPeerShardInfoV2
    makeMessage(Application& app);

private:
    // Finalized immutable shards
    RangeSet<std::uint32_t> finalized_;

    // Incomplete shards being acquired or finalized
    std::map<std::uint32_t, Incomplete> incomplete_;

    // Message creation time
    NetClock::time_point msgTimestamp_;
};

}  // namespace NodeStore
}  // namespace ripple

#endif
