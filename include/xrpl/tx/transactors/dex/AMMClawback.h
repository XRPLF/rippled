/** @file
 *  Declares the `AMMClawback` transactor (XLS-73), which allows a regulated
 *  token issuer to recover assets held inside an AMM liquidity position.
 */

#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {
class Sandbox;

/** Transactor for the `AMMClawback` transaction type (XLS-73).
 *
 *  Allows a regulated-token issuer to reclaim assets from a specific holder's
 *  AMM liquidity position. This prevents holders from circumventing issuer
 *  clawback authority by depositing regulated tokens into an AMM pool.
 *
 *  The clawback is implemented as a forced proportional withdrawal of the
 *  holder's LP tokens via `AMMWithdraw`'s helpers, with the proceeds sent to
 *  the issuer. Two modes are supported: full clawback (all LP tokens) when no
 *  `sfAmount` is specified, and partial clawback (up to a requested asset1
 *  quantity) when `sfAmount` is present. Both modes pass a zero trading fee
 *  because the withdrawal is a proportional equal-ratio removal — charging a
 *  fee would penalise the issuer for exercising a regulatory right.
 *
 *  All ledger mutations are accumulated in a `Sandbox` inside `doApply` and
 *  committed only on success, consistent with the standard transactor pattern.
 *
 *  @see AMMWithdraw::equalWithdrawTokens
 *  @see AMMWithdraw::withdraw
 */
