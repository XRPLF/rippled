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

#include <ripple/basics/contract.h>
#include <ripple/ledger/CachedView.h>
#include <ripple/protocol/Serializer.h>

namespace ripple {
namespace detail {

bool
CachedViewImpl::exists(Keylet const& k) const
{
    return read(k) != nullptr;
}

std::shared_ptr<SLE const>
CachedViewImpl::read(Keylet const& k) const
{
    static CountedObjects::Counter hits{"CachedView::hit"};
    static CountedObjects::Counter hitsexpired{"CachedView::hitExpired"};
    static CountedObjects::Counter misses{"CachedView::miss"};
    bool cacheHit = false;
    bool baseRead = false;

    auto const digest = [&]() -> std::optional<uint256> {
        {
            std::lock_guard lock(mutex_);
            auto const iter = map_.find(k.key);
            if (iter != map_.end())
            {
                cacheHit = true;
                return iter->second;
            }
        }
        return base_.digest(k.key);
    }();
    if (!digest)
        return nullptr;
    auto sle = cache_.fetch(*digest, [&]() {
        baseRead = true;
        return base_.read(k);
    });
    if (cacheHit && baseRead)
        hitsexpired.increment();
    else if (cacheHit)
        hits.increment();
    else
        misses.increment();
    std::lock_guard lock(mutex_);
    auto const er = map_.emplace(k.key, *digest);
    bool const inserted = er.second;
    if (sle && !k.check(*sle))
    {
        if (!inserted)
        {
            // On entry, this function did not find this key in map_. Now
            // something (another thread?) has inserted the sle into the map and
            // it has the wrong type.
            LogicError("CachedView::read: wrong type");
        }
        return nullptr;
    }
    return sle;
}

}  // namespace detail
}  // namespace ripple
