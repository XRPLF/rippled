#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>
#include <expected>
#include <optional>

namespace xrpl {

class STTx;

/**
 * @brief Return the Vault's current assets total.
 *
 * FixedPrecision Vaults return AssetsAvailable plus AssetsDeployed, with no
 * rounding to the asset's 16-digit precision: the sum is exact at Number's
 * active 19-digit mantissa width (the large mantissa range, enabled by
 * featureSingleAssetVault or featureLendingProtocol), rounds Downward regardless of the
 * caller's ambient mode beyond that, and is not rounded to match the cached
 * sfAssetsTotal field. Legacy and CashBasis Vaults return the stored
 * AssetsTotal.
 *
 * @param vault The vault SLE.
 * @return The current assets total.
 */
[[nodiscard]] Number
getAssetsTotal(SLE::ConstRef vault);

/**
 * @brief A change to a FixedPrecision Vault's balances.
 */
struct VaultBalanceChange
{
    /**
     * Signed change to AssetsAvailable. Must be the exact amount moved on the
     * Vault pseudo-account in the same transaction. Absent means no cash
     * moved; adjustVaultBalances then uses zero of the Vault's own asset.
     */
    std::optional<STAmount> cash = std::nullopt;

    /**
     * Signed change to AssetsDeployed: open Loan principal, exact.
     */
    Number deployed = 0;

    /**
     * Signed change to scheduled, unpaid interest (YieldUnrealized), exact.
     */
    Number yield = 0;

