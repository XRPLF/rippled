#pragma once

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

}  // namespace xrpl::vault
