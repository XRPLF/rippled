#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

namespace xrpl {

class STTx;

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
 * Adjusts a requested asset change (`delta`) to match the decimal scale of the
 * updated total vault assets. This ensures `sfAssetsTotal`, `sfAssetsAvailable`,
 * and the actual asset transfer change by the exact same representable amount.
 *
 * Rounding strategy:
 * - Debits (withdrawals): Rounds down `|delta|` on the new scale to prevent
 *   paying out more than requested.
 * - Credits (deposits): Floors the resulting total asset balance and returns the
 *   difference from the current total. This prevents crediting the vault with
 *   more assets than the user deposited.
 *
 * Key rules:
 * - The returned magnitude never exceeds `|delta|`.
 * - Returns `tecPRECISION_LOSS` if the change is smaller than 1 ULP of the target scale
 *   (prevents share operations when totals cannot change).
 * - For integer assets (XRP, MPT), rounding is a no-op.
 *
 * @param vault The vault ledger entry.
 * @param delta The requested signed change to sfAssetsTotal.
 * @return The rounded, positive magnitude, or `tecPRECISION_LOSS` if the
 *         change is below representable precision.
 */
[[nodiscard]] std::expected<STAmount, TER>
clampToAssetsTotalScale(SLE::const_ref vault, STAmount const& delta);

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
 * Returns the assets backing outstanding shares for a withdrawal:
 * sfAssetsTotal minus sfLossUnrealized, or sfAssetsTotal alone when the
 * unrealized loss is waived. Used by assetsToSharesWithdraw and
 * sharesToAssetsWithdraw as the numerator of the share/asset exchange rate.
 *
 * @param vault The vault SLE.
 * @param waive Whether to skip subtracting the unrealized loss.
 */
[[nodiscard]] Number
assetsTotalForWithdrawal(SLE::const_ref vault, WaiveUnrealizedLoss waive);

/**
 * Returns true if debiting `amount` from `total` (the current value of a
 * vault's sfAssetsTotal or sfAssetsAvailable) would canonicalize to the
 * same STAmount value. This happens when `amount` is non-zero but too small
 * to change the stored total at STAmount's precision. Shares would still
 * move, so the ValidVault invariant would fail after apply; callers use
 * this to reject the transaction upfront instead.
 *
 * @param asset The vault's underlying asset, used to canonicalize both
 *              sides the same way the ledger will when the field is stored.
 * @param total The field's current value.
 * @param amount The amount to debit. Zero always returns false; that case
 *               is rejected separately.
 */
[[nodiscard]] bool
debitIsNonZeroDust(Asset const& asset, Number const& total, Number const& amount);

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
 * The assets-to-shares and shares-to-assets conversions for one share class of a
 * vault with tranches. Each takes the class's issuance and the class's value in
 * place of the vault's sfShareMPTID and sfAssetsTotal; the overloads above call
 * these with the vault's own values.
 *
 * @param classTotal The class's value, from vaultClassValue.
 */
[[nodiscard]] std::optional<STAmount>
assetsToSharesDeposit(
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets,
    Number const& classTotal);

[[nodiscard]] std::optional<STAmount>
sharesToAssetsDeposit(
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& shares,
    Number const& classTotal);

[[nodiscard]] std::optional<STAmount>
assetsToSharesWithdraw(
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets,
    TruncateShares truncate,
    Number const& classTotal);

[[nodiscard]] std::optional<STAmount>
sharesToAssetsWithdraw(
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& shares,
    Number const& classTotal);

/**
 * A share class of a vault: the equity share (sfShareMPTID, `tranche` empty) or
 * the debt class at index `tranche` of sfTranches, most senior first.
 */
struct VaultClass
{
    MPTID share;
    std::optional<std::size_t> tranche;
};

/**
 * Returns the class whose issuance is `share`, or nullopt when `share` is not
 * one of the vault's classes.
 */
[[nodiscard]] std::optional<VaultClass>
vaultClassOf(SLE::const_ref vault, MPTID const& share);

/**
 * Returns the class a vault transaction names: its sfShareMPTID, else its
 * sfAmount when that is a share of the vault, else the equity class. Returns
 * nullopt when sfShareMPTID is not a class of the vault, or names a different
 * class than a share-denominated sfAmount.
 */
[[nodiscard]] std::optional<VaultClass>
vaultClassForTx(SLE::const_ref vault, STTx const& tx);

/**
 * Returns a debt class's claim at `now`: its principal and accrued coupon plus
 * the simple interest on its principal since `accrualTime`.
 */
[[nodiscard]] Number
trancheClaim(STObject const& tranche, std::uint32_t accrualTime, NetClock::time_point now);

/**
 * Returns every class's value under the waterfall over `gross`: each debt class
 * takes the lesser of its claim and what remains, most senior first, and the
 * equity class takes the rest. Debt classes in order, the equity class last.
 */
[[nodiscard]] std::vector<Number>
vaultClassValues(SLE::const_ref vault, NetClock::time_point now, Number const& gross);

/**
 * Returns one class's value under the waterfall over `gross`.
 */
[[nodiscard]] Number
vaultClassValue(
    SLE::const_ref vault,
    VaultClass const& vaultClass,
    NetClock::time_point now,
    Number const& gross);

/**
 * Moves the coupon accrued since sfTrancheAccrualTime into each debt class's
 * sfInterestAccrued and sets sfTrancheAccrualTime to `now`. No class's value
 * changes.
 */
void
settleTranches(SLE::ref vault, NetClock::time_point now);

/**
 * Returns each debt class's coverage margin over sfAssetsTotal - sfLossUnrealized:
 * the value its sfSubordinationMinimum allows above it, less its claim and every
 * more senior claim.
 */
[[nodiscard]] std::vector<Number>
trancheMargins(SLE::const_ref vault, NetClock::time_point now);

/**
 * Returns true when, with assets deployed (sfAssetsTotal above sfAssetsAvailable),
 * some coverage margin is negative and lower than in `before`.
 */
[[nodiscard]] bool
trancheCoverageBreached(
    SLE::const_ref vault,
    std::vector<Number> const& before,
    NetClock::time_point now);

/**
 * Returns a class's value for a withdrawal or clawback: the waterfall over
 * sfAssetsTotal less sfLossUnrealized (unless waived), or that amount itself for
 * a vault without tranches.
 */
[[nodiscard]] Number
classTotalForWithdrawal(
    SLE::const_ref vault,
    VaultClass const& vaultClass,
    NetClock::time_point now,
    WaiveUnrealizedLoss waive);

/**
 * Returns true iff `account` holds every outstanding share of every class of the
 * vault that has shares outstanding.
 */
[[nodiscard]] bool
isSoleHolderOfVault(ReadView const& view, AccountID const& account, SLE::const_ref vault);

/**
 * Adds `delta` to the sfPrincipalOutstanding of the debt class at `index`.
 */
void
addTranchePrincipal(SLE::ref vault, std::size_t index, Number const& delta);

/**
 * Removes the share `redeemed / outstanding` of the principal and accrued coupon
 * of the debt class at `index`; all of both when every share is redeemed.
 */
void
reduceTrancheClaim(
    SLE::ref vault,
    std::size_t index,
    Number const& redeemed,
    Number const& outstanding);

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
 * Resolves the VaultKind of a vault SLE. Returns VaultKind::ClosedEnded when
 * sfVaultKind is present and equal to that value; anything else (including an
 * absent field or an unrecognised value) is treated as VaultKind::OpenEnded.
 *
 * @param vault The vault SLE.
 */
[[nodiscard]] VaultKind
getVaultKind(SLE::const_ref vault);

/**
 * Reads sfVaultKind from a transaction. An absent field resolves to
 * VaultKind::OpenEnded (matching the on-ledger default); any unrecognised
 * value is also treated as VaultKind::OpenEnded, mirroring the SLE overload.
 * Callers that need to reject out-of-range values (e.g. preflight) should
 * gate on isValidVaultKind() first.
 *
 * @param tx The transaction.
 */
[[nodiscard]] VaultKind
getVaultKind(STTx const& tx);

/**
 * Returns true iff sfVaultKind is either absent from @p tx or is present and
 * equal to a recognised VaultKind enumerator. Intended for use in preflight
 * to reject malformed transactions before decoding with getVaultKind().
 *
 * @param tx The transaction.
 */
[[nodiscard]] bool
isValidVaultKind(STTx const& tx);

/**
 * Returns true iff the (SubscriptionDate, RedemptionDate) gap of a
 * closed-ended vault satisfies
 * kMinInvestmentPeriod <= (red - sub) < kMaxInvestmentPeriod. The arithmetic
 * is performed in std::int64_t so that @p sub near UINT32_MAX does not
 * overflow. Shared by VaultCreate::preflight and the ValidVault invariant.
 *
 * @param sub The value of sfSubscriptionDate.
 * @param red The value of sfRedemptionDate.
 */
[[nodiscard]] bool
isValidClosedEndedGap(std::uint32_t sub, std::uint32_t red);

/**
 * Returns the current lifecycle phase of a vault. Open-ended
 * vaults are always NoPhase. For closed-ended vaults the phase is derived
 * from the parent ledger close time and the vault's immutable
 * SubscriptionDate and RedemptionDate.
 *
 * @param view The ledger view whose parent close time is used as the clock.
 * @param vault The vault SLE.
 */
[[nodiscard]] VaultPhase
getVaultPhase(ReadView const& view, SLE::const_ref vault);

/**
 * Raw-fields overload of getVaultPhase. Derives the phase from an already
 * decomposed vault snapshot: an absent or non-ClosedEnded @p vaultKind
 * resolves to VaultPhase::NoPhase; otherwise the phase is computed from
 * @p subscriptionDate and @p redemptionDate against the view's parent
 * close time using the same boundary semantics as the SLE overload
 * (Subscription is inclusive of now == SubscriptionDate; Investment starts
 * strictly after).
 *
 * @param view The ledger view whose parent close time is used as the clock.
 * @param vaultKind The value of sfVaultKind, or nullopt if absent.
 * @param subscriptionDate The value of sfSubscriptionDate, or nullopt if absent.
 * @param redemptionDate The value of sfRedemptionDate, or nullopt if absent.
 */
[[nodiscard]] VaultPhase
getVaultPhase(
    ReadView const& view,
    std::optional<std::uint8_t> vaultKind,
    std::optional<std::uint32_t> subscriptionDate,
    std::optional<std::uint32_t> redemptionDate);

/**
 * Controls whether checkVaultDomain reports an expired credential as an
 * error. A caller that deletes expired credentials later, in doApply, passes
 * Yes and treats the subject as authorized; a caller with no such cleanup
 * step must keep the error.
 */
enum class SuppressExpired : bool { No = false, Yes = true };

/**
 * Checks that subject belongs to the permissioned domain governing a vault's
 * shares.
 *
 * The domain is read from the share issuance rather than from the vault. Vault
 * shares are issued by the vault's pseudo-account, which cannot grant an
 * authorization explicitly, so domain membership is the only route to being
 * authorized: a vault with no domain set has no authorized participants at
 * all, and every subject fails with tecNO_AUTH.
 *
 * Which accounts to check, and whether to check at all, is left to the caller.
 * This says nothing about vault privacy or about the roles of the accounts.
 *
 * @param view The ledger view.
 * @param issuance The MPTokenIssuance SLE for the vault's shares.
 * @param subject The account whose domain membership is checked.
 * @param suppressExpired Whether an expired credential counts as authorized.
 *
 * @return tesSUCCESS if the subject is a domain member, otherwise the reason
 * it is not.
 */
[[nodiscard]] TER
checkVaultDomain(
    ReadView const& view,
    SLE::const_ref issuance,
    AccountID const& subject,
    SuppressExpired suppressExpired);

}  // namespace xrpl
