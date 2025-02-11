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

#include <xrpld/app/paths/AccountAssets.h>

namespace ripple {

hash_set<PathAsset>
accountSourceAssets(
    AccountID const& account,
    std::shared_ptr<AssetCache> const& lrCache,
    bool includeXRP)
{
    hash_set<PathAsset> assets;

    // YYY Only bother if they are above reserve
    if (includeXRP)
        assets.insert(xrpCurrency());

    if (auto const lines =
            lrCache->getRippleLines(account, LineDirection::outgoing))
    {
        for (auto const& rspEntry : *lines)
        {
            auto& saBalance = rspEntry.getBalance();

            // Filter out non
            if (saBalance > beast::zero
                // Have IOUs to send.
                ||
                (rspEntry.getLimitPeer()
                 // Peer extends credit.
                 && ((-saBalance) < rspEntry.getLimitPeer())))  // Credit left.
            {
                assets.insert(saBalance.get<Issue>().currency);
            }
        }
    }

    assets.erase(badCurrency());

    if (auto const mpts = lrCache->getMPTs(account))
    {
        for (auto const& rspEntry : *mpts)
        {
            if (!rspEntry.isZeroBalance() && !rspEntry.isMaxedOut())
                assets.insert(rspEntry.getMptID());
        }
    }

    return assets;
}

hash_set<PathAsset>
accountDestAssets(
    AccountID const& account,
    std::shared_ptr<AssetCache> const& lrCache,
    bool includeXRP)
{
    hash_set<PathAsset> assets;

    if (includeXRP)
        assets.insert(xrpCurrency());
    // Even if account doesn't exist

    if (auto const lines =
            lrCache->getRippleLines(account, LineDirection::outgoing))
    {
        for (auto const& rspEntry : *lines)
        {
            auto& saBalance = rspEntry.getBalance();

            if (saBalance < rspEntry.getLimit())  // Can take more
                assets.insert(saBalance.get<Issue>().currency);
        }
    }

    assets.erase(badCurrency());

    if (auto const mpts = lrCache->getMPTs(account))
    {
        for (auto const& rspEntry : *mpts)
        {
            if (rspEntry.isZeroBalance() && !rspEntry.isMaxedOut())
                assets.insert(rspEntry.getMptID());
        }
    }

    return assets;
}

}  // namespace ripple
