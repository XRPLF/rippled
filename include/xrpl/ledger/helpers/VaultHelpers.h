#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

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
 * Returns the scale (number of decimal places) at which a vault's
 * sfAssetsTotal is maintained, derived from the vault's asset and its
 * current sfAssetsTotal value.
 *
 * @param vault The vault SLE.
 *
 * @return The vault's scale, or `Number::kMinExponent - 1` if `vault` is
 * null.
 */
[[nodiscard]] int
getVaultScale(SLE::const_ref vault);

/**
 * The single point through which assets are added to a Vault: updates the
 * Vault's sfAssetsTotal and sfAssetsAvailable and transfers `amount` of the
 * Vault's asset from `sender` to the Vault's pseudo-account.
 *
 * Callers are responsible for rounding `amount` and `valueDelta` to whatever
 * scale is appropriate for their own accounting (e.g. current vs. posterior
 * Vault scale); this helper does not perform any additional rounding.
 *
 * @param view The ledger view to apply changes to.
 * @param vault The vault SLE. Must not be null.
 * @param sender The account to transfer `amount` from.
 * @param amount The amount to add to sfAssetsAvailable, and to transfer from
 *               `sender` to the Vault's pseudo-account.
 * @param valueDelta The amount to add to sfAssetsTotal. May differ from
 *                   `amount`, e.g. when recognizing a value change that is
 *                   not fully backed by a matching cash transfer. May be
 *                   negative (e.g. a default write-off, or a small rounding
 *                   correction), unlike `amount`.
 * @param j Journal for logging.
 *
 * @return TER on success or failure.
 */
[[nodiscard]] TER
addVaultAssets(
    ApplyView& view,
    SLE::ref vault,
    AccountID const& sender,
    STAmount const& amount,
    STAmount const& valueDelta,
    beast::Journal j);

/**
 * Signals that a removal is the last one possible for a Vault — i.e. it
 * burns every outstanding share. removeVaultAssets uses this to hard-reset
 * sfAssetsTotal and sfAssetsAvailable to exactly zero, rather than
 * subtracting `amount`/`valueDelta` from them.
 *
 * This matters because the discounted exchange-rate formula used to compute
 * a withdrawal's `amount` can produce a value with more decimal precision
 * than the Vault's asset can canonically represent. Subtracting such a
 * value from the field would leave a non-canonical residual instead of an
 * exact zero, corrupting the ledger entry. A final removal is defined to
 * exhaust the Vault's exposure entirely, so hard-resetting to zero is both
 * simpler and correct — no residual dust is possible or desired.
 */
enum class FinalRemoval : bool { No = false, Yes = true };

/**
 * The single point through which assets are clawed back from a Vault entirely:
 * decreases the Vault's sfAssetsTotal and sfAssetsAvailable
 * and transfers @p amount from the Vault's pseudo-account to @p recipient via
 * a plain accountSend.
 *
 * Callers are responsible for rounding @p amount to whatever
 * scale is appropriate for their own accounting; this helper does not
 * perform any additional rounding.
 *
 * @param view The ledger view to apply changes to.
 * @param vault The vault SLE. Must not be null.
 * @param recipient The account to transfer `amount` to. Must already be
 *                   able to hold the Vault's asset without further setup.
 * @param amount The amount to clawback from the vault and transfer from the
 *               Vault's pseudo-account to `recipient`. Must be positive;
 *               callers must skip calling this helper entirely for a
 *               zero-amount clawback (unlike addVaultAssets/removeVaultAssets,
 *               which tolerate a zero `amount`).
 * @param j Journal for logging.
 *
 * @return TER code.
 */
[[nodiscard]] TER
clawbackVaultAssets(
    ApplyView& view,
    SLE::ref vault,
    AccountID const& recipient,
    STAmount const& amount,
    beast::Journal j);

