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

#ifndef RIPPLE_LEDGER_CACHEDVIEW_H_INCLUDED
#define RIPPLE_LEDGER_CACHEDVIEW_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/ledger/SLECache.h>

namespace ripple {

/** Cache-aware view to a ledger */
class CachedView : public BasicViewWrapper<Ledger&>
{
private:
    SLECache& cache_;

public:
    CachedView(CachedView const&) = delete;
    CachedView& operator=(CachedView const&) = delete;

    /** Wrap a ledger with a cache.
        @note Only ledgers may be wrapped with a cache.
    */
    CachedView (Ledger& ledger,
            SLECache& cache)
        : BasicViewWrapper(ledger)
        , cache_(cache)
    {
    }

    std::shared_ptr<SLE const>
    read (Keylet const& k) const override;
};

} // ripple

#endif
