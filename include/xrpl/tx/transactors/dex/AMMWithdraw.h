#pragma once

#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

class Sandbox;

/** Sentinel flag used to enable exact-zero semantics in final-LP-token withdrawals.
 *
 *  When a liquidity provider redeems their entire LP position, rounding in the
 *  constant-product math can leave dust amounts. `WithdrawAll::Yes` tells
 *  `equalWithdrawTokens` and `withdraw` to apply exact-zero arithmetic instead
 *  of ratio arithmetic, ensuring the pool is properly drained to zero.
 *  `isWithdrawAll` decodes this from the transaction's `tfWithdrawAll` or
 *  `tfOneAssetWithdrawAll` flag bits.
 */
enum class WithdrawAll : bool { No = false, Yes };

/** Transactor for `AMMWithdraw` transactions (XLS-30d).
 *
 *  Burns LP tokens to return underlying pool assets to the liquidity provider.
 *  Five mutually exclusive withdrawal modes are supported, selected by a single
 *  flag bit from `tfWithdrawSubTx`:
 *
 *  | Flag                   | Fields required              | Fee? | Description |
 *  |------------------------|------------------------------|:----:|-------------|
 *  | `tfLPToken`            | `sfLPTokenIn`                | No   | Proportional dual-asset withdrawal for a token amount |
 *  | `tfTwoAsset`           | `sfAmount` + `sfAmount2`     | No   | Proportional dual-asset withdrawal with per-asset caps |
 *  | `tfSingleAsset`        | `sfAmount`                   | Yes  | Single-asset withdrawal by amount |
 *  | `tfOneAssetLPToken`    | `sfAmount` + `sfLPTokenIn`   | Yes  | Single-asset withdrawal proportional to a token amount |
 *  | `tfLimitLPToken`       | `sfAmount` + `sfEPrice`      | Yes  | Single-asset withdrawal with effective-price ceiling |
 *
 *  Fee-free modes (`tfLPToken`, `tfTwoAsset`) remove liquidity proportionally
 *  without disturbing the pool's price ratio. Fee-bearing modes are equivalent
 *  to an implicit swap followed by a proportional withdrawal; the pool's
 *  trading fee applies to that swap component.
 *
 *  The `tfTwoAsset` mode computes the largest proportional withdrawal fitting
 *  within both asset caps, so actual amounts may be less than the stated maxima.
 *
 *  On success: XRP transfers adjust `sfBalance` on both accounts directly; IOU
 *  and MPT withdrawals move balances through trustlines between the AMM account
 *  and the LP. LP tokens are burned from the LP's holding.
 *
 *  Two methods — `equalWithdrawTokens` and `withdraw` — are `public static` so
 *  that `AMMDelete` and `AMMClawback` can invoke the withdrawal machinery without
 *  constructing a full `AMMWithdraw` transactor instance. All five mode-specific
 *  helpers are `private`.
 *
 *  @see [XLS-30d AMMWithdraw](https://github.com/XRPLF/XRPL-Standards/discussions/78)
 */