    /**
     * Signed change to impaired principal (LossUnrealized), exact.
     */
    Number loss = 0;
};

/**
 * @brief Apply a balance change to a FixedPrecision Vault. The only writer of
 * AssetsAvailable, AssetsDeployed, AssetsTotal, YieldUnrealized and
 * LossUnrealized on these Vaults after creation.
 *
 * @param vault The vault SLE. Must be FixedPrecision.
 * @param change The balance change to apply.
 * @param j Journal for logging the fatal (tefBAD_LEDGER) and warning
 *          (YieldUnrealized clamp) cases.
 *
 * @return tesSUCCESS; tecLIMIT_EXCEEDED if LossUnrealized would exceed
 *         AssetsDeployed; tefBAD_LEDGER if AssetsAvailable, AssetsDeployed or
 *         LossUnrealized would become negative.
 */
[[nodiscard]] TER
adjustVaultBalances(SLE::Ref vault, VaultBalanceChange const& change, beast::Journal j);

/**
 * Return the Vault's current live exponent.
 *
 * Legacy and CashBasis Vaults use the exponent of AssetsTotal. FixedPrecision
 * Vaults use max(base exponent, exponent of AssetsTotal): the grid is never
 * finer than -Scale and coarsens once AssetsTotal outgrows 16 digits there.
 */
[[nodiscard]] int
getVaultScale(SLE::ConstRef vault);

/**
 * Return the Vault's base exponent.
 *
 * Legacy and CashBasis Vaults use their current live exponent. FixedPrecision
 * Vaults use -Scale, or 0 for integral assets.
 */
[[nodiscard]] int
getVaultBaseScale(SLE::ConstRef vault);

/**
 * Round an amount at the Vault's posterior live exponent.
 */
[[nodiscard]] STAmount
roundToPosteriorVaultScale(
    SLE::ConstRef vault,
    STAmount const& amount,
    Number::RoundingMode roundingMode);

namespace detail {

/**
 * Return the Vault's posterior live exponent after applying an unrounded delta.
 */
[[nodiscard]] int
getPosteriorVaultScale(SLE::ConstRef vault, STAmount const& delta);

/**
 * Shared grid-floor core of getVaultScale / getPosteriorVaultScale /
 * getPosteriorBrokerCoverScale: baseScale when reference is zero, otherwise
 * max(baseScale, scale(reference, asset)), rounded Downward regardless of
 * the caller's ambient rounding mode.
 */
[[nodiscard]] int
liveScale(Number const& reference, Asset const& asset, int baseScale);

/**
 * Shared core of getPosteriorVaultScale and getPosteriorBrokerCoverScale:
 * the exponent reference + delta would have under version's live-scale rule
 * (floored at baseScale for FixedPrecision, unbounded for Legacy/CashBasis).
 *
 * @param version The Vault's LEVersion.
 * @param asset The Vault's underlying asset.
 * @param baseScale The Vault's base exponent; unused outside the
 *                   FixedPrecision case.
 * @param reference The balance the delta is applied to (AssetsAvailable,
 *                   AssetsTotal, or a LoanBroker's CoverAvailable).
 * @param delta The unrounded change to reference.
 */
[[nodiscard]] int
posteriorAssetScale(
    VaultVersion version,
    Asset const& asset,
    int baseScale,
    Number const& reference,
    Number const& delta);

/**
 * Shared core of creditToPosteriorAvailableScale and
 * creditToPosteriorBrokerCoverScale: floors reference + raw at scale and
 * returns the difference from reference, so the delta applied is exactly
 * what moves reference onto the floored sum. See
 * creditToPosteriorAvailableScale's doc for why the sum, not raw alone,
 * must be floored. raw is the exact amount; it is not rounded on its own
 * before being added to reference.
 */
[[nodiscard]] STAmount
creditToPosteriorScale(
    Asset const& asset,
    Number const& reference,
    int atScale,
    Number const& raw,
    Number::RoundingMode roundingMode);

}  // namespace detail

/**
 * Round a FixedPrecision cash inflow (LoanPay's credit, LoanManage's
 * default cover credit) into AssetsAvailable, never AssetsTotal, which is
 * a derived cache, by flooring the sum rather than the delta:
 * credit equals floor16(AssetsAvailable + raw) minus AssetsAvailable, never
 * finer than the Vault's base exponent (-Scale).
 *
 * Flooring only the delta at its own posterior grid is not enough: the sum
 * can still need a 17th digit when AssetsAvailable is off the coarser grid
 * right after crossing a power of ten. Example at Scale 6: AssetsAvailable
 * equals 9999999999.999999 (16 digits already) and a credit of 0.000011
 * floors to 0.00001 on its own (finer) grid, but 9999999999.999999 plus
 * 0.00001 equals 10000000000.000009, which still needs 17 digits. Flooring
 * the sum instead, floor16(9999999999.999999 + 0.000011) minus
 * 9999999999.999999, is exact by construction, since STAmount's own
 * 16-digit canonical form of the sum is what gets subtracted from.
 *
 * raw is taken as an exact Number: rounding it to 16 digits before adding
 * it to AssetsAvailable could drop a tail that moves the floored sum.
 */
[[nodiscard]] STAmount
creditToPosteriorAvailableScale(
    SLE::ConstRef vault,
    Number const& raw,
    Number::RoundingMode roundingMode);

/**
 * Open-zone capacity ceiling: 9 * 10^(15 + baseScale).
 *
 * Defined only for FixedPrecision Vaults, where this is 9 * 10^(15 - P).
 */
[[nodiscard]] Number
getVaultOpenLimit(SLE::ConstRef vault);

/**
 * Check whether amount is an admissible optional inflow.
 *
 * Legacy and CashBasis Vaults always succeed. FixedPrecision Vaults must
 * remain at their base scale after applying the rounded amount, and the
 * posterior capacity (AssetsTotal + YieldUnrealized + rounded amount) must
 * stay within the Open zone.
 *
 * The amount is rounded toward zero at the posterior live exponent.
 */
[[nodiscard]] TER
checkOptionalVaultInflow(SLE::ConstRef vault, STAmount const& amount);

/**
 * Open-zone capacity after adding roundedAmount: AssetsTotal + YieldUnrealized
 * + roundedAmount, computed TowardsZero regardless of the caller's ambient
 * rounding mode. Defined only for FixedPrecision Vaults.
 *
 * Shared by checkOptionalVaultInflow and LoanSet's loan-origination capacity
 * check, which pair this formula with different scale checks (posterior vs.
 * current), so only the capacity formula itself -- not the whole check -- is
 * shared here.
 *
 * @param vault The vault SLE. Must be FixedPrecision.
 * @param roundedAmount The amount to add, already rounded to the relevant
 *                       posterior or base scale by the caller.
 */
[[nodiscard]] Number
vaultOpenZoneCapacity(SLE::ConstRef vault, Number const& roundedAmount);

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
assetsToSharesDeposit(SLE::ConstRef vault, SLE::ConstRef issuance, STAmount const& assets);

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
sharesToAssetsDeposit(SLE::ConstRef vault, SLE::ConstRef issuance, STAmount const& shares);

/**
 * Adjusts a requested asset change (delta) to match the decimal scale of the
 * updated total vault assets. This ensures sfAssetsTotal, sfAssetsAvailable,
 * and the actual asset transfer change by the exact same representable amount.
 *
 * Rounding strategy:
 * - Legacy/CashBasis debits (withdrawals): rounds down the magnitude of delta
 *   on the new scale to prevent paying out more than requested.
 * - Legacy/CashBasis credits: floors the resulting total asset balance and
 *   returns the difference from the current total.
 * - FixedPrecision credits only (VaultDeposit): rounds the delta toward zero
 *   at the scale getAssetsTotal (AssetsAvailable + AssetsDeployed, derived,
 *   not the stored AssetsTotal cache) would have after applying delta.
 *   FixedPrecision outflows (VaultWithdraw, VaultClawback) never call this;
 *   they use clampVaultOutflow instead.
 *
 * Key rules:
 * - The returned magnitude never exceeds the magnitude of delta.
 * - Returns tecPRECISION_LOSS if the change is smaller than 1 ULP of the target scale
 *   (prevents share operations when totals cannot change).
 * - For integer assets (XRP, MPT), rounding is a no-op.
 *
 * @param vault The vault ledger entry.
 * @param delta The requested signed change to sfAssetsTotal.
 * @return The rounded, positive magnitude, or tecPRECISION_LOSS if the
 *         change is below representable precision.
 */
[[nodiscard]] std::expected<STAmount, TER>
clampToAssetsTotalScale(SLE::ConstRef vault, STAmount const& delta);

/**
 * Outflow clamp dispatcher for VaultWithdraw and VaultClawback: rounds a
 * negative delta to the scale appropriate for the Vault's version -- for
 * FixedPrecision, at AssetsAvailable's own posterior scale (the balance the
 * cash actually leaves); for Legacy/CashBasis, clampToAssetsTotalScale --
 * so both call sites share one dispatch point. Same tecPRECISION_LOSS and
 * integral-asset and magnitude-never-exceeds-delta rules as
 * clampToAssetsTotalScale.
 *
 * @param vault The vault ledger entry.
 * @param delta The requested signed outflow (negative).
 * @return The rounded, positive magnitude, or tecPRECISION_LOSS if the
 *         change is below representable precision.
 */
[[nodiscard]] std::expected<STAmount, TER>
clampVaultOutflow(SLE::ConstRef vault, STAmount const& delta);

/**
 * Returns the Vault's base exponent for asset at scale, before a Vault
 * object exists to read it from: 0 for an integral asset, -scale otherwise.
 * Used by VaultCreate::preclaim, which only has the proposed Scale field,
 * not yet a Vault SLE; getVaultBaseScale is the post-creation equivalent.
 *
 * @param asset The (prospective) Vault's underlying asset.
 * @param scale The (prospective) Vault's sfScale value, ignored for
 *              integral assets.
 */
[[nodiscard]] int
vaultBaseScale(Asset const& asset, std::uint8_t scale);

/**
 * Returns true iff value is exactly representable both as an STAmount of
 * asset and on the base grid at baseScale (no precision lost rounding
 * TowardsZero at baseScale). Shared representability check for
 * VaultCreate::preclaim, VaultSet::preclaim and LoanBrokerSet::preclaim's
 * AssetsMaximum / DebtMaximum / cover-rate field checks.
 *
 * @param asset The vault's underlying asset.
 * @param value The value to check.
 * @param baseScale The vault's base exponent (see vaultBaseScale /
 *                  getVaultBaseScale).
 */
[[nodiscard]] bool
isOnVaultBaseGrid(Asset const& asset, Number const& value, int baseScale);

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
assetsTotalForWithdrawal(SLE::ConstRef vault, WaiveUnrealizedLoss waive);

/**
 * Returns true if debiting amount from total (the current value of a
 * vault's sfAssetsTotal or sfAssetsAvailable) would canonicalize to the
 * same STAmount value. This happens when amount is non-zero but too small
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
 * Returns the reference balance a cash outflow's dust check
 * (debitIsNonZeroDust) should compare against: FixedPrecision Vaults use
 * AssetsAvailable, the balance the cash actually leaves; Legacy/CashBasis
 * Vaults use the given AssetsTotal. Shared by VaultWithdraw and
 * VaultClawback.
 *
 * @param vault The vault SLE.
 * @param assetsTotal The vault's current AssetsTotal, as already read by the
 *                     caller.
 */
[[nodiscard]] Number
vaultDebitDustReference(SLE::ConstRef vault, Number const& assetsTotal);

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
    SLE::ConstRef vault,
    SLE::ConstRef issuance,
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
    SLE::ConstRef vault,
    SLE::ConstRef issuance,
    STAmount const& shares,
    WaiveUnrealizedLoss waive = WaiveUnrealizedLoss::No);

