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

#include <ripple/app/main/Application.h>
#include <ripple/core/TimeKeeper.h>
#include <ripple/nodestore/ShardInfo.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/digest.h>

namespace ripple {
namespace NodeStore {

std::string
ShardInfo::finalizedToString() const
{
    if (!finalized_.empty())
        return ripple::to_string(finalized_);
    return {};
}

std::string
ShardInfo::incompleteToString() const
{
    std::string result;
    if (!incomplete_.empty())
    {
        for (auto const& [shardIndex, incomplete] : incomplete_)
        {
            result += std::to_string(shardIndex) + ":" +
                std::to_string(incomplete.percentProgress()) + ",";
        }
        result.pop_back();
    }

    return result;
}

bool
ShardInfo::update(
    std::uint32_t shardIndex,
    ShardState state,
    std::uint32_t percentProgress)
{
    if (state == ShardState::finalized)
    {
        if (boost::icl::contains(finalized_, shardIndex))
            return false;

        finalized_.insert(shardIndex);
        return true;
    }

    return incomplete_.emplace(shardIndex, Incomplete(state, percentProgress))
        .second;
}

protocol::TMPeerShardInfoV2
ShardInfo::makeMessage(Application& app)
{
    protocol::TMPeerShardInfoV2 message;
    Serializer s;
    s.add32(HashPrefix::shardInfo);

    // Set the message creation time
    msgTimestamp_ = app.timeKeeper().now();
    {
        auto const timestamp{msgTimestamp_.time_since_epoch().count()};
        message.set_timestamp(timestamp);
        s.add32(timestamp);
    }

    if (!incomplete_.empty())
    {
        message.mutable_incomplete()->Reserve(incomplete_.size());
        for (auto const& [shardIndex, incomplete] : incomplete_)
        {
            auto tmIncomplete{message.add_incomplete()};

            tmIncomplete->set_shardindex(shardIndex);
            s.add32(shardIndex);

            static_assert(std::is_same_v<
                          std::underlying_type_t<decltype(incomplete.state())>,
                          std::uint32_t>);
            auto const state{static_cast<std::uint32_t>(incomplete.state())};
            tmIncomplete->set_state(state);
            s.add32(state);

            // Set progress if greater than zero
            auto const percentProgress{incomplete.percentProgress()};
            if (percentProgress > 0)
            {
                tmIncomplete->set_progress(percentProgress);
                s.add32(percentProgress);
            }
        }
    }

    if (!finalized_.empty())
    {
        auto const str{ripple::to_string(finalized_)};
        message.set_finalized(str);
        s.addRaw(str.data(), str.size());
    }

    // Set the public key
    auto const& publicKey{app.nodeIdentity().first};
    message.set_publickey(publicKey.data(), publicKey.size());

    // Create a digital signature using the node private key
    auto const signature{sign(publicKey, app.nodeIdentity().second, s.slice())};

    // Set the digital signature
    message.set_signature(signature.data(), signature.size());

    return message;
}

}  // namespace NodeStore
}  // namespace ripple
