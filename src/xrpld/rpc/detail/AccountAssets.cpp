#include <xrpld/rpc/detail/AccountAssets.h>

#include <xrpld/rpc/detail/AssetCache.h>
#include <xrpld/rpc/detail/TrustLine.h>

#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/PathAsset.h>
#include <xrpl/protocol/UintTypes.h>

#include <memory>

namespace xrpl {

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

    if (auto const lines = lrCache->getRippleLines(account, LineDirection::Outgoing))
    {
        for (auto const& rspEntry : *lines)
        {
            auto& saBalance = rspEntry.getBalance();

            // Filter out non
            if (saBalance > beast::kZero
                // Have IOUs to send.
                || (rspEntry.getLimitPeer()
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

    if (auto const lines = lrCache->getRippleLines(account, LineDirection::Outgoing))
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

}  // namespace xrpl