/**
 * Returns true iff account holds all of the vault's outstanding shares,
 * i.e. is the sole remaining shareholder. Returns false if the account
 * holds no shares or fewer than the total outstanding.
 *
 * @param view The ledger view.
 * @param account The candidate sole shareholder.
 * @param issuance The MPTokenIssuance SLE for the vault's shares; provides
 *                 both the share MPTID and the outstanding-amount total.
 */
[[nodiscard]] bool
isSoleShareholder(ReadView const& view, AccountID const& account, SLE::ConstRef issuance);

/**
 * Resolves a Vault's LEVersion.
 *
 * LEVersion is the single point every accounting and rounding helper
 * should call to decide which protocol a Vault follows. It is written
 * at VaultCreate and is not updated afterwards, so a Vault created
 * under an older amendment keeps that behaviour after later amendments
 * activate. Absent sfLEVersion resolves to VaultVersion::Legacy.
 *
 * @param vault The vault SLE.
 *
 * @return The Vault's LEVersion, or VaultVersion::Legacy if the field is
 * absent.
 */
[[nodiscard]] VaultVersion
getVaultVersion(SLE::ConstRef vault);

/**
 * Decodes an already-extracted sfLEVersion value with the same range check as
 * getVaultVersion. Usable from contexts, such as an invariant's ledger-entry
 * snapshot, that keep the field value but not the owning SLE.
 *
 * @param leVersion The value of sfLEVersion, or nullopt if absent.
 *
 * @return The decoded LEVersion, or VaultVersion::Legacy if absent.
 */
