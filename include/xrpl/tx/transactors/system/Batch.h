#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the `ttBATCH` transaction type.
 *
 *  Bundles two to eight inner XRPL transactions into a single outer
 *  transaction with one of four configurable execution policies
 *  (`tfAllOrNothing`, `tfOnlyOne`, `tfUntilFailure`, `tfIndependent`).
 *  This lets operations that belong together — such as a DEX offer paired
 *  with a trust-line establishment — be submitted composably without the
 *  race conditions and ordering problems of independent submissions.
 *
 *  The outer transaction handles fee deduction and sequence/ticket
 *  consumption via the base-class `apply()` machinery. Inner transaction
 *  execution is delegated to `applyBatchTransactions()` in `apply.cpp`,
 *  which is called by `applyTransaction()` only after the outer apply
 *  succeeds. Each inner transaction runs in its own `perTxBatchView`
 *  sandbox layered over a shared `wholeBatchView`; `wholeBatchView` is
 *  merged into the authoritative ledger view only if at least one inner
 *  transaction was applied.
 *
 *  @note Vault (`ttVAULT_*`), Loan Broker (`ttLOAN_BROKER_*`), and Loan
 *      (`ttLOAN_*`) transaction types are forbidden as inner transactions
 *      and are enumerated in `kDISABLED_TX_TYPES`.
 */
