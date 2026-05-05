#pragma once

#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <memory>
#include <optional>

namespace xrpl {

/** From the perspective of a vault, return the number of shares to give
    depositor when they offer a fixed amount of assets. Note, since shares are
    MPT, this number is integral and always truncated in this calculation.

    @param vault The vault SLE.
    @param issuance The MPTokenIssuance SLE for the vault's shares.
    @param assets The amount of assets to convert.

    @return The number of shares, or nullopt on error.
*/
[[nodiscard]] std::optional<STAmount>
assetsToSharesDeposit(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& assets);

/** From the perspective of a vault, return the number of assets to take from
    depositor when they receive a fixed amount of shares. Note, since shares are
    MPT, they are always an integral number.

    @param vault The vault SLE.
    @param issuance The MPTokenIssuance SLE for the vault's shares.
    @param shares The amount of shares to convert.

    @return The number of assets, or nullopt on error.
*/
[[nodiscard]] std::optional<STAmount>
sharesToAssetsDeposit(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& shares);

/** Controls whether to truncate shares instead of rounding. */
enum class TruncateShares : bool { No = false, Yes = true };

/** From the perspective of a vault, return the number of shares to demand from
    the depositor when they ask to withdraw a fixed amount of assets. Since
    shares are MPT this number is integral, and it will be rounded to nearest
    unless explicitly requested to be truncated instead.

    @param vault The vault SLE.
    @param issuance The MPTokenIssuance SLE for the vault's shares.
    @param assets The amount of assets to convert.
    @param truncate Whether to truncate instead of rounding.

    @return The number of shares, or nullopt on error.
*/
[[nodiscard]] std::optional<STAmount>
assetsToSharesWithdraw(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& assets,
    TruncateShares truncate = TruncateShares::No);

/** From the perspective of a vault, return the number of assets to give the
    depositor when they redeem a fixed amount of shares. Note, since shares are
    MPT, they are always an integral number.

    @param vault The vault SLE.
    @param issuance The MPTokenIssuance SLE for the vault's shares.
    @param shares The amount of shares to convert.

    @return The number of assets, or nullopt on error.
*/
[[nodiscard]] std::optional<STAmount>
sharesToAssetsWithdraw(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& shares);

}  // namespace xrpl
