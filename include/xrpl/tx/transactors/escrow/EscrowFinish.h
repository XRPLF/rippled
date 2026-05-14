#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the EscrowFinish transaction type.
 *
 *  `EscrowFinish` is the success path in the three-escrow family: it
 *  verifies that all release conditions are satisfied and transfers the
 *  locked funds to the intended recipient.  `EscrowCreate` locks funds;
 *  `EscrowCancel` reclaims them on the failure path; this transactor
 *  completes the happy path.
 *
 *  It is the most feature-rich of the three escrow transactors: it
 *  validates PREIMAGE-SHA-256 crypto-conditions (Interledger spec),
 *  scales its fee with fulfillment size to prevent DoS, and performs
 *  multi-asset eligibility checks in `preclaim` for both IOU and MPT
 *  escrows under `featureTokenEscrow`.
 *
 *  The `kCONSEQUENCES_FACTORY` is `Normal` because releasing funds has
 *  standard fee semantics.  Contrast with `EscrowCreate`, which uses
 *  `Custom` to account for the arbitrary amount of XRP or tokens it locks.
 *
 *  @see EscrowCreate
 *  @see EscrowCancel
 */
class EscrowFinish : public Transactor
{
public:
    /** Uses the standard fee-consequence model; no custom factory needed. */
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit EscrowFinish(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Amendment gate for `sfCredentialIDs` support.
     *
     *  Returns `false` — causing `invokePreflight` to short-circuit with
     *  `temDISABLED` — when the transaction carries `sfCredentialIDs` but
     *  the `featureCredentials` amendment is not yet active.  If the field
     *  is absent, or if the amendment is active, returns `true` and
     *  processing continues normally.
     *
     *  This hook runs before `preflight` and keeps amendment gating cleanly
     *  separated from field-level validation.
     *
     *  @param ctx  stateless preflight context carrying the raw transaction.
     *  @return     `false` if `sfCredentialIDs` is present without the
     *              `featureCredentials` amendment; `true` otherwise.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Stateless preflight validation for EscrowFinish.
     *
     *  Enforces the pairing rule for crypto-condition fields: `sfCondition`
     *  and `sfFulfillment` must either both be present or both be absent.
     *  Submitting one without the other returns `temMALFORMED`.
     *
     *  Expensive crypto-condition validation is deliberately deferred to
     *  `preflightSigValidated` so it cannot be triggered by unauthenticated
     *  input.
     *
     *  @param ctx  stateless preflight context carrying the raw transaction.
     *  @return     `tesSUCCESS` if the condition/fulfillment pairing is
     *              valid; `temMALFORMED` if exactly one of the two fields is
     *              present.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Post-signature crypto-condition validation hook.
     *
     *  Runs after `preflight2` has verified the transaction's cryptographic
     *  signature, ensuring condition validation is never performed on
     *  unauthenticated input.
     *
     *  When both `sfCondition` and `sfFulfillment` are present, checks the
     *  fulfillment against the condition via PREIMAGE-SHA-256 verification
     *  and stores the boolean outcome in the `HashRouter` using the private
     *  flag bits `SF_CF_VALID` / `SF_CF_INVALID`.  A failed check does NOT
     *  return an error here — the result is cached for `doApply` to enforce,
     *  keeping preflight non-blocking for transaction broadcasting.
     *
     *  Also validates `sfCredentialIDs` format via `credentials::checkFields`
     *  when the field is present.
     *
     *  @param ctx  stateless preflight context carrying the raw transaction.
     *  @return     `tesSUCCESS` if credential fields are well-formed (or
     *              absent); a credential field error code otherwise.
     *  @note The hash-router cache entry set here is consumed by `doApply`.
     *      If the entry has expired by the time `doApply` runs, the
     *      condition check is repeated and re-cached at that point.
     */
    static NotTEC
    preflightSigValidated(PreflightContext const& ctx);

    /** Compute the fee for an EscrowFinish transaction.
     *
     *  Adds a surcharge on top of the base fee when a fulfillment is
     *  attached, proportional to its size:
     *
     *  @code
     *  extraFee = base_fee * (32 + fulfillment_size / 16)
     *  @endcode
     *
     *  This prevents DoS via large fulfillments: bigger payloads impose
     *  more validation cost on validators, and the surcharge ensures that
     *  cost is borne by the submitter.
     *
     *  @param view  read-only ledger view supplying the current fee schedule.
     *  @param tx    the raw transaction being evaluated.
     *  @return      the base fee plus any fulfillment surcharge.
     */
    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    /** Read-only ledger checks for EscrowFinish.
     *
     *  Handles two independent concerns, each gated by its own amendment:
     *
     *  - **`featureCredentials`**: validates `sfCredentialIDs` authorization
     *    via `credentials::valid`.
     *  - **`featureTokenEscrow`**: for non-XRP escrows, dispatches to a
     *    template helper via `std::visit`:
     *    - `Issue` (IOU) — checks `requireAuth` authorization and verifies
     *      the destination is not deep-frozen by the issuer.
     *    - `MPTIssue` (MPToken) — verifies the issuance object exists,
     *      checks `requireAuth` (using `WeakAuth` semantics), and checks
     *      for MPT-level freeze.
     *
     *  These asset eligibility checks are placed in `preclaim` (rather than
     *  `doApply`) so that authorization failures do not charge a fee.
     *
     *  @param ctx  read-only preclaim context with ledger access.
     *  @return     `tesSUCCESS` on success; a credential error if
     *              `sfCredentialIDs` authorization fails; `tecNO_TARGET` if
     *              the escrow object does not exist; `tecNO_PERMISSION` or
     *              a `requireAuth` error if the destination is unauthorized;
     *              `tecFROZEN` for a deep-frozen IOU; `tecLOCKED` for a
     *              frozen MPT; `tecOBJECT_NOT_FOUND` if the MPT issuance
     *              is absent.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Apply the EscrowFinish transaction to the mutable ledger.
     *
     *  Enforces temporal and cryptographic release conditions, then performs
     *  the ledger state mutations:
     *
     *  1. **Time window**: compares the ledger's `parentCloseTime` against
     *     `sfFinishAfter` (too early → `tecNO_PERMISSION`) and
     *     `sfCancelAfter` (too late → `tecNO_PERMISSION`).
     *  2. **Condition re-check**: reads the cached `SF_CF_VALID`/`SF_CF_INVALID`
     *     hash-router flags set during `preflightSigValidated`.  If the cache
     *     has expired, the fulfillment is re-validated and re-cached.  A
     *     failed check returns `tecCRYPTOCONDITION_ERROR`.  Also enforces
     *     that the condition presented in the transaction matches the one
     *     stored in the escrow object at creation time.
     *  3. **Deposit pre-auth**: `verifyDepositPreauth` ensures the
     *     destination's allow-list is satisfied if required.
     *  4. **Directory cleanup**: removes the escrow from the originating
     *     account's owner directory, the recipient's owner directory (when
     *     `sfDestinationNode` is present), and — for non-XRP escrows — the
     *     issuer's owner directory.  Any directory removal failure returns
     *     `tefBAD_LEDGER` (indicates ledger corruption).
     *  5. **Fund transfer**: for XRP, credits the destination's `sfBalance`
     *     directly.  For non-XRP, routes through `escrowUnlockApplyHelper`,
     *     applying the transfer rate locked at escrow creation (using
     *     `parityRate` when none was recorded).
     *  6. **Owner count**: decrements the originating account's owner count
     *     by one via `adjustOwnerCount`.
     *  7. **Erase**: removes the escrow SLE from the ledger.
     *
     *  @return `tesSUCCESS` on success; `tecNO_TARGET` (or `tecINTERNAL`
     *          with `featureTokenEscrow` active) if the escrow is missing at
     *          apply time; `tecNO_PERMISSION` if a time bound is violated;
     *          `tecCRYPTOCONDITION_ERROR` if the fulfillment is invalid or
     *          the condition does not match; `tecNO_DST` if the destination
     *          account no longer exists; a deposit pre-auth error if the
     *          destination's allow-list is not satisfied; `tefBAD_LEDGER` if
     *          a directory removal fails.
     *  @note Escrow payments cannot be used to fund new accounts — the
     *      destination account must already exist at apply time.
     */
    TER
    doApply() override;

    /** Per-entry invariant hook — currently a no-op.
     *
     *  Reserved for future transaction-specific post-condition checks.
     *  The base-class invariant framework still runs protocol-level checkers
     *  independently of this method.
     *
     *  @param isDelete  true if the entry was erased by this transaction.
     *  @param before    entry state before the transaction (nullptr if new).
     *  @param after     entry state after the transaction.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Post-apply invariant hook — currently a no-op.
     *
     *  Reserved for future post-condition checks after all modified entries
     *  have been visited.  Currently returns `true` unconditionally.
     *
     *  @param tx     the transaction being applied.
     *  @param result the tentative TER result from `doApply`.
     *  @param fee    fee consumed by the transaction.
     *  @param view   read-only view of the ledger after the transaction.
     *  @param j      journal for logging invariant failures.
     *  @return       always `true`.
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
