/** @file
 *  @brief Asset eligibility helpers for the XRPL path-finding subsystem.
 *
 *  Implements `accountSourceAssets` and `accountDestAssets`, which classify
 *  which assets an account can send or receive.  Both functions read from a
 *  shared `AssetCache` (a ledger-snapshot cache) to avoid redundant SLE
 *  lookups during a path-finding session and return a `hash_set<PathAsset>`
 *  that seeds the `Pathfinder` before the expensive graph traversal begins.
 *
 *  See `AccountAssets.h` for full behavioral contracts.
 */

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

            // Include if the account holds a positive balance (IOUs to push)
            // or is a borrower with room left under the peer's credit limit.
            if (saBalance > beast::kZERO ||
                (rspEntry.getLimitPeer() &&
                 ((-saBalance) < rspEntry.getLimitPeer())))
            {
                assets.insert(saBalance.get<Issue>().currency);
            }
        }
    }

    // Remove the reserved sentinel to guard against malformed trust-line entries.
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

    // XRP is always a valid destination — even if the account does not yet
    // exist — because account creation itself requires receiving XRP above
    // the base reserve.
    if (includeXRP)
        assets.insert(xrpCurrency());

    if (auto const lines = lrCache->getRippleLines(account, LineDirection::Outgoing))
    {
        for (auto const& rspEntry : *lines)
        {
            auto& saBalance = rspEntry.getBalance();

            // Include if the account's own trust limit has not been reached.
            if (saBalance < rspEntry.getLimit())
                assets.insert(saBalance.get<Issue>().currency);
        }
    }

    // Remove the reserved sentinel to guard against malformed trust-line entries.
    assets.erase(badCurrency());

    if (auto const mpts = lrCache->getMPTs(account))
    {
        for (auto const& rspEntry : *mpts)
        {
            // Zero balance means fresh capacity; !isMaxedOut() confirms the
            // issuer's supply ceiling permits new transfers.
            if (rspEntry.isZeroBalance() && !rspEntry.isMaxedOut())
                assets.insert(rspEntry.getMptID());
        }
    }

    return assets;
}

}  // namespace xrpl
