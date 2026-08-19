#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>
#include <optional>

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
 * Returns the effective total of assets backing outstanding shares for the
 * purposes of a withdrawal, i.e. sfAssetsTotal, discounted by sfLossUnrealized
 * unless waived. This is the numerator used by both withdraw conversion
 * helpers (assetsToSharesWithdraw and sharesToAssetsWithdraw) to compute the
 * share/asset exchange rate.
 *
 * @param vault The vault SLE.
 * @param waive Whether to waive (i.e. not subtract) the vault's unrealized
 *              loss.
 */
[[nodiscard]] Number
assetsTotalForWithdrawal(SLE::const_ref vault, WaiveUnrealizedLoss waive);

/**
 * Returns whether debiting `amount` from `total` — the current value of a
 * vault's sfAssetsTotal or sfAssetsAvailable field — would canonicalize back
 * to the exact same STAmount value it started at. This happens when a
 * genuinely non-zero debit is dust relative to a `total` large enough to
 * exceed STAmount's significant-digit precision: the shares still move, but
 * the stored total doesn't change, which otherwise trips the ValidVault
 * invariant after the fact instead of failing cleanly upfront.
 *
 * @param asset The vault's underlying asset, used to canonicalize both sides
 *              the same way the ledger will when the field is stored.
 * @param total The field's current value.
 * @param amount The amount to debit. A value of zero always returns false;
 *               that case is rejected separately and unconditionally.
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
