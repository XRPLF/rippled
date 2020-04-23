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

#include <ripple/ledger/CachedSLEs.h>
#include <vector>

namespace ripple {

void
CachedSLEs::expire()
{
    std::vector<std::shared_ptr<void const>> trash;
    {
        auto const expireTime = map_.clock().now() - timeToLive_;
        std::lock_guard lock(mutex_);
        for (auto iter = map_.chronological.begin();
             iter != map_.chronological.end();
             ++iter)
        {
            if (iter.when() > expireTime)
                break;
            if (iter->second.unique())
            {
                trash.emplace_back(std::move(iter->second));
                iter = map_.erase(iter);
            }
        }
    }
}

double
CachedSLEs::rate() const
{
    std::lock_guard lock(mutex_);
    auto const tot = hit_ + miss_;
    if (tot == 0)
        return 0;
    return double(hit_) / tot;
}

}  // namespace ripple
