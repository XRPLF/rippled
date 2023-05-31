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

#include <ripple/sync/ReplayLedgers.h>

#include <ripple/basics/utility.h>

#include <functional>
#include <ranges>
#include <stdexcept>

namespace ripple {
namespace sync {

LedgerFuturePtr
ReplayLedgers::start()
{
    JLOG(getter_.journal_.info()) << "ReplayLedgers " << digest_ << " start";
    txSet_ = getter_.peerClient_.getTxSet(ripple::copy(digest_));
    header_ = getter_.peerClient_.getHeader(ripple::copy(digest_));
    return header_->thenv(std::bind_front(&ReplayLedgers::withHeader, this))
        ->then([this](auto const& ledgerf) {
            if (ledgerf->fulfilled())
            {
                return ledgerf;
            }
            JLOG(getter_.journal_.info()) << ledgerf->message();
            return getter_.copy(std::move(digest_));
        });
}

LedgerFuturePtr
ReplayLedgers::withHeader(LedgerHeader const& header)
{
    assert(header.hash == digest_);
    JLOG(getter_.journal_.info())
        << "ReplayLedgers " << digest_ << " withHeader";

    auto const& parentDigest = header.parentHash;
    auto parent = getter_.find(parentDigest);
    if (parent)
    {
        return getter_.replay(
            std::move(digest_),
            std::move(header_),
            std::move(txSet_),
            std::move(parent));
    }

    // TODO: Look for parent in skip list.
    return getter_.peerClient_.getSkipList(ripple::copy(digest_))
        ->thenv(std::bind_front(&ReplayLedgers::withSkipList, this));
}

LedgerFuturePtr
ReplayLedgers::withSkipList(SkipList const& skipList)
{
    if (skipList.empty())
    {
        throw std::runtime_error("empty skip list");
    }

    // The index from where to resume our search.
    auto i = skipList_.size();

    // `skipList` is ordered oldest to newest,
    // but `skipList_` is newest to oldest.
    skipList_.reserve(skipList_.size() + skipList.size());
    std::move(
        skipList.rbegin(), skipList.rend(), std::back_inserter(skipList_));

    LedgerFuturePtr parent;

    auto it = skipList_.begin() + i;
    for (; it != skipList_.end(); ++it)
    {
        auto const& digest = *it;
        if ((parent = getter_.find(digest)))
        {
            JLOG(getter_.journal_.debug())
                << "ReplayLedgers " << digest_ << " found ancestor " << digest;
            break;
        }
    }

    if (!parent)
    {
        if (limit_--)
        {
            return getter_.peerClient_
                .getSkipList(ripple::copy(skipList_.back()))
                ->thenv(std::bind_front(&ReplayLedgers::withSkipList, this));
        }
        throw std::runtime_error("could not find ancestor");
    }

    for (auto rit = std::make_reverse_iterator(it); rit != skipList_.rend();
         ++rit)
    {
        auto digest = *rit;
        parent = getter_.replay(
            std::move(digest),
            getter_.peerClient_.getHeader(ripple::copy(digest)),
            getter_.peerClient_.getTxSet(ripple::copy(digest)),
            std::move(parent));
    }

    return getter_.replay(
        std::move(digest_),
        std::move(header_),
        std::move(txSet_),
        std::move(parent));
}

}  // namespace sync
}  // namespace ripple