class AMMWithdraw : public Transactor
{
public:
    /** Standard fee/sequence consequences; no special queue-blocking behavior. */
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit AMMWithdraw(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Gate on the `featureAMM` amendment and, for MPT-backed pools, on
     *  `featureMPTokensV2`.
     *
     *  Returns `false` if `featureAMM` is not active. Also returns `false` if
     *  the pool asset pair or any withdrawal amount uses an `MPTIssue` but
     *  `featureMPTokensV2` has not yet activated.
     *
     *  @param ctx  The preflight context providing rules and the transaction.
     *  @return `true` if the transaction type is permitted under current rules.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Return the valid transaction flag mask (`tfAMMWithdrawMask`).
     *
     *  The framework uses this to reject any transaction whose flags fall
     *  outside the set of bits defined for `AMMWithdraw`.
     *
     *  @param ctx  Unused; present for the static dispatch interface.
     *  @return     Bitmask of all valid `AMMWithdraw` flags.
     */
    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    /** Validate the transaction structure without accessing ledger state.
     *
     *  Enforces:
     *  - Exactly one flag bit from `tfWithdrawSubTx` is set
     *    (`std::popcount == 1`); otherwise `temMALFORMED`.
     *  - Per-mode field constraints (required and forbidden optional fields).
     *  - Asset-pair validity via `invalidAMMAssetPair`.
     *  - `sfAmount` and `sfAmount2` denominate different assets.
     *  - `sfLPTokenIn`, if present, is positive.
     *  - `sfEPrice`, `sfAmount`, and `sfAmount2` amounts are valid via
     *    `invalidAMMAmount`.
     *
     *  @param ctx  The preflight context providing the transaction and rules.
     *  @return `tesSUCCESS` on success, or a `tem*` error code.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Validate against current ledger state (read-only).
     *
     *  Runs after signature verification. Key checks:
     *  - The AMM ledger entry for the declared asset pair must exist;
     *    absent → `terNO_AMM`.
     *  - LP token balance must be positive (`tecAMM_EMPTY` if the pool is
     *    already drained).
     *  - The caller holds a sufficient LP token balance for the requested
     *    withdrawal.
     *  - When `featureAMMClawback` is active: neither pool asset may be
     *    individually frozen on the LP's account (`tecFROZEN`).
     *  - MPT-specific authorization via `checkMPTTxAllowed`.
     *
     *  @param ctx  The preclaim context providing the read-only ledger view.
     *  @return `tesSUCCESS` on success, or the appropriate `tec*`/`ter*` code.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Execute the withdrawal against a copy-on-write sandbox and commit on success.
     *
     *  Creates a `Sandbox` over `ctx_.view()`, delegates to `applyGuts`, and
     *  calls `sb.apply(ctx_.rawView())` only if `applyGuts` returns success.
     *  Any failure leaves the consensus view unmodified.
     *
     *  @return `tesSUCCESS` or the `tec*` error from the selected withdrawal mode.
     */
    TER
    doApply() override;

    /** No-op placeholder for future transaction-specific invariant state.
     *
     *  `AMMWithdraw` currently delegates all invariant checking to the global
     *  `ValidAMM` invariant checker. This override exists to satisfy the
     *  `Transactor` interface and is reserved for future per-transaction
     *  invariants.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** No-op placeholder for future transaction-specific invariant finalization.
     *
     *  Always returns `true`. See `visitInvariantEntry` for rationale.
     *
     *  @return `true` unconditionally.
     */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;

    /** Proportional dual-asset withdrawal for a given LP token amount.
     *
     *  Burns `lpTokensWithdraw` LP tokens and returns both pool assets to
     *  `account` in the ratio `lpTokensWithdraw / lptAMMBalance`. No trading
     *  fee is charged because the withdrawal preserves the pool's price ratio.
     *  When `withdrawAll` is `WithdrawAll::Yes`, exact-zero arithmetic is used
     *  to prevent dust from rounding errors on full-position redemptions.
     *
     *  This overload is `public static` so `AMMDelete` and `AMMClawback` can
     *  invoke it without constructing an `AMMWithdraw` transactor. The caller
     *  supplies `freezeHandling`, `authHandling`, `priorBalance`, and a journal
     *  that the instance methods obtain from `ctx_` automatically.
     *
     *  @param view             Sandbox to mutate; caller commits only on success.
     *  @param ammSle           AMM ledger entry.
     *  @param account          LP account receiving the withdrawn assets.
     *  @param ammAccount       AMM pseudo-account ID.
     *  @param amountBalance    Current pool balance of asset1.
     *  @param amount2Balance   Current pool balance of asset2.
     *  @param lptAMMBalance    Current total LP token supply.
     *  @param lpTokens         LP token balance held by `account`.
     *  @param lpTokensWithdraw LP tokens to burn.
     *  @param tfee             Pool trading fee in basis points (unused for fee
     *                              charging; passed to amount-adjustment helpers).
     *  @param freezeHandling   Whether to zero out frozen trustline transfers.
     *  @param authHandling     Whether to zero out unauthorized trustline transfers.
     *  @param withdrawAll      `Yes` if the LP is redeeming their entire position.
     *  @param priorBalance     Caller's XRP balance before fee deduction
     *                              (used for reserve checks during asset transfer).
     *  @param journal          For error logging.
     *  @return A tuple of `{TER, newLPTokenBalance, asset1Withdrawn, asset2Withdrawn}`.
     *      Returns `{tecINTERNAL, ...}` if an unexpected exception is caught.
     */
    static std::tuple<TER, STAmount, STAmount, std::optional<STAmount>>
    equalWithdrawTokens(
        Sandbox& view,
        SLE const& ammSle,
        AccountID const account,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& amount2Balance,
        STAmount const& lptAMMBalance,
        STAmount const& lpTokens,
        STAmount const& lpTokensWithdraw,
        std::uint16_t tfee,
        FreezeHandling freezeHandling,
        AuthHandling authHandling,
        WithdrawAll withdrawAll,
        XRPAmount const& priorBalance,
        beast::Journal const& journal);

    /** Transfer withdrawn assets from the AMM account to the LP and burn LP tokens.
     *
     *  Moves `amountWithdraw` of asset1 (and optionally `amount2Withdraw` of
     *  asset2) from `ammAccount` to `account`, then burns `lpTokensWithdraw`
     *  tokens. The trading fee is charged on single-asset modes (the caller is
     *  responsible for passing the correct `tfee`; fee-free callers pass 0).
     *  When `withdrawAll` is `WithdrawAll::Yes`, exact-zero arithmetic prevents
     *  dust from rounding errors on full-position redemptions.
     *
     *  This overload is `public static` so `AMMDelete` and `AMMClawback` can
     *  invoke it without constructing an `AMMWithdraw` transactor. The caller
     *  supplies `freezeHandling`, `authHandling`, `priorBalance`, and a journal
     *  that the instance methods obtain from `ctx_` automatically.
     *
     *  @param view               Sandbox to mutate; caller commits only on success.
     *  @param ammSle             AMM ledger entry.
     *  @param ammAccount         AMM pseudo-account ID.
     *  @param account            LP account receiving the withdrawn assets.
     *  @param amountBalance      Current pool balance of asset1.
     *  @param amountWithdraw     Asset1 amount to transfer to the LP.
     *  @param amount2Withdraw    Asset2 amount to transfer, or absent for
     *                                single-asset withdrawals.
     *  @param lpTokensAMMBalance Current total LP token supply.
     *  @param lpTokensWithdraw   LP tokens to burn.
     *  @param tfee               Pool trading fee in basis points.
     *  @param freezeHandling     Whether to zero out frozen trustline transfers.
     *  @param authHandling       Whether to zero out unauthorized trustline transfers.
     *  @param withdrawAll        `Yes` if the LP is redeeming their entire position.
     *  @param priorBalance       Caller's XRP balance before fee deduction
     *                                (used for reserve checks during asset transfer).
     *  @param journal            For error logging.
     *  @return A tuple of `{TER, newLPTokenBalance, asset1Withdrawn, asset2Withdrawn}`.
     */
    static std::tuple<TER, STAmount, STAmount, std::optional<STAmount>>
    withdraw(
        Sandbox& view,
        SLE const& ammSle,
        AccountID const& ammAccount,
        AccountID const& account,
        STAmount const& amountBalance,
        STAmount const& amountWithdraw,
        std::optional<STAmount> const& amount2Withdraw,
        STAmount const& lpTokensAMMBalance,
        STAmount const& lpTokensWithdraw,
        std::uint16_t tfee,
        FreezeHandling freezeHandling,
        AuthHandling authHandling,
        WithdrawAll withdrawAll,
        XRPAmount const& priorBalance,
        beast::Journal const& journal);

    /** Delete the AMM instance account and its `ltAMM` entry if no LP tokens remain.
     *
     *  After burning LP tokens, if `lpTokenBalance` has reached zero the AMM
     *  pseudo-account and its associated ledger entry are orphaned objects.
     *  This method detects that condition and calls `deleteAMMAccount`, which
     *  removes those objects before the sandbox is committed.
     *
     *  If `deleteAMMAccount` returns `tecINCOMPLETE` (partial deletion — more
     *  objects remain to clean up), the LP token balance is still updated on
     *  `ammSle` so the next transaction can continue the teardown. If the
     *  balance is non-zero, only the balance field is updated and no deletion
     *  is attempted.
     *
     *  Called by `applyGuts`, `AMMDelete`, and `AMMClawback` after every
     *  successful token burn.
     *
     *  @param sb             Sandbox staging all mutations; caller commits on success.
     *  @param ammSle         AMM ledger entry (mutable); its `sfLPTokenBalance`
     *                            is updated unless full deletion occurs.
     *  @param lpTokenBalance Post-burn LP token balance.
     *  @param asset1         First pool asset (identifies the AMM for deletion).
     *  @param asset2         Second pool asset (identifies the AMM for deletion).
     *  @param journal        For error logging.
     *  @return `{TER, true}` on success or partial cleanup (`tecINCOMPLETE`);
     *      `{tec*, false}` if an unexpected deletion error occurs.
     */
    static std::pair<TER, bool>
    deleteAMMAccountIfEmpty(
        Sandbox& sb,
        std::shared_ptr<SLE> const ammSle,
        STAmount const& lpTokenBalance,
        Asset const& asset1,
        Asset const& asset2,
        beast::Journal const& journal);

private:
    /** Dispatch to the mode-specific withdrawal helper and commit AMM state.
     *
     *  Reads the current pool balances, selects the appropriate private method
     *  based on `tfWithdrawSubTx` flag bits, executes it, then calls
     *  `deleteAMMAccountIfEmpty` to clean up orphaned objects if the pool is
     *  fully drained. On `tesSUCCESS`, updates `sfLPTokenBalance` on the AMM
     *  ledger entry.
     *
     *  @param view  The sandbox to mutate; committed by the caller only on success.
     *  @return      `{TER, true}` on success; `{tec*, false}` on failure.
     */
    std::pair<TER, bool>
    applyGuts(Sandbox& view);

    /** Instance-method wrapper around the public static `withdraw` overload.
     *
     *  Forwards to the static overload with `FreezeHandling::ZeroIfFrozen`,
     *  `AuthHandling::ZeroIfUnauthorized`, `isWithdrawAll(ctx_.tx)`,
     *  `preFeeBalance_`, and `j_` sourced from the instance's `ApplyContext`.
     *
     *  @param view               Sandbox to mutate.
     *  @param ammSle             AMM ledger entry.
     *  @param ammAccount         AMM pseudo-account ID.
     *  @param amountBalance      Current pool balance of asset1.
     *  @param amountWithdraw     Asset1 amount to transfer to the LP.
     *  @param amount2Withdraw    Asset2 amount to transfer, or absent for
     *                                single-asset withdrawals.
     *  @param lpTokensAMMBalance Current total LP token supply.
     *  @param lpTokensWithdraw   LP tokens to burn.
     *  @param tfee               Pool trading fee in basis points.
     *  @return `{tesSUCCESS, newLPTokenBalance}` or `{tec*, STAmount{}}`.
     */
    std::pair<TER, STAmount>
    withdraw(
        Sandbox& view,
        SLE const& ammSle,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& amountWithdraw,
        std::optional<STAmount> const& amount2Withdraw,
        STAmount const& lpTokensAMMBalance,
        STAmount const& lpTokensWithdraw,
        std::uint16_t tfee);

    /** Instance-method wrapper around the public static `equalWithdrawTokens` overload.
     *
     *  Forwards to the static overload with `FreezeHandling::ZeroIfFrozen`,
     *  `AuthHandling::ZeroIfUnauthorized`, `isWithdrawAll(ctx_.tx)`,
     *  `preFeeBalance_`, and `ctx_.journal` sourced from the instance's
     *  `ApplyContext`. No trading fee is charged.
     *
     *  @param view             Sandbox to mutate.
     *  @param ammSle           AMM ledger entry.
     *  @param ammAccount       AMM pseudo-account ID.
     *  @param amountBalance    Current pool balance of asset1.
     *  @param amount2Balance   Current pool balance of asset2.
     *  @param lptAMMBalance    Current total LP token supply.
     *  @param lpTokens         LP token balance held by the caller's account.
     *  @param lpTokensWithdraw LP tokens to burn.
     *  @param tfee             Pool trading fee in basis points (passed to
     *                              amount-adjustment helpers; not charged).
     *  @return `{tesSUCCESS, newLPTokenBalance}` or `{tec*, STAmount{}}`.
     */
    std::pair<TER, STAmount>
    equalWithdrawTokens(
        Sandbox& view,
        SLE const& ammSle,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& amount2Balance,
        STAmount const& lptAMMBalance,
        STAmount const& lpTokens,
        STAmount const& lpTokensWithdraw,
        std::uint16_t tfee);

    /** Proportional dual-asset withdrawal with per-asset maximum caps (`tfTwoAsset`).
     *
     *  Computes the largest proportional withdrawal (as a fraction of
     *  `lptAMMBalance`) that fits within both `amount` (asset1 cap) and `amount2`
     *  (asset2 cap). Because the withdrawal is proportional, the pool's price
     *  ratio is preserved and no trading fee is charged. The actual amounts
     *  withdrawn may be less than the stated maximums.
     *
     *  Fails with `tecAMM_FAILED` if neither asset cap yields a feasible solution.
     *
     *  @param view           Sandbox to mutate.
     *  @param ammSle         AMM ledger entry.
     *  @param ammAccount     AMM pseudo-account ID.
     *  @param amountBalance  Current pool balance of asset1.
     *  @param amount2Balance Current pool balance of asset2.
     *  @param lptAMMBalance  Current total LP token supply.
     *  @param amount         Maximum asset1 the LP is willing to withdraw.
     *  @param amount2        Maximum asset2 the LP is willing to withdraw.
     *  @param tfee           Pool trading fee in basis points (passed through
     *                            for amount-adjustment rounding; not charged).
     *  @return `{tesSUCCESS, newLPTokenBalance}` or `{tec*, STAmount{}}`.
     */
    std::pair<TER, STAmount>
    equalWithdrawLimit(
        Sandbox& view,
        SLE const& ammSle,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& amount2Balance,
        STAmount const& lptAMMBalance,
        STAmount const& amount,
        STAmount const& amount2,
        std::uint16_t tfee);

    /** Single-asset withdrawal by specified amount (`tfSingleAsset`).
     *
     *  Computes the LP tokens to burn via `lpTokensIn(amountBalance, amount,
     *  lptAMMBalance, tfee)`. Single-asset withdrawal is mathematically
     *  equivalent to a swap followed by a proportional withdrawal, so the pool's
     *  trading fee applies. Fails with `tecAMM_INVALID_TOKENS` (under
     *  `fixAMMv1_3`) if the computed token amount rounds to zero.
     *
     *  @param view          Sandbox to mutate.
     *  @param ammSle        AMM ledger entry.
     *  @param ammAccount    AMM pseudo-account ID.
     *  @param amountBalance Current pool balance of asset1.
     *  @param lptAMMBalance Current total LP token supply.
     *  @param amount        Asset1 amount to withdraw.
     *  @param tfee          Pool trading fee in basis points.
     *  @return `{tesSUCCESS, newLPTokenBalance}` or `{tec*, STAmount{}}`.
     */
    std::pair<TER, STAmount>
    singleWithdraw(
        Sandbox& view,
        SLE const& ammSle,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& lptAMMBalance,
        STAmount const& amount,
        std::uint16_t tfee);

    /** Single-asset withdrawal for a specified LP token amount (`tfOneAssetLPToken`).
     *
     *  Adjusts `lpTokensWithdraw` via `adjustLPTokensIn`, then solves for the
     *  asset1 output implied by burning exactly that many tokens, subject to a
     *  minimum of `amount` (the caller's floor). The trading fee is charged.
     *  Fails with `tecAMM_INVALID_TOKENS` (under `fixAMMv1_3`) if the adjusted
     *  token amount rounds to zero.
     *
     *  @param view             Sandbox to mutate.
     *  @param ammSle           AMM ledger entry.
     *  @param ammAccount       AMM pseudo-account ID.
     *  @param amountBalance    Current pool balance of asset1.
     *  @param lptAMMBalance    Current total LP token supply.
     *  @param amount           Minimum asset1 the LP is willing to receive.
     *  @param lpTokensWithdraw LP tokens to burn.
     *  @param tfee             Pool trading fee in basis points.
     *  @return `{tesSUCCESS, newLPTokenBalance}` or `{tec*, STAmount{}}`.
     */
    std::pair<TER, STAmount>
    singleWithdrawTokens(
        Sandbox& view,
        SLE const& ammSle,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& lptAMMBalance,
        STAmount const& amount,
        STAmount const& lpTokensWithdraw,
        std::uint16_t tfee);

    /** Single-asset withdrawal with an effective-price ceiling (`tfLimitLPToken`).
     *
     *  Enforces two simultaneous constraints: a minimum output amount (`amount`,
     *  or zero meaning unconstrained) and a maximum effective price per LP token
     *  burned (`ePrice`). This mode is the closest analogue to a limit order at
     *  exit time — the transaction fails with `tecAMM_FAILED` if the current
     *  pool price exceeds `ePrice`. The trading fee is charged.
     *
     *  Two-pass algorithm: if a non-zero `amount` satisfies the price constraint
     *  it is used directly; otherwise the exact asset1 and token amounts that
     *  satisfy `EP = ePrice` are derived analytically.
     *
     *  @param view          Sandbox to mutate.
     *  @param ammSle        AMM ledger entry.
     *  @param ammAccount    AMM pseudo-account ID.
     *  @param amountBalance Current pool balance of asset1.
     *  @param lptAMMBalance Current total LP token supply.
     *  @param amount        Minimum asset1 to withdraw (zero = unconstrained).
     *  @param ePrice        Maximum effective price (asset1 per LP token burned).
     *  @param tfee          Pool trading fee in basis points.
     *  @return `{tesSUCCESS, newLPTokenBalance}` or `{tec*, STAmount{}}`.
     */
    std::pair<TER, STAmount>
    singleWithdrawEPrice(
        Sandbox& view,
        SLE const& ammSle,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& lptAMMBalance,
        STAmount const& amount,
        STAmount const& ePrice,
        std::uint16_t tfee);

    /** Decode the `tfWithdrawAll` / `tfOneAssetWithdrawAll` flag bits.
     *
     *  @param tx  The transaction to inspect.
     *  @return `WithdrawAll::Yes` if either full-redemption flag is set;
     *      `WithdrawAll::No` otherwise.
     */
    static WithdrawAll
    isWithdrawAll(STTx const& tx);
};

}  // namespace xrpl
