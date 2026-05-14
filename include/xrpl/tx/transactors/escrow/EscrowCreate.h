#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the EscrowCreate transaction type.
 *
 *  Locks XRP or tokens (IOU/MPT) into an escrow ledger object that can
 *  later be released by `EscrowFinish` or reclaimed by `EscrowCancel`.
 *  The held amount is unavailable to either party until one of those
 *  outcomes occurs.
 *
 *  Unlike its siblings, this transactor uses `kCONSEQUENCES_FACTORY =
 *  Custom` and supplies `makeTxConsequences()`.  The custom factory is
 *  required because the XRP cost of an `EscrowCreate` scales with
 *  `sfAmount`: XRP escrows lock the principal (increasing the effective
 *  spend), whereas token escrows carry zero additional XRP impact.
 *
 *  @see EscrowFinish
 *  @see EscrowCancel
 */
class EscrowCreate : public Transactor
{
public:
    /** Uses a custom consequence model to capture the locked XRP principal. */
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Custom;

    explicit EscrowCreate(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Compute the fee consequences of an EscrowCreate transaction.
     *
     *  For XRP escrows the consequences include the locked principal amount
     *  so the transaction queue can correctly reason about account spend.
     *  For token escrows the XRP consequence is `beast::zero` because no
     *  XRP leaves the account.
     *
     *  @param ctx  stateless preflight context carrying the raw transaction.
     *  @return     `TxConsequences` with the appropriate potential XRP spend.
     */
    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    /** Stateless preflight validation for EscrowCreate.
     *
     *  Validates the `sfAmount` field by asset type:
     *  - **XRP**: must be strictly positive.
     *  - **IOU** (`Issue`): requires `featureTokenEscrow`; rejects native
     *    amounts, non-positive values, and the bad-currency sentinel.
     *  - **MPT** (`MPTIssue`): additionally requires `featureMPTokensV1`.
     *
     *  Timing rules enforced here (all field-level, no ledger access):
     *  - At least one of `sfCancelAfter` or `sfFinishAfter` must be present.
     *  - When both are provided, `sfCancelAfter` must be strictly after
     *    `sfFinishAfter`.
     *  - When `sfFinishAfter` is absent, `sfCondition` must be present —
     *    an escrow with no finish time and no condition could be completed
     *    immediately with no unlock mechanism.
     *  - If `sfCondition` is present its wire encoding must deserialize
     *    without error; a parse failure returns `temMALFORMED`.
     *
     *  @param ctx  stateless preflight context carrying the raw transaction.
     *  @return     `tesSUCCESS` on success; `temBAD_AMOUNT` for invalid or
     *              unsupported amounts; `temBAD_EXPIRATION` if timing
     *              constraints are violated; `temMALFORMED` if the condition
     *              field is unparseable; `temDISABLED` if a required amendment
     *              is not active.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Read-only ledger checks for EscrowCreate.
     *
     *  Confirms the destination account exists (`tecNO_DST` otherwise) and
     *  is not a pseudo-account (`tecNO_PERMISSION` otherwise).  The
     *  pseudo-account check is unconditional rather than amendment-gated
     *  because the ledger writes that create pseudo-account discriminator
     *  fields are themselves amendment-gated, keeping behaviour consistent
     *  with whichever amendments are active.
     *
     *  For token escrows, dispatches to a template helper via `std::visit`:
     *  - **IOU** (`escrowCreatePreclaimHelper<Issue>`): verifies
     *    `lsfAllowTrustLineLocking` on the issuer, trust-line existence for
     *    both sender and destination, freeze status, `requireAuth`
     *    authorization for both parties, sufficient spendable balance, and
     *    an IOU-arithmetic precision guard (`canAdd`).
     *  - **MPT** (`escrowCreatePreclaimHelper<MPTIssue>`): verifies the
     *    MPT issuance exists, that `lsfMPTCanEscrow` is set, that the sender
     *    holds an `MPToken`, freeze/lock status for both parties, transfer
     *    eligibility (`canTransfer`), and sufficient spendable balance.
     *
     *  @param ctx  read-only preclaim context with ledger access.
     *  @return     `tesSUCCESS` on success; `tecNO_DST` if the destination
     *              does not exist; `tecNO_PERMISSION` for pseudo-account
     *              destination or other authorization failures;
     *              `tecNO_LINE`/`tecOBJECT_NOT_FOUND` if a required trust
     *              line or MPT object is absent; `tecFROZEN`/`tecLOCKED` for
     *              frozen or lock-restricted assets; `tecINSUFFICIENT_FUNDS`
     *              if the sender lacks sufficient balance; `tecPRECISION_LOSS`
     *              for IOU arithmetic edge cases.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Apply the EscrowCreate transaction to the mutable ledger.
     *
     *  Re-validates that neither `sfCancelAfter` nor `sfFinishAfter` (when
     *  present) has already elapsed relative to the ledger's
     *  `parentCloseTime`.  This time-lapse check is intentionally repeated
     *  here: time advances between preflight and apply, and creating an
     *  already-expired escrow must be prevented at this stage.
     *
     *  Then checks that the sender's XRP balance covers the incremented
     *  owner-count reserve plus, for XRP escrows, the locked principal.
     *  Verifies that the destination's `lsfRequireDestTag` flag is satisfied
     *  when set.
     *
     *  Constructs and inserts the escrow SLE.  When `fixIncludeKeyletFields`
     *  is active, copies `sfSequence` into the SLE for off-ledger keylet
     *  derivation.  For token escrows, snapshots the issuer's transfer rate
     *  at creation time into `sfTransferRate`, freezing the fee that will
     *  apply when `EscrowFinish` later executes — the issuer cannot change
     *  the rate in the interim.
     *
     *  After insertion, registers the escrow in owner directories:
     *  - Always added to the sender's `ownerDir`.
     *  - Added to the destination's `ownerDir` unless this is a self-escrow.
     *  - For IOU escrows only, added to the issuer's `ownerDir` so the issuer
     *    can enumerate all locked balances.  MPT escrows skip this step
     *    because the MPT issuance object tracks its locked balance directly.
     *
     *  Debits the sender's balance:
     *  - XRP: deducted directly from `sfBalance`.
     *  - IOU: transferred fee-free to the issuer via `directSendNoFee`.
     *  - MPT: atomically moves tokens to the locked field via `lockEscrowMPT`.
     *
     *  @return `tesSUCCESS` on successful creation; `tecNO_PERMISSION` if a
     *          time bound has already elapsed; `tecINSUFFICIENT_RESERVE` if
     *          the sender cannot meet the new reserve requirement;
     *          `tecUNFUNDED` if the XRP balance cannot cover both reserve and
     *          principal; `tecNO_DST` if the destination is absent at apply
     *          time; `tecDST_TAG_NEEDED` if a required destination tag is
     *          missing; `tecDIR_FULL` if an owner directory has no room.
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
