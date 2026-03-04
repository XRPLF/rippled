#pragma once

#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>

namespace xrpl {

enum class TruncateShares : bool { no = false, yes = true };

// From the perspective of a vault, return the number of shares to give the
// depositor when they deposit a fixed amount of assets. Since shares are MPT
// this number is integral and always truncated in this calculation.
[[nodiscard]] std::optional<STAmount>
assetsToSharesDeposit(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& assets);

// From the perspective of a vault, return the number of assets to take from
// depositor when they receive a fixed amount of shares. Note, since shares are
// MPT, they are always an integral number.
[[nodiscard]] std::optional<STAmount>
sharesToAssetsDeposit(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& shares);

// From the perspective of a vault, return the number of shares to demand from
// the depositor when they ask to withdraw a fixed amount of assets. Since
// shares are MPT this number is integral, and it will be rounded to nearest
// unless explicitly requested to be truncated instead.
[[nodiscard]] std::optional<STAmount>
assetsToSharesWithdraw(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& assets,
    TruncateShares truncate = TruncateShares::no);

// From the perspective of a vault, return the number of assets to give the
// depositor when they redeem a fixed amount of shares. Note, since shares are
// MPT, they are always an integral number.
[[nodiscard]] std::optional<STAmount>
sharesToAssetsWithdraw(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& shares);

// Determine if a vault is insolvent. A vault is considered insolvent when
// the total assets in the vault are zero, and outstanding shares are non-zero.
[[nodiscard]] bool
isVaultInsolvent(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& shareIssuance);

}  // namespace xrpl
