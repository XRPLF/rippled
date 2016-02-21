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

#include <BeastConfig.h>
#include <ripple/app/paths/RippleLineCache.h>
#include <ripple/ledger/OpenView.h>

namespace ripple {

RippleLineCache::RippleLineCache(
    std::shared_ptr <ReadView const> const& ledger)
{
    // We want the caching that OpenView provides
    // And we need to own a shared_ptr to the input view
    // VFALCO TODO This should be a CachedLedger
    mLedger = std::make_shared<OpenView>(&*ledger, ledger);
}

std::vector<RippleState::pointer> const&
RippleLineCache::getRippleLines (AccountID const& accountID)
{
    AccountKey key (accountID, hasher_ (accountID));

    std::lock_guard <std::mutex> sl (mLock);

    auto it = lines_.emplace (key,
        std::vector<RippleState::pointer>());

    if (it.second)
        it.first->second = getRippleStateItems (
            accountID, *mLedger);

    return it.first->second;
}

} // ripple
