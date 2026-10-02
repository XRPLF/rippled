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

HashSet<PathAsset>
accountSourceAssets(
    AccountID const& account,
    std::shared_ptr<AssetCache> const& lrCache,
    bool includeXRP)
{
    HashSet<PathAsset> assets;

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
            if (rspEntry.canSend(account))
                assets.insert(rspEntry.getMptID());
        }
    }

    return assets;
}

HashSet<PathAsset>
accountDestAssets(
    AccountID const& account,
    std::shared_ptr<AssetCache> const& lrCache,
    bool includeXRP)
{
    HashSet<PathAsset> assets;

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
            // Any cached MPT entry means this account already has an issuance
            // or MPToken object. A maxed-out issuance does not prevent
            // receiving existing MPT from another holder.
            assets.insert(rspEntry.getMptID());
        }
    }

    return assets;
}

}  // namespace xrpl
