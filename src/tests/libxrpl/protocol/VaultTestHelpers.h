#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
// Needed for standalone compilation of sfScale/sfLEVersion assignment below;
// full-TU builds already see it transitively, so misc-include-cleaner flags
// it as unused there. Keep it so this header still compiles on its own.
#include <xrpl/protocol/STInteger.h>  // NOLINT(misc-include-cleaner)
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTakesAsset.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace xrpl {

// Shared by VaultGridTests, VaultBalanceTests and LoanBrokerCoverTests. For
// FixedPrecision, getAssetsTotal derives from AssetsAvailable plus
// AssetsDeployed rather than reading the stored AssetsTotal cache, so a
// FixedPrecision vault must have AssetsAvailable (and, if requested, a
// non-zero AssetsDeployed) seeded separately from assetsTotal. By default
// AssetsAvailable is assetsTotal - assetsDeployed, so the derived total
// equals assetsTotal; pass assetsAvailable explicitly to decouple it (e.g.
// to leave assetsTotal as a deliberately stale cache). Legacy/CashBasis
// vaults have no AssetsDeployed field and normally leave AssetsAvailable
// unset, but some cash-flow tests need it seeded too, so assetsAvailable is
// honored for those versions as well when given.
inline std::shared_ptr<SLE>
makeVault(
    Asset const& asset,
    Number const& assetsTotal,
    std::optional<VaultVersion> version,
    std::uint8_t scaleValue = kVaultDefaultIouScale,
    Number const& assetsDeployed = Number{0},
    std::optional<Number> assetsAvailable = std::nullopt)
{
    auto vault = std::make_shared<SLE>(keylet::vault(uint256(1)));
    vault->setFieldIssue(sfAsset, STIssue{sfAsset, asset});
    vault->at(sfAssetsTotal) = assetsTotal;
    if (!asset.integral())
        vault->at(sfScale) = scaleValue;
    if (version)
        vault->at(sfLEVersion) = std::to_underlying(*version);
    if (version == VaultVersion::FixedPrecision)
    {
        vault->at(sfAssetsAvailable) = assetsAvailable.value_or(assetsTotal - assetsDeployed);
        vault->at(sfAssetsDeployed) = assetsDeployed;
    }
    else if (assetsAvailable)
    {
        vault->at(sfAssetsAvailable) = *assetsAvailable;
    }
    associateAsset(*vault, asset);
    return vault;
}

}  // namespace xrpl
