#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <memory>
#include <optional>

namespace xrpl {

class ApplyView;

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
enum class TruncateShares : bool { no = false, yes = true };

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
    TruncateShares truncate = TruncateShares::no);

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

/** Apply the asset-side of a vault deposit.

    Increments the vault's asset accounting fields, transfers assets from the
    depositor to the vault pseudo-account, runs `associateAsset` on the vault
    SLE so any STNumber rounding to the asset's scale is observable, and then
    verifies that the resulting deltas match the requested amount. If
    `associateAsset` (or any other rounding step) silently dropped precision,
    the verification fails with `tecPRECISION_LOSS` and the partial mutation
    is rolled back via standard tec-class semantics.

    @param view The apply view.
    @param vault The vault SLE (peeked, will be mutated).
    @param depositor The account depositing assets.
    @param assetsDeposited The amount of assets to be deposited.
    @param j Journal for logging.

    @return tesSUCCESS on success; tecLIMIT_EXCEEDED if the deposit would push
            the vault past its `sfAssetsMaximum`; tecPRECISION_LOSS if any
            ledger field or trust-line balance changed by an amount different
            than `assetsDeposited`; tefINTERNAL on impossible internal states;
            otherwise the result of the underlying `accountSend`.
*/
[[nodiscard]] TER
depositToVault(
    ApplyView& view,
    std::shared_ptr<SLE> const& vault,
    AccountID const& depositor,
    STAmount const& assetsDeposited,
    beast::Journal j);

}  // namespace xrpl
