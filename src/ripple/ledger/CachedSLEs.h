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

#ifndef RIPPLE_LEDGER_CACHEDSLES_H_INCLUDED
#define RIPPLE_LEDGER_CACHEDSLES_H_INCLUDED

#include <ripple/basics/TaggedCache.h>
#include <ripple/basics/base_uint.h>
#include <ripple/protocol/STLedgerEntry.h>

namespace ripple {

class CachedSLEs : public TaggedCache<uint256, SLE const>
{
    std::atomic<std::uint64_t> handlerHits_ = 0;
    std::atomic<std::uint64_t> handlerMisses_ = 0;

public:
    using TaggedCache::TaggedCache;

    /** Fetch from the cache; if needed, invoke the handler to load the item. */
    template <class Handler>
    std::shared_ptr<SLE const>
    fetch(uint256 const& digest, Handler const& handler)
    {
        if (auto ret = TaggedCache::fetch(digest))
            return ret;

        if (auto sle = handler(); sle)
        {
            if (retrieve_or_insert(digest, sle))
                handlerHits_++;

            return sle;
        }

        handlerMisses_++;
        return {};
    }

    // Reintroduce the function we just hid.
    using TaggedCache::fetch;

    /** Returns the fraction of cache hits. */
    double
    rate() const
    {
        // TODO
        return 0;
    }

    Json::Value
    info()
    {
        auto ret = TaggedCache::info();
        ret["handler_hits"] = std::to_string(handlerHits_.load());
        ret["handler_misses"] = std::to_string(handlerMisses_.load());
        return ret;
    }
};

}  // namespace ripple

#endif  // RIPPLE_LEDGER_CACHEDSLES_H_INCLUDED
