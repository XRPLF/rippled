//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#include <ripple/peerclient/ObjectsRequester.h>

#include <ripple/basics/algorithm.h>
#include <ripple/basics/utility.h>

#include <algorithm>
#include <cassert>
#include <iterator>

namespace ripple {

ObjectsRequester::Clock::duration MINIMUM_TIMEOUT = std::chrono::seconds(1);

void
ObjectsRequester::onReady(MessageScheduler::Courier& courier)
{
    // TODO: Move newPeers to front of the line.
    // TODO: See getPeerWithLedger in PeerImp.cpp.
    auto const& allPeers = courier.allPeers();
    assert(std::is_sorted(
        allPeers.begin(), allPeers.end(), [](auto const& a, auto const& b) {
            return a->id < b->id;
        }));
    assert(std::is_sorted(tried_.begin(), tried_.end()));

    // Customized set difference.
    MetaPeerSet untriedPeers;
    set_diff(
        allPeers.begin(),
        allPeers.end(),
        tried_.begin(),
        tried_.end(),
        std::back_inserter(untriedPeers),
        [](auto const& metaPeer, auto peerId) {
            return (int)metaPeer->id - (int)peerId;
        });

    auto timeout = start_ + timeout_ - Clock::now();
    if (untriedPeers.empty())
    {
        if (timeout < Clock::duration::zero())
        {
            // We're out of time.
            courier.withdraw();
            std::stringstream message;
            message << "exhausted " << tried_.size() << " of "
                    << allPeers.size() << " peers looking for " << digest_;
            return throw_(message.str());
        }
        // We still have time. Sometimes we ask for an object that our peers
        // will have soon but not yet, and they all respond quickly that they
        // don't have it. A common example is the header of a recently
        // endorsed ledger. We should wait for the full timeout before calling
        // it quits.
        // Clamp the timeout to a minimum duration.
        timeout = std::max(MINIMUM_TIMEOUT, timeout);
        untriedPeers = courier.allPeers();
        tried_.clear();
    }

    std::random_device rd;
    std::mt19937 urbg(rd());
    std::shuffle(untriedPeers.begin(), untriedPeers.end(), urbg);
    for (auto const& metaPeer : untriedPeers)
    {
        if (metaPeer->nclosed >= metaPeer->nchannels)
        {
            continue;
        }
        // Sorted insertion.
        auto pos = std::find_if(tried_.begin(), tried_.end(), [&](auto id) {
            return id > metaPeer->id;
        });
        tried_.insert(pos, metaPeer->id);
        if (courier.send(metaPeer, *request_, this, timeout))
        {
            return;
        }
    }
    // Never sent. We will be offered again later.
}

void
ObjectsRequester::onSuccess(
    MessageScheduler::RequestId requestId,
    MessagePtr const& response)
{
    auto response_ =
        std::static_pointer_cast<protocol::TMGetObjectByHash>(response);
    if (response_->objects_size() < 1)
    {
        return schedule();
    }
    assert(response_->objects_size() == 1);
    JLOG(journal_.info()) << name() << " finish";
    return return_(std::move(response_));
}

}  // namespace ripple
