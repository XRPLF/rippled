#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <optional>

namespace xrpl::vault {

enum class TruncateShares : bool { no = false, yes = true };

// SLE-based public API — dispatches to v1 or v2 based on amendment rules.

[[nodiscard]] std::optional<STAmount>
assetsToSharesDeposit(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets);

[[nodiscard]] std::optional<STAmount>
sharesToAssetsDeposit(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& shares);

[[nodiscard]] std::optional<STAmount>
assetsToSharesWithdraw(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets,
    TruncateShares truncate = TruncateShares::no);

[[nodiscard]] std::optional<STAmount>
sharesToAssetsWithdraw(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& shares);

// Pure math — v2 only. Testable without SLE objects.

namespace math::v2 {

[[nodiscard]] Number
assetsToSharesDeposit(
    Number const& assetTotal,
    Number const& shareTotal,
    std::int32_t scale,
    Number const& assets);

[[nodiscard]] Number
sharesToAssetsDeposit(
    Number const& assetTotal,
    Number const& shareTotal,
    std::int32_t scale,
    STAmount const& shares);

[[nodiscard]] Number
assetsToSharesWithdraw(
    Number const& assetTotal,
    Number const& lossUnrealized,
    Number const& shareTotal,
    Number const& assets,
    TruncateShares truncate = TruncateShares::no);

[[nodiscard]] Number
sharesToAssetsWithdraw(
    Number const& assetTotal,
    Number const& lossUnrealized,
    Number const& shareTotal,
    STAmount const& shares);

}  // namespace math::v2

}  // namespace xrpl::vault
