#include <xrpld/rpc/detail/AccountAssets.h>

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

    if (auto const lines = lrCache->getRippleLines(account, LineDirection::outgoing))
    {
        for (auto const& rspEntry : *lines)
        {
            auto& saBalance = rspEntry.getBalance();

            // Filter out non
            if (saBalance > beast::zero
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

    if (auto const lines = lrCache->getRippleLines(account, LineDirection::outgoing))
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