/**
 * The single point through which assets are removed from a Vault entirely
 * and withdrawn to a recipient that may not yet have a holding for the
 * Vault's asset: decreases the Vault's sfAssetsTotal and sfAssetsAvailable
 * and calls doWithdraw to transfer @p amount from the
 * Vault's pseudo-account to @p dstAcct.
 *
 * Unlike clawbackVaultAssets, this relies solely on doWithdraw's own
 * pre-transfer balance check rather than an additional post-transfer sanity
 * check.
 *
 * Callers are responsible for rounding @p amount; this helper does not
 * perform any additional rounding, except when `finalRemoval` is Yes (see
 * FinalRemoval).
 *
 * @param ctx The apply-view context to apply changes to.
 * @param vault The vault SLE. Must not be null.
 * @param senderAcct The account that submitted the withdrawal transaction.
 * @param dstAcct The account to transfer `amount` to; may equal `senderAcct`.
 * @param priorBalance The XRP reserve base, passed through to doWithdraw for
 *                      creating a holding for `dstAcct` when required.
 * @param amount The amount to subtract from sfAssetsAvailable, and to
 *               transfer from the Vault's pseudo-account to `dstAcct`.
 *               Ignored (other than for the transfer) when `finalRemoval` is
 *               Yes.
 * @param j Journal for logging.
 * @param finalRemoval Whether this is the Vault's final removal (see
 *                      FinalRemoval).
 *
 * @return TER from doWithdraw.
 */
[[nodiscard]] TER
removeVaultAssets(
    ApplyViewContext ctx,
    SLE::ref vault,
    AccountID const& senderAcct,
    AccountID const& dstAcct,
    XRPAmount priorBalance,
    STAmount const& amount,
    beast::Journal j,
    FinalRemoval finalRemoval = FinalRemoval::No);

/**
 * The single point through which cash is moved out of a Vault's
 * sfAssetsAvailable to multiple recipients in a single atomic payment, e.g.
 * a loan's principal and origination fee, without necessarily shrinking the
 * Vault's total exposure: updates sfAssetsAvailable (decreases by the sum of
 * `recipients`' amounts) and sfAssetsTotal (changes by `valueDelta`, same
 * sign convention as addVaultAssets — typically an increase, since
 * disbursing a loan recognizes accrued interest into sfAssetsTotal even as
 * cash leaves the Vault), then transfers the Vault's asset from the Vault's
 * pseudo-account to each of `recipients`, via accountSendMulti.
 *
 * Unlike removeVaultAssets, this is not a removal — the Vault's receivables
 * grow to match the cash that leaves sfAssetsAvailable, so there is no
 * "final" edge case to handle here.
 *
 * Recipients must already be able to hold the Vault's asset (e.g. via
 * addEmptyHolding and requireAuth performed by the caller beforehand); this
 * helper does not create holdings or check authorization.
 *
 * sfAssetsAvailable is decreased by an STAmount built from the sum of the
 * recipients' Numbers, so for a very large recipient list whose sum exceeds
 * STAmount's ~16 significant digits, this could round differently than
 * summing the underlying Numbers directly. Not a concern for the current
 * caller (LoanSet, two recipients).
 *
 * @param view The ledger view to apply changes to.
 * @param vault The vault SLE. Must not be null.
 * @param recipients The accounts and amounts to transfer from the Vault's
 *                    pseudo-account. Must contain more than one entry.
 * @param valueDelta The amount to add to sfAssetsTotal (same convention as
 *                   addVaultAssets). May be negative, and may differ from
 *                   the sum of `recipients`' amounts.
 * @param j Journal for logging.
 *
 * @return TER from accountSendMulti.
 */
[[nodiscard]] TER
moveVaultAssets(
    ApplyView& view,
    SLE::ref vault,
    MultiplePaymentDestinations const& recipients,
    STAmount const& valueDelta,
    beast::Journal j);

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
 * @namespace vault_dust
 *
 * Vault-side adopter of the trust-line dust mechanism (see
 * `xrpl::DustSplit` in TokenHelpers.h, and docs/dust-mechanism.md).
 * Owns the Vault-level orchestration only: an eligibility gate
 * (`useVaultDust`) and dust-aware overloads of the four base Vault
 * helpers, with identical signatures so the transactor call sites can
 * stay agnostic.
 *
 * Non-Vault features that want the same primitives should add a sibling
 * namespace with their own gate and helper overloads — do not extend
 * this namespace.
 *
 * The four helper overloads here are the ONLY code in the tree that
 * constructs a `xrpl::DustSplit`; every other caller uses the base
 * helpers verbatim.
 */
