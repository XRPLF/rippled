#pragma once

#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for `TrustSet` transactions.
 *
 *  A `TrustSet` transaction creates, modifies, or implicitly deletes a
 *  *trust line* — the bilateral `RippleState` ledger entry that permits two
 *  accounts to carry a non-XRP IOU balance in a specific currency. Without a
 *  trust line, no IOU balance can exist between the two parties.
 *
 *  The class participates in the standard three-phase pipeline declared by
 *  `Transactor`:
 *  - `getFlagsMask` / `preflight` — stateless validation (no ledger access).
 *  - `checkPermission`            — delegate-authority check against a
 *                                   read-only view.
 *  - `preclaim`                   — read-only ledger checks.
 *  - `doApply`                    — mutable ledger writes.
 *
 *  Static methods use compile-time name hiding rather than virtual dispatch;
 *  `Transactor::invokePreflight<TrustSet>` resolves them at compile time.
 *  `ConsequencesFactoryType::Normal` is used — the transaction does not block
 *  subsequent transactions from the same account and needs no custom
 *  consequence computation.
 */
class TrustSet : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit TrustSet(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Return the set of flags valid for a `TrustSet` transaction.
     *
     *  Returns `tfTrustSetMask`, which is the union of `tfSetfAuth`,
     *  `tfSetNoRipple`, `tfClearNoRipple`, `tfSetFreeze`, `tfClearFreeze`,
     *  `tfSetDeepFreeze`, and `tfClearDeepFreeze`. `preflight1` uses this mask
     *  to reject transactions that set any flag outside the union before any
     *  ledger state is touched. The deep-freeze bits are included in the mask
     *  unconditionally; `preflight` enforces the `featureDeepFreeze` amendment
     *  guard separately at runtime.
     *
     *  @param ctx  Preflight context (unused; present for interface uniformity).
     *  @return     Bitmask of all flags permitted on a `TrustSet` transaction.
     */
    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    /** Validate a `TrustSet` transaction without accessing ledger state.
     *
     *  Checks performed (all return `tem` codes, so no fee is charged on
     *  failure):
     *  - If `featureDeepFreeze` is not enabled, `tfSetDeepFreeze` and
     *    `tfClearDeepFreeze` are rejected with `temINVALID_FLAG`.
     *  - `sfLimitAmount` must be a legal non-native amount (`temBAD_AMOUNT`).
     *  - `sfLimitAmount` must not be denominated in XRP (`temBAD_LIMIT`).
     *  - The currency must not be the XRP pseudo-currency sentinel
     *    (`temBAD_CURRENCY`).
     *  - The limit must be non-negative (`temBAD_LIMIT`).
     *  - The issuer field must be a real, non-zero account ID
     *    (`temDST_NEEDED`).
     *
     *  @param ctx  Immutable preflight context carrying the transaction and
     *              active ledger rules.
     *  @return     `tesSUCCESS` on success, or a `tem` error code.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Validate delegate authority for a `TrustSet` transaction.
     *
     *  Called only when `sfDelegate` is present on the transaction. Locates
     *  the `Delegate` ledger object for `(sfAccount, sfDelegate)` and first
     *  checks full transaction-type permission via `checkTxPermission`. If
     *  that passes, the entire transaction is allowed. Otherwise, the method
     *  falls through to the following **granular permission** checks:
     *
     *  - Any flag in `tfTrustSetPermissionMask` (everything except
     *    `tfSetfAuth`, `tfSetFreeze`, and `tfClearFreeze`) → denied.
     *  - `sfQualityIn` or `sfQualityOut` present → denied (delegates cannot
     *    adjust quality settings under granular grants).
     *  - Trust line does not yet exist → denied (granular grants cannot
     *    create new trust lines).
     *  - `tfSetfAuth` set without `TrustlineAuthorize` permission → denied.
     *  - `tfSetFreeze` set without `TrustlineFreeze` permission → denied.
     *  - `tfClearFreeze` set without `TrustlineUnfreeze` permission → denied.
     *  - `sfLimitAmount` differs from the stored limit → denied (granular
     *    delegates cannot modify credit limits).
     *
     *  @param view  Read-only ledger view for looking up delegate objects and
     *               the existing trust line.
     *  @param tx    The transaction being evaluated.
     *  @return      `tesSUCCESS` if the delegate is authorised, or
     *               `terNO_DELEGATE_PERMISSION` otherwise.
     */
    static NotTEC
    checkPermission(ReadView const& view, STTx const& tx);

    /** Read-only ledger checks before state mutation.
     *
     *  Checks (in order):
     *  1. **Auth flag guard**: `tfSetfAuth` is only valid when the issuing
     *     account has `lsfRequireAuth` set (`tefNO_AUTH_REQUIRED`).
     *  2. **Self-trust**: sender and destination (issuer) must differ
     *     (`temDST_IS_SRC`).
     *  3. **Destination existence**: when AMM or `featureSingleAssetVault` is
     *     enabled the destination account must exist (`tecNO_DST`).
     *  4. **`lsfDisallowIncomingTrustline`**: if the destination has opted out
     *     of incoming trust lines, the transaction is blocked (`tecNO_PERMISSION`)
     *     unless `fixDisallowIncomingV1` is enabled *and* a trust line between
     *     the parties already exists (modification of an existing relationship
     *     is always permitted regardless of the opt-out).
     *  5. **Pseudo-account restrictions**:
     *     - AMM pseudo-accounts: new trust lines are only permitted when the
     *       currency matches the pool's LP token and the pool has non-zero
     *       liquidity; existing lines may always be modified.
     *     - Vault and loan-broker pseudo-accounts: modification of an existing
     *       line is permitted; creation of a new line is not.
     *     - Any other pseudo-account type → `tecPSEUDO_ACCOUNT`.
     *  6. **Deep-freeze invariants** (when `featureDeepFreeze` is active):
     *     - `lsfNoFreeze` on the sender's account blocks any freeze action.
     *     - Setting and clearing any freeze flag in the same transaction is
     *       rejected.
     *     - A post-transaction simulation via `computeFreezeFlags` ensures
     *       deep-freeze cannot be set without a co-occurring normal freeze.
     *
     *  @param ctx  Read-only preclaim context with ledger view and transaction.
     *  @return     `tesSUCCESS`, or an appropriate `tec`/`tef`/`tem` error.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Apply the `TrustSet` transaction to the mutable ledger view.
     *
     *  **Reserve policy**: accounts with fewer than two owned objects are
     *  exempt from the incremental reserve requirement when creating a new
     *  trust line. This allows gateways to fund new users with exactly the
     *  base reserve — without having to pad for the trust-line reserve — so
     *  the user cannot pocket the surplus.
     *
     *  **Existing trust line** (`RippleState` SLE found):
     *  - Account IDs are compared numerically to assign the "low" and "high"
     *    side; all field accesses (`sfLowLimit`/`sfHighLimit`,
     *    `sfLowQualityIn`/`sfHighQualityIn`, etc.) use the appropriate side.
     *  - Quality values equal to `QUALITY_ONE` are stored as zero to maintain
     *    canonical representation.
     *  - The owner-reserve flags (`lsfLowReserve`/`lsfHighReserve`) are
     *    recomputed from scratch after every change; `adjustOwnerCount` is
     *    called with ±1 whenever either flag transitions.
     *  - If both sides of the trust line reach fully default state (zero limit,
     *    zero quality, no special flags, no outstanding balance), `trustDelete`
     *    is called to remove the SLE and decrement both owner counts (automatic
     *    garbage collection).
     *  - If a reserve increase would be required but `preFeeBalance_` falls
     *    short of the post-creation reserve threshold, the transaction fails
     *    with `tecINSUF_RESERVE_LINE`.
     *
     *  **No existing trust line**:
     *  - Submitting all-default parameters on a non-existent line returns
     *    `tecNO_LINE_REDUNDANT` immediately.
     *  - If the sender's XRP balance is below the reserve needed to hold one
     *    more object, the transaction fails with `tecNO_LINE_INSUF_RESERVE`.
     *  - Otherwise, `trustCreate` initialises a new `RippleState` SLE with
     *    the supplied limit, quality, and flag values.
     *
     *  @return  `tesSUCCESS`, `tecINSUF_RESERVE_LINE`, `tecNO_LINE_REDUNDANT`,
     *           `tecNO_LINE_INSUF_RESERVE`, `tecNO_DST`, `tecNO_PERMISSION`,
     *           or `tefINTERNAL` on unexpected ledger corruption.
     */
    TER
    doApply() override;

    /** Accumulate per-SLE state for transaction-specific invariant checks.
     *
     *  Called once per modified ledger entry before `finalizeInvariants`.
     *  The current implementation is a no-op placeholder; `TrustSet` does
     *  not yet define transaction-specific post-conditions beyond those
     *  enforced by the protocol-level invariant checkers.
     *
     *  @param isDelete  `true` if the entry was erased.
     *  @param before    Entry state before the transaction (`nullptr` if
     *                   the entry was newly created).
     *  @param after     Entry state after the transaction (not guaranteed to
     *                   be `nullptr` on deletion; use `isDelete` instead).
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Verify transaction-specific post-conditions after all entries are visited.
     *
     *  Called once after every modified ledger entry has been passed to
     *  `visitInvariantEntry`. Returns `false` to fail the transaction with
     *  `tecINVARIANT_FAILED`. The current implementation always returns
     *  `true` (placeholder for future transaction-specific invariants).
     *
     *  @param tx      The applied transaction.
     *  @param result  Tentative `TER` result prior to invariant checking.
     *  @param fee     Fee consumed by the transaction.
     *  @param view    Read-only view of the final ledger state.
     *  @param j       Journal for logging invariant failures.
     *  @return        `true` if all transaction-specific invariants pass.
     */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

}  // namespace xrpl
