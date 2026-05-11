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

// =============================================================================================
// Vault asset accounting precision (fixCleanup3_2_0)
// =============================================================================================
//
// A vault tracks two STNumber accounting fields:
//   - sfAssetsTotal       — total accounted assets, including outstanding loan value.
//   - sfAssetsAvailable   — assets the vault pseudo-account actually holds (mirrors its IOU TL).
//
// With outstanding loans, sfAssetsAvailable < sfAssetsTotal, and the two can land in different
// IOU exponent bands. Different bands → different ULPs.
//
// Bug — silent SLE asymmetric rounding: when the fields sit at different scales, the same request
// canonicalizes differently on each. The pseudo-account trustline and sfAssetsAvailable apply the
// exact delta; sfAssetsTotal rounds to the nearest multiple of its (coarser) ULP, drifting by up to
// one ULP. Repeated ops accumulate dust until the vault conservation invariant trips.
//
//   Example: sfAssetsTotal at 1.5×10^16 (ULP=10), sfAssetsAvailable at 5×10^15 (ULP=1).
//   A 7-unit withdrawal: pseudo TL and sfAssetsAvailable lose 7 (exact); sfAssetsTotal loses 10.
//   sfAssetsTotal under-states accounted assets by 3.
//
// Defense layers (post-fixCleanup3_2_0):
//
//   1. canApplyToVault preclaim guard. Two legs, both return tecPRECISION_LOSS:
//        a. Rail sub-ULP — reject when the request rounds to zero at sfAssetsAvailable's scale
//           (silent absorption). Uses roundsToZeroAtScale().
//        b. Coarser-scale lossless — only when sfAssetsTotal sits coarser than sfAssetsAvailable
//            require the request rounds losslessly at sfAssetsTotal's scale. Uses
//            roundsLosslesslyAtScale(). Skipping the leg when scales match avoids over-
//           rejecting legitimate same-scale fractional ops (e.g. share-denominated withdraws
//           whose NAV-derived asset amount has fractional precision). Same-scale tail
//           absorption on deposits is handled by caller-side clamping in
//           VaultDeposit::doApply rather than predicate rejection — the user pays only the
//           clamped amount, the remainder stays in their wallet, and shares mint on the
//           clamped value (no dilution).
//
//   2. accountSendExact wraps every value transfer on these rails: depositToVault, via
//      doWithdraw in withdrawFromVault, and the direct issuer-bound transfer in VaultClawback.
//      Two-sided shape compares sender loss to receiver gain across both trust lines;
//      destroy shape (to == issuer) verifies the sender's delta in isolation, catching
//      sub-ULP sender-side absorption that the preclaim guard can't anticipate when the
//      holder's trust line sits at a different scale than sfAssetsAvailable.
//
//   3. equalAtAssetScale cross-check (in depositToVault and withdrawFromVault). Defense-in-depth
//      at the coarser field's scale; lenient by design (tolerates the 1-ULP drift the preclaim
//      guard catches upfront) and guards against grosser corruption.
//
// Trade-off — granularity floor: in the loan-trapped state, leg (b) rejects any request that
// isn't a clean multiple of the coarser ULP. A holder with a 7-unit stake (per the example)
// can't withdraw via any denomination until outstanding loans repay and sfAssetsAvailable grows
// back into sfAssetsTotal's exponent band. Deposit-side same-scale clamping (see
// VaultDeposit::doApply) sidesteps this for new deposits by transferring only the representable
// portion of the user's request and leaving the remainder in the depositor's wallet.
//
// Applies symmetrically to VaultDeposit, VaultWithdraw, VaultClawback.
//
// =============================================================================================

/** Vault preclaim precision guard. Catches the two-part bug class documented
 *  above: silent absorption on the rail (request rounds to zero at
 *  sfAssetsAvailable's scale) and asymmetric SLE rounding in the loan-trapped
 *  state (request fails lossless canonicalization at sfAssetsTotal's coarser
 *  scale). Same-scale tail absorption is intentionally not caught here —
 *  VaultDeposit::doApply applies caller-side clamping for that case.
 *
 *  Returns tesSUCCESS when the amendment is disabled, when amount is zero,
 *  or when the request passes both legs. Returns tecPRECISION_LOSS on
 *  rejection. The amount==0 short-circuit lets drained-vault and "all
 *  available" sentinel callers fall through to their downstream
 *  INSUFFICIENT_FUNDS / INTERNAL handling rather than re-encoding the
 *  amount==0 contract at every call site.
 *
 *  Share-denominated requests must be converted to their asset equivalent
 *  via sharesToAssetsWithdraw before invoking; the doApply SLE subtraction
 *  operates on the converted value, so the predicate must too. The STAmount
 *  argument carries the vault asset for the log message — the predicate
 *  itself only reads the numeric magnitude.
 *
 *  @param view       Apply view (rules used for amendment gating).
 *  @param vault      The vault SLE (read-only).
 *  @param amount     Asset-denominated effective subtraction/addition amount.
 *  @param j          Journal for logging.
 *  @param logPrefix  Transactor name for log diagnostics (e.g. "VaultDeposit").
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
    SLE::ref vault,
    AccountID const& depositor,
    STAmount const& assetsDeposited,
    beast::Journal j);

/** Apply the asset-side of a vault withdrawal.

    Decrements the vault's asset accounting fields, transfers shares from the
    depositor back to the vault pseudo-account (and removes their empty
    MPToken if applicable), runs `associateAsset` on the vault SLE, then
    transfers the requested assets from the vault pseudo-account to the
    destination via `doWithdraw`. Post-amendment, verifies that the resulting
    deltas match the requested amount and returns `tecPRECISION_LOSS` on
    silent rounding mismatches before the withdrawal invariants would fire.

    @param view The apply view.
    @param tx The triggering transaction (forwarded to `doWithdraw` for
        deposit-preauth and destination-tag checks).
    @param vault The vault SLE (peeked, will be mutated).
    @param depositor The account redeeming shares.
    @param destination The account receiving the assets (may equal depositor).
    @param preFeeBalance The depositor's pre-fee XRP balance, forwarded to
        `doWithdraw` and used by `addEmptyHolding` to verify the reserve when
        destination == depositor and a new trust line / MPToken needs to be
        created for the inbound assets.
    @param assetsWithdrawn The amount of assets to be withdrawn.
    @param sharesRedeemed The amount of shares to be burned in exchange.
    @param j Journal for logging.

    @return tesSUCCESS on success; tecINSUFFICIENT_FUNDS if the vault's
            available balance is too low; tecPRECISION_LOSS if any ledger
            field or trust-line balance changed by an amount different than
            `assetsWithdrawn`; otherwise the result of the underlying
            `accountSend`, `removeEmptyHolding`, or `doWithdraw`.
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
