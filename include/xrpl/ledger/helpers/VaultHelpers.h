#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <memory>
#include <optional>
#include <string_view>

namespace xrpl {

/**
 * Vault preclaim precision guard (fixCleanup3_2_0).
 *
 * Prevents asymmetric drift between `sfAssetsTotal` and `sfAssetsAvailable`
 * when they live in different IOU exponent bands. Two checks return `tecPRECISION_LOSS`:
 *  1. Rail sub-ULP: reject if `amount` rounds to zero at `sfAssetsAvailable`'s
 *     scale.
 *  2. Coarser-scale lossless (only when `sfAssetsTotal` sits coarser than
 *     `sfAssetsAvailable`): reject if `amount` is not representable losslessly
 *     at `sfAssetsTotal`'s scale.
 *
 * Trade-off: in a loan-trapped state, check (2) rejects any request that isn't
 * a clean multiple of the coarser ULP.
 *
 * Note: Share-denominated requests must be converted via `sharesToAssetsWithdraw`
 * before calling.
 *
 * @param view         Apply view (rules used for amendment gating).
 * @param vault        The vault SLE (read-only).
 * @param amount       Asset-denominated effective subtraction/addition.
 * @param j            Journal for logging.
 * @param logPrefix    Transactor name for log diagnostics.
 */
[[nodiscard]] TER
canApplyToVault(
    ReadView const& view,
    SLE::const_ref vault,
    STAmount const& amount,
    beast::Journal j,
    std::string_view logPrefix);

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

/** Aligned (shares, assets) pair for a VaultWithdraw rail update. */
struct ClampedWithdrawal
{
    STAmount assets;
    STAmount shares;
};

/**
 * Clamp an asset-denominated withdraw to the vault's coarsest grid.
 *
 * Pre-amendment: returns the legacy round-trip
 * (`assetsToSharesWithdraw` no-truncate → `sharesToAssetsWithdraw`).
 *
 * Post-amendment: truncates shares, re-derives assets, floors the asset
 * round-trip down to `sfAssetsTotal`'s scale, and re-derives shares from the
 * clamped assets so the two rails decrement in lockstep. Returns
 * `tecPRECISION_LOSS` when the request is sub-share or sub-asset dust.
 */
Expected<ClampedWithdrawal, TER>
clampAssetWithdrawal(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& amount,
    Rules const& rules);

/**
 * Clamp a share-denominated withdraw to the vault's coarsest grid.
 *
 * Drained-vault short-circuit: if the NAV-conversion yields zero assets,
 * passes through so `doApply`'s `accountHolds` check can surface
 * `tecINSUFFICIENT_FUNDS`.
 *
 * Pre-amendment: returns the legacy pass-through after the short-circuit.
 *
 * Post-amendment: floors the asset round-trip down to `sfAssetsTotal`'s scale,
 * then re-derives shares from the clamped assets. Returns `tecPRECISION_LOSS`
 * on sub-asset dust or when the re-derived share count truncates to zero
 * (heavily diluted vault where NAV < 1 ULP).
 */
Expected<ClampedWithdrawal, TER>
clampShareWithdrawal(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& shares,
    Rules const& rules);

/**
 * Applies the asset-side of a vault deposit.
 *
 * Execution steps:
 * 1. Increments the vault's asset accounting fields.
 * 2. Transfers assets from the `depositor` to the vault pseudo-account.
 * 3. Runs `associateAsset` on the vault SLE.
 * 4. Verifies that the resulting deltas exactly match the requested amount.
 *
 * Note: If `associateAsset` (or any other step) silently drops precision due
 * to rounding, the delta verification fails with `tecPRECISION_LOSS` and the
 * partial mutation is safely rolled back via standard tec-class semantics.
 */
[[nodiscard]] TER
depositToVault(
    ApplyView& view,
    SLE::ref vault,
    AccountID const& depositor,
    STAmount const& assetsDeposited,
    beast::Journal j);

/**
 * Applies the asset-side of a vault withdrawal.
 *
 * Execution steps:
 * 1. Decrements the vault's asset accounting fields.
 * 2. Transfers shares from the `depositor` back to the vault pseudo-account
 * (removing their empty MPToken if applicable).
 * 3. Runs `associateAsset` on the vault SLE.
 * 4. Transfers `assetsWithdrawn` from the vault pseudo-account to the
 * `destination` via `doWithdraw`.
 * 5. Verifies that the resulting deltas exactly match the requested amount.
 *
 * Note: Post-amendment, the delta verification catches silent rounding
 * mismatches and safely returns `tecPRECISION_LOSS` before the broader
 * withdrawal invariants have a chance to fire.
 */
[[nodiscard]] TER
withdrawFromVault(
    ApplyView& view,
    STTx const& tx,
    SLE::ref vault,
    AccountID const& depositor,
    AccountID const& destination,
    XRPAmount preFeeBalance,
    STAmount const& assetsWithdrawn,
    STAmount const& sharesRedeemed,
    beast::Journal j);
}  // namespace xrpl
