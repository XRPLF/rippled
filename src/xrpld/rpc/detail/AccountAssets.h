#pragma once

#include <xrpld/rpc/detail/AssetCache.h>

#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/PathAsset.h>

#include <memory>

namespace xrpl {

HashSet<PathAsset>
accountDestAssets(
    AccountID const& account,
    std::shared_ptr<AssetCache> const& cache,
    bool includeXRP);

HashSet<PathAsset>
accountSourceAssets(
    AccountID const& account,
    std::shared_ptr<AssetCache> const& lrLedger,
    bool includeXRP);

}  // namespace xrpl
