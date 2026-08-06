#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <expected>
#include <optional>

namespace xrpl {

/**
 * From the perspective of a vault, return the number of shares to give
 * depositor when they offer a fixed amount of assets. Note, since shares are
 * MPT, this number is integral and always truncated in this calculation.
 *
 * @param vault The vault SLE.
 * @param issuance The MPTokenIssuance SLE for the vault's shares.
 * @param assets The amount of assets to convert.
 *
 * @return The number of shares, or nullopt on error.
 */
[[nodiscard]] std::optional<STAmount>
assetsToSharesDeposit(SLE::const_ref vault, SLE::const_ref issuance, STAmount const& assets);

/**
 * From the perspective of a vault, return the number of assets to take from
 * depositor when they receive a fixed amount of shares. Note, since shares are
 * MPT, they are always an integral number.
 *
 * @param vault The vault SLE.
 * @param issuance The MPTokenIssuance SLE for the vault's shares.
 * @param shares The amount of shares to convert.
 *
 * @return The number of assets, or nullopt on error.
 */
[[nodiscard]] std::optional<STAmount>
sharesToAssetsDeposit(SLE::const_ref vault, SLE::const_ref issuance, STAmount const& shares);

/**
 * Controls whether to truncate shares instead of rounding.
 */
enum class TruncateShares : bool { No = false, Yes = true };

/**
 * Controls whether the withdraw conversion helpers
 * (assetsToSharesWithdraw and sharesToAssetsWithdraw) subtract
 * sfLossUnrealized from sfAssetsTotal before computing the exchange rate.
 * The default (No) applies the standard discounted rate; Yes is used when
 * the redeemer is the sole remaining shareholder.
 */
enum class WaiveUnrealizedLoss : bool { No = false, Yes = true };

/**
 * From the perspective of a vault, return the number of shares to demand from
 * the depositor when they ask to withdraw a fixed amount of assets. Since
 * shares are MPT this number is integral, and it will be rounded to nearest
 * unless explicitly requested to be truncated instead.
 *
 * @param vault The vault SLE.
 * @param issuance The MPTokenIssuance SLE for the vault's shares.
 * @param assets The amount of assets to convert.
 * @param truncate Whether to truncate instead of rounding.
 * @param waive Whether to waive the unrealized-loss discount when computing
 *              the exchange rate.
 *
 * @return The number of shares, or nullopt on error.
 */
[[nodiscard]] std::optional<STAmount>
assetsToSharesWithdraw(
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets,
    TruncateShares truncate = TruncateShares::No,
    WaiveUnrealizedLoss waive = WaiveUnrealizedLoss::No);

/**
 * From the perspective of a vault, return the number of assets to give the
 * depositor when they redeem a fixed amount of shares. Note, since shares are
 * MPT, they are always an integral number.
 *
 * @param vault The vault SLE.
 * @param issuance The MPTokenIssuance SLE for the vault's shares.
 * @param shares The amount of shares to convert.
 * @param waive Whether to waive (i.e. not subtract) the vault's unrealized
 *              loss when computing the exchange rate.
 *
 * @return The number of assets, or nullopt on error.
 */
[[nodiscard]] std::optional<STAmount>
sharesToAssetsWithdraw(
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& shares,
    WaiveUnrealizedLoss waive = WaiveUnrealizedLoss::No);

/**
 * Returns true iff `account` holds all of the vault's outstanding shares —
 * i.e. is the sole remaining shareholder. Returns false if the account
 * holds no shares or fewer than the total outstanding.
 *
 * @param view The ledger view.
 * @param account The candidate sole shareholder.
 * @param issuance The MPTokenIssuance SLE for the vault's shares; provides
 *                 both the share MPTID and the outstanding-amount total.
 */
[[nodiscard]] bool
isSoleShareholder(ReadView const& view, AccountID const& account, SLE::const_ref issuance);

/**
 * Resolves a Vault's LEVersion, the single point every accounting touch
 * point should call to determine which recognition model (accrual vs.
 * cash-basis) a Vault uses. Vaults created before featureLendingProtocolV1_1
 * activated never have sfLEVersion set, which resolves here to
 * VaultVersion::Legacy.
 *
 * @param vault The vault SLE.
 *
 * @return The Vault's LEVersion, or VaultVersion::Legacy if the field is
 * absent.
 */
[[nodiscard]] VaultVersion
getVaultVersion(SLE::const_ref vault);

/**
 * The single owner of every write to a Vault's two accounting fields,
 * sfAssetsAvailable and sfAssetsTotal, for a cash-moving-in operation
 * (deposit, repayment, or default settlement).
 *
 * On this branch this is a pure bookkeeping refactor with no behaviour
 * change: it performs exactly
 *   sfAssetsAvailable += cashIn
 *   sfAssetsTotal     += recognitionDelta
 *   view.update(vault)
 * and nothing else. In particular it does NOT enforce
 * sfAssetsAvailable <= sfAssetsTotal — callers keep their own guard exactly
 * where it is today, because that guard is order-sensitive and, at at least
 * one call site (LoanManage::defaultLoan), the fields legitimately cross
 * transiently before an existing post-write correction runs.
 *
 * @param view The ApplyView to mutate.
 * @param vault The vault SLE (mutated in place; caller retains ownership).
 * @param cashIn The amount of cash arriving at the vault's main custody.
 *               Must already be rounded to whatever scale the caller's
 *               transactor uses; this function performs no rounding.
 * @param recognitionDelta The signed amount sfAssetsTotal should recognise,
 *                          independently of cashIn.
 * @param j Journal (currently unused; reserved for the dust-mechanism
 *          amendment gate that lands on the solution branches).
 *
 * @return What sfAssetsAvailable actually moved by. On this branch that is
 *         always exactly cashIn.
 */
[[nodiscard]] std::expected<Number, TER>
addAssetsToVault(
    ApplyView& view,
    SLE::ref vault,
    Number const& cashIn,
    Number const& recognitionDelta,
    beast::Journal j);

/**
 * The counterpart of addAssetsToVault for a cash-moving-out operation
 * (withdrawal, clawback, or loan funding). Performs
 *   sfAssetsAvailable -= cashOut
 *   sfAssetsTotal     += recognitionDelta
 *   view.update(vault)
 * See addAssetsToVault for the guard-placement rationale.
 *
 * @return What sfAssetsAvailable actually moved by (as a negative number).
 *         On this branch that is always exactly -cashOut.
 */
[[nodiscard]] std::expected<Number, TER>
removeAssetsFromVault(
    ApplyView& view,
    SLE::ref vault,
    Number const& cashOut,
    Number const& recognitionDelta,
    beast::Journal j);

/**
 * The terminal-withdrawal entry point: assigns both accounting fields to
 * zero. This is an assignment, not a delta, which is why it is a separate
 * function rather than a degenerate call to removeAssetsFromVault
 * (VaultWithdraw.cpp's "Do not let dust accumulate in the Vault" branch).
 *
 * @return sfAssetsAvailable's value immediately before it was zeroed (i.e.
 *         what the caller still owes the departing shareholder).
 */
[[nodiscard]] std::expected<Number, TER>
closeVaultAssets(ApplyView& view, SLE::ref vault, beast::Journal j);

}  // namespace xrpl