class Batch : public Transactor
{
public:
    /** Uses standard consequence analysis; does not unconditionally block
     *  the transaction queue the way a `Blocker`-factory transactor does.
     */
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit Batch(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Compute the total base fee for a Batch transaction.
     *
     *  The fee is additive:
     *  - One base fee for the outer Batch transaction itself.
     *  - One base fee per inner transaction (preventing Batch from being a
     *    cost-avoidance mechanism relative to submitting transactions
     *    individually).
     *  - One base fee per entry in `sfBatchSigners` (mirrors the multi-sign
     *    fee structure used elsewhere in the protocol).
     *
     *  Overflow is checked at every accumulation step. If any addition would
     *  overflow, `INITIAL_XRP` is returned as a sentinel — consistent with
     *  the defensive pattern used across the XRPL fee pipeline.
     *
     *  @param view The current ledger view, used to obtain the base fee.
     *  @param tx   The Batch transaction being evaluated.
     *  @return The total base fee in drops, or `INITIAL_XRP` on overflow.
     */
    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    /** Return the set of flags valid on the outer Batch transaction.
     *
     *  Returns `tfBatchMask`, which admits exactly the four mutually
     *  exclusive execution-policy flags and rejects `tfInnerBatchTxn` on
     *  the outer transaction (only inner transactions carry that flag).
     *
     *  @param ctx Preflight context (unused beyond satisfying the static
     *      interface; the mask is unconditional).
     *  @return The bitmask of valid outer-Batch flags.
     */
    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    /** Validate the structural integrity of a Batch transaction.
     *
     *  Runs before signature verification and has no ledger access.
     *  Checks (in order):
     *  1. Exactly one execution-policy flag is set (enforced via
     *     `std::popcount`).
     *  2. `sfRawTransactions` contains at least 2 and at most
     *     `maxBatchTxCount` (8) entries.
     *  3. Each inner transaction: is not a `ttBATCH` (no nesting), is not
     *     a disabled type (`kDISABLED_TX_TYPES`), carries `tfInnerBatchTxn`,
     *     has an empty `sfSigningPubKey` and no `sfTxnSignature`/`sfSigners`,
     *     has a zero XRP fee, and passes its own `xrpl::preflight()` call
     *     with the `tapBATCH` flag and the outer batch's ID as
     *     `parentBatchId`.
     *  4. Each inner transaction has either a non-zero `sfSequence` or an
     *     `sfTicketSequence`, but not both.
     *  5. For `tfAllOrNothing` and `tfUntilFailure` modes, duplicate
     *     sequence or ticket values across inner transactions from the same
     *     account are rejected (these modes commit or abort as a unit, so
     *     consuming the same account slot twice would be incoherent).
     *
     *  @param ctx The preflight context for the outer Batch transaction.
     *  @return `tesSUCCESS` if the batch is structurally valid; a `tem*`
     *      code otherwise (e.g., `temINVALID_INNER_BATCH` for disabled
     *      types, `temINVALID_FLAG` for multiple policy flags).
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Validate batch-signer authorization after the outer signature is
     *  verified.
     *
     *  Called by the framework only after the outer transaction's own
     *  cryptographic signature has been accepted, granting confidence that
     *  the submitter is who they claim to be. This hook:
     *  1. Builds `requiredSigners` — every inner-transaction account that
     *     differs from the outer account, plus any `sfCounterparty` fields
     *     that differ from the outer account.
     *  2. Validates `sfBatchSigners` against that set with a bidirectional
     *     pass: extraneous signers (not in `requiredSigners`) and missing
     *     signers (in `requiredSigners` but absent from `sfBatchSigners`)
     *     both fail, as do duplicates. The outer account may not appear in
     *     `sfBatchSigners`.
     *  3. Calls `ctx.tx.checkBatchSign()` to verify the cryptographic
     *     signatures of all batch signers.
     *
     *  @param ctx The preflight context, guaranteed to be post-outer-sig.
     *  @return `tesSUCCESS` if all batch signers are valid and sufficient;
     *      a `tem*` code otherwise.
     */
    static NotTEC
    preflightSigValidated(PreflightContext const& ctx);

    /** Verify signatures at the preclaim stage, where ledger state is
     *  available.
     *
     *  Runs two checks in sequence:
     *  1. `Transactor::checkSign` — validates the outer account's
     *     signature (regular key, master key, or signer list).
     *  2. `Transactor::checkBatchSign` — re-validates batch-signer
     *     credentials against on-ledger `RegularKey`/`SignerList` objects,
     *     which are only accessible in preclaim.
     *
     *  The split from `preflightSigValidated` is intentional: preflight
     *  can only check cryptographic correctness; preclaim can additionally
     *  confirm on-ledger authorization state.
     *
     *  @param ctx The preclaim context, providing a read-only ledger view.
     *  @return `tesSUCCESS` if both checks pass; a `tef*` code otherwise.
     */
    static NotTEC
    checkSign(PreclaimContext const& ctx);

    /** Apply the outer Batch transaction.
     *
     *  Returns `tesSUCCESS` immediately. The outer Batch itself writes
     *  nothing to the ledger beyond the fee deduction and sequence/ticket
     *  consumption handled by the base-class `apply()` method. Inner
     *  transaction execution is performed by `applyBatchTransactions()` in
     *  `apply.cpp`, invoked by `applyTransaction()` after this method
     *  returns.
     *
     *  @return Always `tesSUCCESS`.
     */
    TER
    doApply() override;

    /** Invariant visitor for Batch — currently a no-op.
     *
     *  Inner-transaction invariant checking is handled by the individual
     *  inner transactors in their own apply contexts. The outer Batch
     *  transaction does not directly mutate ledger objects beyond the
     *  base-class fee/sequence handling.
     *
     *  @param isDelete `true` if the entry is being deleted.
     *  @param before   The SLE state before the transaction; may be null
     *      for newly created entries.
     *  @param after    The SLE state after the transaction; may be null for
     *      deleted entries.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Finalize invariant checks for Batch — currently a no-op.
     *
     *  Returns `true` unconditionally. Inner-transaction invariants are
     *  finalized within each inner transactor's own apply context.
     *
     *  @param tx     The Batch transaction.
     *  @param result The TER code produced by `doApply`.
     *  @param fee    The fee deducted from the submitting account.
     *  @param view   A read-only view of the ledger after application.
     *  @param j      Journal for diagnostic logging.
     *  @return Always `true` (no outer-Batch invariant violations possible).
     */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;

    /** Transaction types that may not appear as inner transactions in a
     *  Batch.
     *
     *  Vault (`ttVAULT_*`), Loan Broker (`ttLOAN_BROKER_*`), and Loan
     *  (`ttLOAN_*`) families are blocked because their multi-ledger-object
     *  state machines are difficult to reason about under batch atomicity
     *  semantics. `preflight` rejects any inner transaction whose type
     *  appears here with `temINVALID_INNER_BATCH`.
     */
    static constexpr auto kDISABLED_TX_TYPES = std::to_array<TxType>({
        ttVAULT_CREATE,
        ttVAULT_SET,
        ttVAULT_DELETE,
        ttVAULT_DEPOSIT,
        ttVAULT_WITHDRAW,
        ttVAULT_CLAWBACK,
        ttLOAN_BROKER_SET,
        ttLOAN_BROKER_DELETE,
        ttLOAN_BROKER_COVER_DEPOSIT,
        ttLOAN_BROKER_COVER_WITHDRAW,
        ttLOAN_BROKER_COVER_CLAWBACK,
        ttLOAN_SET,
        ttLOAN_DELETE,
        ttLOAN_MANAGE,
        ttLOAN_PAY,
    });
};

}  // namespace xrpl
