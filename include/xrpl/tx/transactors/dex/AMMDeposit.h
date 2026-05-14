#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class Sandbox;

/** Transactor for `AMMDeposit` transactions (XLS-30d).
 *
 *  Allows liquidity providers to add assets to an AMM pool in exchange for
 *  LP tokens that represent their fractional share of the pool's reserves.
 *  Six mutually exclusive deposit modes are supported, selected by a single
 *  flag bit from `tfDepositSubTx`:
 *
 *  | Flag                | Method                    | Fee charged? | Description |
 *  |---------------------|---------------------------|:------------:|-------------|
 *  | `tfLPToken`         | `equalDepositTokens`      | No           | Proportional deposit targeting a specific LP token amount |
 *  | `tfTwoAsset`        | `equalDepositLimit`       | No           | Proportional deposit with per-asset maximum constraints |
 *  | `tfSingleAsset`     | `singleDeposit`           | Yes          | Single-asset deposit by amount |
 *  | `tfOneAssetLPToken` | `singleDepositTokens`     | Yes          | Single-asset deposit targeting an LP token quantity |
 *  | `tfLimitLPToken`    | `singleDepositEPrice`     | Yes          | Single-asset deposit with effective-price ceiling |
 *  | `tfTwoAssetIfEmpty` | `equalDepositInEmptyState`| N/A          | Bootstraps an AMM pool whose LP token balance is zero |
 *
 *  Proportional modes (the first two) do not charge a trading fee because they
 *  preserve the pool's price ratio. Single-asset modes are mathematically
 *  equivalent to an implicit swap followed by a proportional deposit, so the
 *  pool's trading fee applies to the swap component.
 *
 *  On success, XRP deposits adjust account `sfBalance` fields directly; IOU
 *  and MPT deposits move balances through trustlines between the depositor and
 *  the AMM account. LP tokens are issued to the depositor, creating a new
 *  trustline if none exists.
 *
 *  Unlike `AMMWithdraw`, which exposes `withdraw` and `equalWithdrawTokens` as
 *  `public static` methods for reuse by `AMMDelete`, all deposit-mode helpers
 *  here are `private` — the deposit path is only reachable through the
 *  validated transactor lifecycle.
 *
 *  @see [XLS-30d AMMDeposit](https://github.com/XRPLF/XRPL-Standards/discussions/78)
 */