namespace vault_dust {

/**
 * Whether this Vault's custody trust line participates in the sfDust
 * mechanism. True only when featureLendingProtocolV1_1 is enabled AND the
 * Vault is cash-basis (sfLEVersion == VaultVersion::CashBasis) AND its
 * asset is an IOU. Every other case (Legacy vault, integral asset,
 * amendment disabled) skips every dust-aware code path.
 *
 * See directSendNoFeeIOU for the canonical amendment-gate rationale.
 *
 * @param view The ledger view (for amendment lookup).
 * @param vault The vault SLE.
 */
[[nodiscard]] bool
useVaultDust(ReadView const& view, SLE::const_ref vault);

/**
 * Dust-aware overload of `xrpl::addVaultAssets`. Same signature as the
 * base version. The dispatcher in `xrpl::addVaultAssets` forwards here
 * when `useVaultDust(view, vault)` returns true.
 *
 * The credit path uses a `DustSplit` targeting the Vault's posterior
 * scale (the scale implied by `sfAssetsTotal + valueDelta`, an upper
 * bound on the Vault's post-op scale), so any sub-quantum remainder in
 * `amount` lands in the custody line's `sfDust` rather than being lost.
 * Both `sfAssetsTotal` and `sfAssetsAvailable` are updated with the split
 * outputs so the receivable (`sfAssetsTotal - sfAssetsAvailable`) is
 * identical to what a dust-unaware call would produce.
 */
[[nodiscard]] TER
addVaultAssets(
    ApplyView& view,
    SLE::ref vault,
    AccountID const& sender,
    STAmount const& amount,
    STAmount const& valueDelta,
    beast::Journal j);

/**
 * Dust-aware overload of `xrpl::clawbackVaultAssets`. Same signature as
 * the base version. The dispatcher forwards here when
 * `useVaultDust(view, vault)` returns true.
 *
 * A clawback shrinks `sfAssetsTotal`, which can refine the Vault's
 * scale. This overload drives the transfer through a sender-leg
 * `DustSplit::LegPolicy::Mode::Override` at the Vault's posterior
 * scale; the trust-line layer re-splits `sfBalance`/`sfDust` on the
 * custody line and reports any promoted (or newly-deferred) sub-quantum
 * residual so the Vault fields stay aligned with the extended balance.
 */
[[nodiscard]] TER
clawbackVaultAssets(
    ApplyView& view,
    SLE::ref vault,
    AccountID const& recipient,
    STAmount const& amount,
    beast::Journal j);

/**
 * Dust-aware overload of `xrpl::removeVaultAssets`. Same signature as the
 * base version. The dispatcher forwards here when
 * `useVaultDust(ctx.view, vault)` returns true.
 *
 * Non-terminal (`FinalRemoval::No`): drives the withdrawal through a
 * sender-leg `DustSplit::LegPolicy::Mode::Override` at the Vault's
 * posterior scale, so any dust stranded on the custody line by a
 * scale-refining update is renormalised inside the trust-line layer
 * (no separate promotion pass in Vault code).
 *
 * Terminal (`FinalRemoval::Yes`): drives the withdrawal through a
 * sender-leg `DustSplit::LegPolicy::Mode::Drain`. The trust-line layer
 * folds the custody line's `sfDust` reservoir into `sfBalance`,
 * inflates the outgoing transfer by the reservoir so the destination
 * receives `amount + drainedDust`, and zeroes `sfDust` — leaving the
 * line with `sfBalance == 0` and `sfDust == 0`, a precondition for
 * downstream Vault-cleanup deletion guards.
 */
[[nodiscard]] TER
removeVaultAssets(
    ApplyViewContext ctx,
    SLE::ref vault,
    AccountID const& senderAcct,
    AccountID const& dstAcct,
    XRPAmount priorBalance,
    STAmount const& amount,
    beast::Journal j,
    FinalRemoval finalRemoval = FinalRemoval::No);

/**
 * Dust-aware overload of `xrpl::moveVaultAssets`. Same signature as the
 * base version. The dispatcher forwards here when
 * `useVaultDust(view, vault)` returns true.
 *
 * A multi-recipient move (typically a loan disbursement) is a
 * cash-out-plus-recognition on the Vault side; it shrinks
 * `sfAssetsAvailable` and may change `sfAssetsTotal` via `valueDelta`.
 * Both changes can refine the Vault's scale, so this overload attaches
 * a sender-leg `DustSplit::LegPolicy::Mode::Override` at the Vault's
 * posterior scale to `accountSendMulti`; the trust-line layer
 * renormalises any newly-representable dust on the custody line during
 * the bulk sender-line debit.
 */
[[nodiscard]] TER
moveVaultAssets(
    ApplyView& view,
    SLE::ref vault,
    MultiplePaymentDestinations const& recipients,
    STAmount const& valueDelta,
    beast::Journal j);

}  // namespace vault_dust

}  // namespace xrpl