class AMMClawback : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit AMMClawback(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Gate the transaction on the `featureAMMClawback` amendment.
     *
     *  Also requires `featureMPTokensV2` when any of `sfAsset`, `sfAsset2`,
     *  or `sfAmount` refers to an MPT issuance, ensuring MPT support is gated
     *  behind its own separate amendment rollout.
     *
     *  @param ctx Preflight context providing access to the transaction and
     *      active amendment rules.
     *  @return `true` if the required amendments are enabled; `false` otherwise
     *      (transaction is rejected before preflight runs).
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Return the set of valid transaction flags for `AMMClawback`.
     *
     *  @param ctx Preflight context (unused; present for interface uniformity).
     *  @return Bitmask of flags accepted by this transaction type
     *      (`tfAMMClawbackMask`).
     */
    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    /** Stateless validation of the `AMMClawback` transaction fields.
     *
     *  Enforces the following invariants without accessing ledger state:
     *  - `sfAsset` must not be XRP (only issued assets support clawback).
     *  - `sfAccount` (issuer) must equal `sfAsset`'s issuer field.
     *  - `sfHolder` must differ from `sfAccount`.
     *  - If `tfClawTwoAssets` is set, both `sfAsset` and `sfAsset2` must share
     *      the same issuer.
     *  - If `sfAmount` is present, its asset subfield must match `sfAsset` and
     *      the quantity must be positive.
     *
     *  @param ctx Preflight context providing the transaction and active rules.
     *  @return `tesSUCCESS` on valid input; `temMALFORMED`, `temINVALID_FLAG`,
     *      or `temBAD_AMOUNT` on constraint violations.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Ledger-state permission checks for `AMMClawback`.
     *
     *  Verifies that both the issuer account and the target AMM pool exist, and
     *  that the issuer has clawback authority over the relevant asset(s):
     *  - For IOU assets: the issuer's `AccountRoot` must have
     *      `lsfAllowTrustLineClawback` set and must not have `lsfNoFreeze`
     *      set (permanent freeze-waiver revokes clawback ability).
     *  - For MPT assets: the specific MPT issuance must carry
     *      `lsfMPTCanClawback` and must be owned by the transaction's account.
     *
     *  When `tfClawTwoAssets` is set, permission is checked for both assets.
     *
     *  @param ctx Preclaim context providing read-only ledger access.
     *  @return `tesSUCCESS` if all checks pass; `terNO_ACCOUNT` if issuer or
     *      holder account is absent; `terNO_AMM` if the pool does not exist;
     *      `tecNO_PERMISSION` if clawback authority is not established.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Execute the `AMMClawback` transaction against the ledger.
     *
     *  Wraps `applyGuts` in a `Sandbox`. All ledger mutations are accumulated
     *  in the sandbox and flushed to the real `ApplyView` only if `applyGuts`
     *  returns `tesSUCCESS`. Failed executions leave no trace beyond fee
     *  deduction.
     *
     *  @return `tesSUCCESS` on success; a `tec*` code on failure.
     */
    TER
    doApply() override;

    /** Per-SLE invariant visitor — no transaction-specific invariants yet.
     *
     *  @param isDelete `true` if the entry is being deleted.
     *  @param before SLE state before the transaction.
     *  @param after SLE state after the transaction.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Post-transaction invariant finalizer — no transaction-specific checks yet.
     *
     *  @param tx The applied transaction.
     *  @param result TER result of the transaction.
     *  @param fee XRP fee charged.
     *  @param view Read-only view of the ledger after the transaction.
     *  @param j Journal for diagnostic logging.
     *  @return Always `true`; reserved for future invariant checks.
     */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;

private:
    /** Core clawback logic executed inside a `Sandbox`.
     *
     *  Reads pool balances, selects full or partial withdrawal based on
     *  whether `sfAmount` is present, drives `AMMWithdraw` helpers to burn
     *  LP tokens, and calls `directSendNoFee` to transfer recovered assets
     *  from the holder to the issuer. Both code paths pass
     *  `FreezeHandling::IgnoreFreeze` and `AuthHandling::IgnoreAuth` so the
     *  issuer is not blocked by trustline restrictions they may have set.
     *
     *  @param view Sandbox accumulating all mutations for this transaction.
     *  @return `tesSUCCESS` on success; a `tec*` error code on failure.
     */
    TER
    applyGuts(Sandbox& view);

    /** Proportional two-asset withdrawal bounded by a maximum asset1 amount.
     *
     *  Computes the pool fraction corresponding to `amount` of asset1, derives
     *  the matching asset2 and LP-token quantities, then calls
     *  `AMMWithdraw::withdraw`. No trading fee is charged because both assets
     *  are withdrawn in the current pool ratio.
     *
     *  If the derived LP-token quantity exceeds `holdLPtokens` (the holder
     *  does not have enough LP tokens to satisfy the requested asset1 amount),
     *  the method degrades gracefully to a full clawback of all LP tokens the
     *  holder owns via `AMMWithdraw::equalWithdrawTokens` — the issuer cannot
     *  over-claw.
     *
     *  When `fixAMMClawbackRounding` is active, `getRoundedLPTokens` and
     *  `getRoundedAsset` are applied before calling `withdraw` to snap values
     *  to representable amounts and prevent dust or rounding-driven invariant
     *  violations.
     *
     *  @param view Sandbox accumulating ledger mutations.
     *  @param ammSle The AMM ledger entry.
     *  @param holder Account whose LP tokens are being redeemed.
     *  @param ammAccount AMM pseudo-account holding the pooled assets.
     *  @param amountBalance Current pool balance of asset1.
     *  @param amount2Balance Current pool balance of asset2.
     *  @param lptAMMBalance Current total LP-token supply for the pool.
     *  @param holdLPtokens LP tokens currently held by `holder`.
     *  @param amount Maximum asset1 quantity the issuer wishes to recover.
     *  @return Tuple of `(TER, newLPTokenBalance, asset1Withdrawn,
     *      asset2Withdrawn)`. `asset2Withdrawn` is always populated for
     *      equal-ratio withdrawals; it is `std::nullopt` only on internal
     *      error paths.
     */
    std::tuple<TER, STAmount, STAmount, std::optional<STAmount>>
    equalWithdrawMatchingOneAmount(
        Sandbox& view,
        SLE const& ammSle,
        AccountID const& holder,
        AccountID const& ammAccount,
        STAmount const& amountBalance,
        STAmount const& amount2Balance,
        STAmount const& lptAMMBalance,
        STAmount const& holdLPtokens,
        STAmount const& amount);
};

}  // namespace xrpl
