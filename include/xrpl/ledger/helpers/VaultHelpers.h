#pragma once

#include <xrpl/basics/Expected.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

namespace xrpl::vault {

enum class TruncateShares : bool { no = false, yes = true };

// Low-level v2 math — exposed for unit testing.
namespace detail {

[[nodiscard]] STAmount
assetsToSharesDeposit(SLE::const_ref vault, SLE::const_ref issuance, STAmount const& assets);

[[nodiscard]] STAmount
sharesToAssetsDeposit(SLE::const_ref vault, SLE::const_ref issuance, STAmount const& shares);

[[nodiscard]] STAmount
assetsToSharesWithdraw(
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets,
    TruncateShares truncate = TruncateShares::no);

[[nodiscard]] STAmount
sharesToAssetsWithdraw(SLE::const_ref vault, SLE::const_ref issuance, STAmount const& shares);

}  // namespace detail

// High-level API — orchestrates forward+reverse conversions, handles overflow.

/** The actual amounts exchanged after the forward+reverse round-trip.
 *  Both values reflect post-rounding quantities: assets is the amount
 *  transferred, shares is the number of share tokens created or destroyed.
 */
struct ExchangeResult
{
    STAmount assets;
    STAmount shares;
};

/**
 * Computes the asset/share exchange for a vault deposit. Converts the
 * requested asset amount to shares (forward), then back-calculates the
 * actual assets consumed (reverse) to ensure the vault never takes more
 * than offered.
 *
 * @param assets  The asset amount the depositor is offering.
 *
 * @return {assetsDeposited, sharesCreated} on success. assetsDeposited
 *         is always <= assets. Returns tecPRECISION_LOSS if shares
 *         truncate to zero, tecPATH_DRY on overflow.
 */
[[nodiscard]] Expected<ExchangeResult, TER>
computeDeposit(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets,
    beast::Journal j);

/**
 * Computes the asset/share exchange for a withdrawal by asset amount.
 * Converts the requested assets to shares, then back-calculates the
 * actual assets returned.
 *
 * Note: due to banker's rounding on the intermediate share value, the
 * returned assets may slightly exceed the requested amount. This is by
 * design — the user burns more shares to receive proportionally more
 * assets. The per-share price is preserved. See XLS-0065 §3.1.7.1.
 *
 * @param assets  The asset amount the withdrawer is requesting.
 *
 * @return {assetsWithdrawn, sharesRedeemed} on success. Returns
 *         tecPRECISION_LOSS if shares truncate to zero, tecPATH_DRY
 *         on overflow.
 */
[[nodiscard]] Expected<ExchangeResult, TER>
computeWithdrawByAssets(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets,
    beast::Journal j);

/**
 * Computes the asset/share exchange for a withdrawal by share amount.
 * Converts the given shares directly to assets.
 *
 * @param shares  The number of shares the withdrawer is redeeming.
 *
 * @return {assetsWithdrawn, shares} on success. Returns tecPATH_DRY
 *         on overflow.
 */
[[nodiscard]] Expected<ExchangeResult, TER>
computeWithdrawByShares(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& shares,
    beast::Journal j);

/**
 * Computes the asset/share exchange for a clawback. Performs a two-pass
 * computation: first converts the clawback amount to shares and back to
 * assets. If the result exceeds assetsAvailable, re-computes with
 * truncated shares to ensure the recovered amount stays within the limit.
 *
 * @param clawbackAmount   The asset amount being clawed back.
 * @param assetsAvailable  Hard ceiling — recovered assets must not exceed
 *                         this value.
 *
 * @return {assetsRecovered, sharesDestroyed} on success. assetsRecovered
 *         is always <= assetsAvailable. Returns tecPATH_DRY on overflow.
 */
[[nodiscard]] Expected<ExchangeResult, TER>
computeClawback(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& clawbackAmount,
    Number const& assetsAvailable,
    beast::Journal j);

/**
 * Updates vault state when a loan is issued: reduces available assets by the
 * borrowed amount and increases total assets by the yield (accrued interest).
 * With featureLendingProtocolV1_1, also tracks the yield in InterestUnrealized
 * and validates vault state after modification.
 *
 * @param view    The ledger view to read issuance from and update.
 * @param vault   The vault SLE to modify. Must be of type ltVAULT.
 * @param amount  The principal to borrow. Must be > 0 and <= AssetsAvailable.
 * @param yield   The accrued interest to add. Must be >= 0. Added to both
 *                AssetsTotal and InterestUnrealized (v1_1 only).
 * @param j       Journal for error logging.
 *
 * @return tesSUCCESS on success, tecINTERNAL on invalid inputs or if the
 *         resulting vault state fails validation. The caller should validate
 *         inputs beforehand to return a user-facing error code.
 */
[[nodiscard]] TER
borrowFromVault(
    ApplyView& view,
    SLE::ref vault,
    Number const& amount,
    Number const& yield,
    beast::Journal j);

}  // namespace xrpl::vault
