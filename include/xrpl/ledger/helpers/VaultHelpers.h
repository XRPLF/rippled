/** @file
 *  Pure arithmetic helpers for the XLS-65d Single-Sided Vault feature.
 *
 *  Each function converts between the two token types a vault manages:
 *  the underlying *asset* (XRP, IOU, or MPT that depositors contribute) and
 *  vault *shares* (an MPT representing proportional ownership). Because MPT
 *  values are always integers every function makes an explicit rounding
 *  decision — and those decisions differ between the deposit and withdrawal
 *  paths to protect vault solvency.
 *
 *  These functions are stateless and side-effect-free; all ledger mutations
 *  are the caller's responsibility.
 */
#pragma once

#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <memory>
#include <optional>

namespace xrpl {

/** Compute the shares minted when a depositor offers a fixed asset amount.
 *
 *  Uses `sfAssetsTotal` from `vault` directly, *without* subtracting
 *  `sfLossUnrealized`. Unrealized losses are a risk borne by existing
 *  shareholders, not a discount for new depositors.
 *
 *  **Bootstrap case**: when `sfAssetsTotal == 0` the result is
 *  `assets × 10^sfScale` (truncated), establishing the initial exchange rate.
 *  The non-bootstrap result is `(sfOutstandingAmount × assets) / sfAssetsTotal`,
 *  always truncated — depositors always receive a whole number of shares, never
 *  more than the assets strictly warrant.
 *
 *  @note The deposit transactor calls this first, then back-calculates the
 *      true asset cost via `sharesToAssetsDeposit()` to ensure it never
 *      extracts more than the depositor offered.
 *  @throws std::overflow_error if `sfScale` is large enough to overflow
 *      XRPL's `Number` type; callers should catch and return `tecPATH_DRY`.
 *
 *  @param vault    The vault SLE; must contain `sfAsset`, `sfAssetsTotal`,
 *      `sfScale`, and `sfShareMPTID`.
 *  @param issuance The MPTokenIssuance SLE for the vault's share token;
 *      must contain `sfOutstandingAmount`.
 *  @param assets   The asset amount to convert; must be non-negative and
 *      must match `vault->at(sfAsset)`.
 *  @return The integral share amount, or `nullopt` if `assets` is negative
 *      or its asset type does not match the vault.
 */
[[nodiscard]] std::optional<STAmount>
assetsToSharesDeposit(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& assets);

/** Compute the asset cost for a depositor who will receive a fixed share amount.
 *
 *  This is the inverse of `assetsToSharesDeposit()` and is used in the second
 *  step of the deposit calculation: after truncating the forward direction to
 *  determine how many whole shares are created, the transactor calls this
 *  function to derive the exact asset amount to collect.
 *
 *  Uses `sfAssetsTotal` directly, without subtracting `sfLossUnrealized`,
 *  matching the deposit-path convention.
 *
 *  **Bootstrap case**: when `sfAssetsTotal == 0` the result uses `sfScale` to
 *  reverse the bootstrap formula applied by `assetsToSharesDeposit()`.
 *
 *  @throws std::overflow_error if `sfScale` is large enough to overflow
 *      XRPL's `Number` type; callers should catch and return `tecPATH_DRY`.
 *
 *  @param vault    The vault SLE.
 *  @param issuance The MPTokenIssuance SLE for the vault's share token.
 *  @param shares   The share amount to convert; must be non-negative and must
 *      match `vault->at(sfShareMPTID)`.
 *  @return The asset amount, or `nullopt` if `shares` is negative or its
 *      asset type does not match the vault's share MPT.
 */
[[nodiscard]] std::optional<STAmount>
sharesToAssetsDeposit(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& shares);

/** Controls whether to truncate (floor) the share result instead of rounding.
 *
 *  `No` (the default) rounds to nearest, ensuring the vault is never
 *  shortchanged when computing shares to redeem for a fixed asset withdrawal.
 *  `Yes` applies floor truncation, used when the caller explicitly needs
 *  conservative (depositor-favoring) rounding.
 */
enum class TruncateShares : bool { No = false, Yes = true };

/** Compute the shares a withdrawer must redeem to receive a fixed asset amount.
 *
 *  Unlike the deposit path, this function subtracts `sfLossUnrealized` from
 *  `sfAssetsTotal` before computing the exchange rate. Withdrawers receive fewer
 *  assets per share when the vault has recorded unrealized losses, preventing
 *  early withdrawers from exiting at inflated prices at the expense of remaining
 *  holders.
 *
 *  The result is rounded to nearest by default (`TruncateShares::No`), ensuring
 *  the vault is not shortchanged. The withdraw transactor then back-calculates
 *  the actual assets delivered via `sharesToAssetsWithdraw()` for a precise
 *  two-step computation.
 *
 *  If `sfAssetsTotal - sfLossUnrealized == 0` (fully insolvent vault), returns
 *  a zero-valued `STAmount` rather than dividing by zero.
 *
 *  @throws std::overflow_error if arithmetic overflows XRPL's `Number` type;
 *      callers should catch and return `tecPATH_DRY`.
 *
 *  @param vault    The vault SLE; must contain `sfAsset`, `sfAssetsTotal`,
 *      `sfLossUnrealized`, and `sfShareMPTID`.
 *  @param issuance The MPTokenIssuance SLE for the vault's share token.
 *  @param assets   The asset amount to convert; must be non-negative and must
 *      match `vault->at(sfAsset)`.
 *  @param truncate Whether to truncate instead of rounding to nearest.
 *  @return The integral share amount, or `nullopt` if `assets` is negative or
 *      its asset type does not match the vault.
 */
[[nodiscard]] std::optional<STAmount>
assetsToSharesWithdraw(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& assets,
    TruncateShares truncate = TruncateShares::No);

/** Compute the assets delivered when a withdrawer redeems a fixed share amount.
 *
 *  Like `assetsToSharesWithdraw()`, this function subtracts `sfLossUnrealized`
 *  from `sfAssetsTotal` before computing the exchange rate, so withdrawers
 *  bear their proportional share of any recorded losses.
 *
 *  If `sfAssetsTotal - sfLossUnrealized == 0` (fully insolvent vault), returns
 *  a zero-valued `STAmount` rather than dividing by zero.
 *
 *  @throws std::overflow_error if arithmetic overflows XRPL's `Number` type;
 *      callers should catch and return `tecPATH_DRY`.
 *
 *  @param vault    The vault SLE.
 *  @param issuance The MPTokenIssuance SLE for the vault's share token.
 *  @param shares   The share amount to convert; must be non-negative and must
 *      match `vault->at(sfShareMPTID)`.
 *  @return The asset amount, or `nullopt` if `shares` is negative or its
 *      asset type does not match the vault's share MPT.
 */
[[nodiscard]] std::optional<STAmount>
sharesToAssetsWithdraw(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& shares);

}  // namespace xrpl
