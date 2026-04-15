#pragma once

#include <xrpld/rpc/detail/AssetCache.h>

#include <xrpl/protocol/UintTypes.h>

namespace xrpl {

hash_set<PathAsset>
accountDestAssets(
    AccountID const& account,
    std::shared_ptr<AssetCache> const& cache,
    bool includeXRP);

hash_set<PathAsset>
accountSourceAssets(
    AccountID const& account,
    std::shared_ptr<AssetCache> const& lrLedger,
    bool includeXRP);

}  // namespace xrpl
