#pragma once

#include <xrpl/basics/Expected.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <optional>

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

struct ExchangeResult
{
    STAmount assets;
    STAmount shares;
};

[[nodiscard]] Expected<ExchangeResult, TER>
computeDeposit(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets,
    beast::Journal j);

/** Compute a withdrawal given a fixed asset amount.
 *
 *  Converts assets → shares → assets (round-trip) to determine the
 *  exact shares to redeem and assets to return.
 *
 *  The intermediate shares value is stored as an MPT (integer), which
 *  uses banker's rounding (round-to-nearest, even on tie). When shares
 *  round up, the back-calculated assets may exceed the requested amount.
 *
 *  Example (scale=1, vault=87.5 IOU, 875 shares):
 *    assetsToSharesWithdraw(3.75) → round(37.5) = 38 shares
 *    sharesToAssetsWithdraw(38)   → 3.8 IOU  (> 3.75 requested)
 *
 *  This matches v1 behaviour. See XLS-0065 §3.1.7.1.
 *
 *  @return ExchangeResult{assets, shares} on success.
 *  @return tecPRECISION_LOSS if computed shares are zero.
 *  @return tecPATH_DRY on arithmetic overflow.
 *  @return tecINTERNAL on invalid input or vault state.
 */
[[nodiscard]] Expected<ExchangeResult, TER>
computeWithdrawByAssets(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets,
    beast::Journal j);

[[nodiscard]] Expected<ExchangeResult, TER>
computeWithdrawByShares(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& shares,
    beast::Journal j);

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