[[nodiscard]] VaultVersion
decodeVaultVersion(std::optional<std::uint8_t> leVersion);

/**
 * Resolves which LEVersion a newly-created Vault should get under the
 * currently active amendments. This is the only place that decides Vault
 * version policy; VaultCreate calls it instead of checking amendments
 * directly.
 *
 * @param rules The active ledger rules.
 *
 * @return VaultVersion::FixedPrecision once featureLendingProtocolV1_2,
 * fixCleanup3_2_0 and fixCleanup3_4_0 are all enabled; VaultVersion::CashBasis
 * once featureLendingProtocolV1_1 is enabled; VaultVersion::Legacy otherwise.
 */
[[nodiscard]] VaultVersion
vaultVersionFor(Rules const& rules);

/**
 * Resolves the VaultKind of a vault SLE. Returns VaultKind::ClosedEnded when
 * sfVaultKind is present and equal to that value; anything else (including an
 * absent field or an unrecognised value) is treated as VaultKind::OpenEnded.
 *
 * @param vault The vault SLE.
 */
[[nodiscard]] VaultKind
getVaultKind(SLE::ConstRef vault);

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
getVaultPhase(ReadView const& view, SLE::ConstRef vault);

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
    SLE::ConstRef issuance,
    AccountID const& subject,
    SuppressExpired suppressExpired);

}  // namespace xrpl