class AMMDeposit : public Transactor
{
public:
    /** Standard fee/sequence consequences; no special queue-blocking behavior. */
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit AMMDeposit(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Gate on the `featureAMM` amendment and, for MPT-backed pools, on
     *  `featureMPTokensV2`.
     *
     *  Returns `false` (disabling this transaction type entirely) if `featureAMM`
     *  is not active. Additionally returns `false` if the pool asset pair or any
     *  deposit amount uses an `MPTIssue` but `featureMPTokensV2` has not yet
     *  activated, keeping MPT pool deposits gated behind the second amendment.
     *
     *  @param ctx  The preflight context providing rules and the transaction.
     *  @return `true` if the transaction type is permitted under current rules.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Return the valid transaction flag mask (`tfAMMDepositMask`).
     *
     *  The framework uses this to reject any transaction whose flags fall
     *  outside the set of bits defined for `AMMDeposit`.
     *
     *  @param ctx  Unused; present for the static dispatch interface.
     *  @return     Bitmask of all valid `AMMDeposit` flags.
     */
    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    /** Validate the transaction structure without accessing ledger state.
     *
     *  Enforces:
     *  - Exactly one flag bit from `tfDepositSubTx` is set
     *    (`std::popcount == 1`); otherwise `temMALFORMED`.
     *  - Per-mode field constraints (required and forbidden optional fields).
     *  - Asset-pair validity via `invalidAMMAssetPair`.
     *  - `sfAmount` and `sfAmount2` denominate different assets.
     *  - `sfLPTokenOut`, if present, is positive.
     *  - `sfTradingFee`, if present, does not exceed `kTRADING_FEE_THRESHOLD`.
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
     *  - For `tfTwoAssetIfEmpty`: LP token balance must be zero
     *    (`tecAMM_NOT_EMPTY` if already populated).
     *  - For all other modes: LP token balance must be positive
     *    (`tecAMM_EMPTY` if the pool is drained).
     *  - Account has sufficient funds for the requested deposit amounts.
     *    Re-checked inside `deposit()` for modes where the final amount is
     *    derived from pool math rather than specified directly.
     *  - When `featureAMMClawback` is active: neither pool asset may be
     *    individually frozen on the depositor's account (`tecFROZEN`).
     *  - MPT-specific authorization via `checkMPTTxAllowed`.
     *
     *  @param ctx  The preclaim context providing the read-only ledger view.
     *  @return `tesSUCCESS` on success, or the appropriate `tec*`/`ter*` code.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Execute the deposit against a copy-on-write sandbox and commit on success.
     *
     *  Creates a `Sandbox` over `ctx_.view()`, delegates to `applyGuts`, and
     *  calls `sb.apply(ctx_.rawView())` only if `applyGuts` returns success.
     *  Any failure leaves the consensus view unmodified.
     *
     *  @return `tesSUCCESS` or the `tec*` error from the selected deposit mode.
     */
    TER
    doApply() override;

    /** No-op placeholder for future transaction-specific invariant state.
     *
     *  `AMMDeposit` currently delegates all invariant checking to the global
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

private:
    /** Dispatch to the mode-specific deposit helper and commit AMM state.
     *
     *  Reads the current pool balances, determines the effective trading fee
     *  (pool fee for non-empty pools; `sfTradingFee` from the transaction for
     *  the empty-pool bootstrap path), then routes to the appropriate private
     *  method based on `tfDepositSubTx`. On `tesSUCCESS`, updates
     *  `sfLPTokenBalance` on the AMM ledger entry and, for the empty-pool case,
     *  calls `initializeFeeAuctionVote` to grant the bootstrapping LP their
     *  initial fee-governance position.
     *
     *  @param view  The sandbox to mutate; committed by the caller only on success.
     *  @return      `{TER, true}` on success; `{tec*, false}` on failure.
     */
    std::pair<TER, bool>
    applyGuts(Sandbox& view);

    /** Shared execution kernel: transfer assets into the AMM and issue LP tokens.
     *
     *  Adjusts the requested amounts via `adjustAmountsByLPTokens` (rounding
     *  and token-cap corrections), then verifies minimum constraints and account
     *  balance. On success, performs three `accountSend` transfers in order:
     *  depositor → AMM for asset1, depositor → AMM for asset2 (if present),
     *  and AMM → depositor for the LP tokens (creating a trustline if needed).
     *
     *  This function re-checks account balance even for modes where `preclaim`
     *  already validated it, because the actual amounts may differ from the
     *  values specified in the transaction (they are derived from pool math).
     *
     *  @param view              Sandbox to mutate.
     *  @param ammAccount        AccountID of the AMM pseudo-account.
     *  @param amountBalance     Current pool balance of asset1.
     *  @param amountDeposit     Computed asset1 amount to transfer.
     *  @param amount2Deposit    Computed asset2 amount to transfer, or absent for
     *                              single-asset modes.
     *  @param lptAMMBalance     Current total LP token supply.
     *  @param lpTokensDeposit   LP tokens to issue to the depositor.
     *  @param depositMin        Minimum acceptable asset1 deposit; `tecAMM_FAILED`
     *                              if the adjusted amount falls below this.
     *  @param deposit2Min       Minimum acceptable asset2 deposit; same semantics.
     *  @param lpTokensDepositMin Minimum acceptable LP token issuance; same semantics.
     *  @param tfee              Trading fee in basis points.
     *  @return `{tesSUCCESS, newLPTokenBalance}` on success, or
     *          `{tec*, STAmount{}}` on failure.
     */
    std::pair<TER, STAmount>
    deposit(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& amountDeposit,
        std::optional<STAmount> const& amount2Deposit,
        STAmount const& lptAMMBalance,
        STAmount const& lpTokensDeposit,
        std::optional<STAmount> const& depositMin,
        std::optional<STAmount> const& deposit2Min,
        std::optional<STAmount> const& lpTokensDepositMin,
        std::uint16_t tfee);

    /** Proportional two-asset deposit for a targeted LP token amount (`tfLPToken`).
     *
     *  Computes asset1 and asset2 deposit amounts proportional to `lpTokensDeposit`
     *  relative to the current LP supply (`lptAMMBalance`), then forwards to
     *  `deposit`. Under `fixAMMv1_3`, if token adjustment rounds to zero the
     *  transaction fails with `tecAMM_INVALID_TOKENS`. No trading fee is charged
     *  because the deposit preserves the pool's price ratio.
     *
     *  @param view             Sandbox to mutate.
     *  @param ammAccount       AMM pseudo-account ID.
     *  @param amountBalance    Current pool balance of asset1.
     *  @param amount2Balance   Current pool balance of asset2.
     *  @param lptAMMBalance    Current total LP token supply.
     *  @param lpTokensDeposit  Target LP token amount to receive.
     *  @param depositMin       Optional minimum asset1 deposit; `tecAMM_FAILED` if not met.
     *  @param deposit2Min      Optional minimum asset2 deposit; same semantics.
     *  @param tfee             Trading fee in basis points (unused for fee charging; passed
     *                              to `deposit` for amount-adjustment rounding).
     *  @return `{tesSUCCESS, newLPTokenBalance}` or `{tec*, STAmount{}}`.
     */
    std::pair<TER, STAmount>
    equalDepositTokens(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& amount2Balance,
        STAmount const& lptAMMBalance,
        STAmount const& lpTokensDeposit,
        std::optional<STAmount> const& depositMin,
        std::optional<STAmount> const& deposit2Min,
        std::uint16_t tfee);

    /** Proportional two-asset deposit bounded by per-asset maximums (`tfTwoAsset`).
     *
     *  Computes the LP token fraction implied by `amount` (asset1 max), then
     *  derives the required asset2. If asset2 exceeds `amount2`, re-derives from
     *  `amount2` and checks that asset1 stays within `amount`. If neither
     *  binding is satisfiable the transaction fails with `tecAMM_FAILED`.
     *  No trading fee is charged; the deposit preserves the pool price ratio.
     *
     *  Equations used (A = pool asset1, B = pool asset2, T = LP supply):
     *  `a = (t/T) * A`, `b = (t/T) * B`, solved for whichever asset is the
     *  binding constraint.
     *
     *  @param view               Sandbox to mutate.
     *  @param ammAccount         AMM pseudo-account ID.
     *  @param amountBalance      Current pool balance of asset1.
     *  @param amount2Balance     Current pool balance of asset2.
     *  @param lptAMMBalance      Current total LP token supply.
     *  @param amount             Maximum asset1 the depositor will provide.
     *  @param amount2            Maximum asset2 the depositor will provide.
     *  @param lpTokensDepositMin Optional minimum LP tokens to receive; `tecAMM_FAILED` if not met.
     *  @param tfee               Trading fee in basis points (passed through for rounding).
     *  @return `{tesSUCCESS, newLPTokenBalance}` or `{tec*, STAmount{}}`.
     */
    std::pair<TER, STAmount>
    equalDepositLimit(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& amount2Balance,
        STAmount const& lptAMMBalance,
        STAmount const& amount,
        STAmount const& amount2,
        std::optional<STAmount> const& lpTokensDepositMin,
        std::uint16_t tfee);

    /** Single-asset deposit by amount (`tfSingleAsset`).
     *
     *  Computes LP tokens issued via the single-deposit formula
     *  `t = T * (b/B - x) / (1 + x)` where `x = sqrt(f1² + b/(B*(1-fee))) - f1`
     *  and `f1 = (1 - 0.5*fee) / (1 - fee)`. The trading fee is charged because
     *  depositing one asset is equivalent to swapping half for the other asset,
     *  then making a proportional deposit.
     *
     *  @param view               Sandbox to mutate.
     *  @param ammAccount         AMM pseudo-account ID.
     *  @param amountBalance      Current pool balance of asset1.
     *  @param lptAMMBalance      Current total LP token supply.
     *  @param amount             Asset1 amount to deposit.
     *  @param lpTokensDepositMin Optional minimum LP tokens to receive; `tecAMM_FAILED` if not met.
     *  @param tfee               Pool trading fee in basis points.
     *  @return `{tesSUCCESS, newLPTokenBalance}` or `{tec*, STAmount{}}`.
     */
    std::pair<TER, STAmount>
    singleDeposit(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& lptAMMBalance,
        STAmount const& amount,
        std::optional<STAmount> const& lpTokensDepositMin,
        std::uint16_t tfee);

    /** Single-asset deposit targeting a specific LP token quantity (`tfOneAssetLPToken`).
     *
     *  Solves the single-deposit formula for the required asset1 input given a
     *  desired LP token output (`lpTokensDeposit`). Fails with `tecAMM_FAILED`
     *  if the computed asset1 input would exceed the caller's stated maximum
     *  (`amount`). The trading fee is charged for the same reason as
     *  `singleDeposit`.
     *
     *  @param view             Sandbox to mutate.
     *  @param ammAccount       AMM pseudo-account ID.
     *  @param amountBalance    Current pool balance of asset1.
     *  @param amount           Maximum asset1 the depositor will provide.
     *  @param lptAMMBalance    Current total LP token supply.
     *  @param lpTokensDeposit  Exact LP token amount the depositor targets.
     *  @param tfee             Pool trading fee in basis points.
     *  @return `{tesSUCCESS, newLPTokenBalance}` or `{tec*, STAmount{}}`.
     */
    std::pair<TER, STAmount>
    singleDepositTokens(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& amount,
        STAmount const& lptAMMBalance,
        STAmount const& lpTokensDeposit,
        std::uint16_t tfee);

    /** Single-asset deposit with an effective-price ceiling (`tfLimitLPToken`).
     *
     *  Accepts at most `amount` of asset1, subject to the constraint that the
     *  effective price EP = asset1_in / LP_out must not exceed `ePrice`. Two-pass
     *  algorithm: if a non-zero `amount` is given and its EP ≤ `ePrice`, that
     *  deposit is used directly. Otherwise, the exact asset1 and LP token amounts
     *  that satisfy EP = `ePrice` are derived by solving the quadratic form of
     *  the single-deposit equation (the derivation is in the `.cpp` inline
     *  comments). The trading fee is charged as for `singleDeposit`.
     *
     *  @param view             Sandbox to mutate.
     *  @param ammAccount       AMM pseudo-account ID.
     *  @param amountBalance    Current pool balance of asset1.
     *  @param amount           Optional maximum asset1 amount (zero means unconstrained).
     *  @param lptAMMBalance    Current total LP token supply.
     *  @param ePrice           Maximum acceptable effective price (asset1/LP token).
     *  @param tfee             Pool trading fee in basis points.
     *  @return `{tesSUCCESS, newLPTokenBalance}` or `{tec*, STAmount{}}`.
     */
    std::pair<TER, STAmount>
    singleDepositEPrice(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& amount,
        STAmount const& lptAMMBalance,
        STAmount const& ePrice,
        std::uint16_t tfee);

    /** Bootstrap a zero-balance AMM pool (`tfTwoAssetIfEmpty`).
     *
     *  Only valid when `lptAMMBalance == 0`. Computes the initial LP token
     *  supply as `sqrt(amount * amount2)` via `ammLPTokens`, seeds the pool with
     *  both assets, and issues the resulting tokens to the depositor. After
     *  `applyGuts` commits this, `initializeFeeAuctionVote` grants the
     *  bootstrapping LP their initial auction-slot and voting position.
     *
     *  @param view        Sandbox to mutate.
     *  @param ammAccount  AMM pseudo-account ID.
     *  @param amount      Asset1 amount to seed the pool.
     *  @param amount2     Asset2 amount to seed the pool.
     *  @param lptIssue    Asset descriptor for the LP token to be issued.
     *  @param tfee        Initial trading fee (from `sfTradingFee` in the transaction,
     *                         or 0 if absent); stored by `initializeFeeAuctionVote`.
     *  @return `{tesSUCCESS, newLPTokenBalance}` or `{tec*, STAmount{}}`.
     */
    std::pair<TER, STAmount>
    equalDepositInEmptyState(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& amount,
        STAmount const& amount2,
        Asset const& lptIssue,
        std::uint16_t tfee);
};

}  // namespace xrpl
