/** @file
 *  Public interface for the XRPL transaction application pipeline.
 *
 *  Defines the structured three-stage sequence `preflight → preclaim → doApply`
 *  that every transaction traverses before being committed to an open ledger.
 *  The stages are exposed as separate functions so the Transaction Queue (TxQ)
 *  can cache `PreflightResult` across ledger boundaries and defer
 *  `preclaim`/`doApply` until an application slot is available.
 *
 *  @see apply.h for a single-call wrapper that composes all three stages.
 */
#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyViewImpl.h>

namespace xrpl {

class ServiceRegistry;
class STTx;
class TxQ;

/** Outcome of a complete transaction application attempt.
 *
 *  Returned by `doApply()` and by the single-call `apply()` wrapper.
 *  `applied` is true only when the transaction was committed to the ledger
 *  (either `tesSUCCESS` or a fee-claiming `tec*` without `tapRETRY`).
 *  `metadata` is populated when `applied` is true and the caller requested
 *  metadata generation.
 */
struct ApplyResult
{
    TER ter;
    bool applied;
    std::optional<TxMeta> metadata;

    ApplyResult(TER t, bool a, std::optional<TxMeta> m = std::nullopt)
        : ter(t), applied(a), metadata(std::move(m))
    {
    }
};

/** Return true when a `tec` result will definitely charge a fee.
 *
 *  A `tec` transaction normally charges its fee and is included in the ledger
 *  with all `doApply` mutations rolled back.  However, when `tapRETRY` is set
 *  the TxQ is treating the transaction as a soft failure that may succeed after
 *  other queued transactions settle — in that mode the fee must not yet be
 *  charged because the transaction is not actually being applied.
 *
 *  This predicate is the authoritative definition of "this transaction will
 *  cost the submitter money right now."  It is evaluated in the
 *  `PreclaimResult` constructor to populate `likelyToClaimFee`, and again
 *  in `Transactor::operator()()` to decide whether to call `reset()` (which
 *  discards all `doApply` mutations and re-applies the fee only).
 *
 *  @param ter  The `TER` result from preclaim or doApply.
 *  @param flags  The `ApplyFlags` governing this application attempt.
 *  @return True when `ter` is a `tec*` code and `tapRETRY` is absent,
 *      meaning the fee will be charged and the transaction included in the
 *      ledger.
 */
inline bool
isTecClaimHardFail(TER ter, ApplyFlags flags)
{
    return isTecClaim(ter) && ((flags & TapRetry) == 0u);
}

/** Worst-case XRP cost and queue-ordering impact of a transaction.
 *
 *  The Transaction Queue (TxQ) evaluates this object — produced during
 *  `preflight` — to decide whether queued follow-on transactions from the
 *  same account remain viable *before* the transaction is applied to a
 *  ledger.  It answers: how much XRP can this transaction consume in the
 *  worst case, and does it invalidate subsequent queue entries?
 *
 *  The five constructors are deliberately distinct variants rather than a
 *  single struct with defaulted fields, so that each variant enforces its own
 *  invariant: the `NotTEC` constructor zeros everything to ensure no cost
 *  estimate leaks from a rejected transaction; the `Category` constructor
 *  flips `isBlocker_`; and the `XRPAmount`/`uint32_t` constructors extend the
 *  normal case for transactions with non-standard spending or sequence use.
 *
 *  @note `potentialSpend_` does not include the fee; it represents additional
 *      XRP that may leave the account (e.g., the `sfSendMax` of a Payment).
 */
class TxConsequences
{
public:
    /** Categorises the impact a transaction has on subsequent queue entries. */
    enum class Category {
        /** Standard transaction: moves currency, creates offers, etc.
         *  Subsequent transactions from the same account may still queue. */
        Normal = 0,
        /** Key-management operation whose execution may invalidate the
         *  signatures on transactions already in the queue.
         *  Examples: `SetRegularKey` (key removal), `AccountDelete`,
         *  `SignerListSet`.  TxQ enforces that a blocker cannot coexist
         *  with other queued transactions from the same account. */
        Blocker
    };

private:
    bool isBlocker_;
    XRPAmount fee_;
    /// Additional XRP the transaction may spend beyond the fee.
    XRPAmount potentialSpend_;
    SeqProxy seqProx_;
    std::uint32_t sequencesConsumed_;

public:
    /** Construct a zeroed-out consequences for a failed preflight.
     *
     *  All cost fields are set to zero so that no XRP estimate leaks from
     *  a rejected transaction into queue accounting.
     *
     *  @param pfResult  A non-success `NotTEC` code from preflight.
     *      Asserts if `tesSUCCESS` is passed.
     */
    explicit TxConsequences(NotTEC pfResult);

    /** Construct consequences for a transaction with no special queue impact.
     *
     *  Fee and sequence are read from the transaction.  `potentialSpend_` is
     *  zero (fee only) and `sequencesConsumed_` is 1 for a sequence-based
     *  transaction or 0 for a ticket.
     *
     *  @param tx  The transaction to summarise.
     */
    explicit TxConsequences(STTx const& tx);

    /** Construct consequences for a blocker transaction.
     *
     *  Sets `isBlocker_` when `category == Category::Blocker`, preventing
     *  the TxQ from accepting additional sequence-based transactions from the
     *  same account while this transaction is queued.
     *
     *  @param tx  The transaction to summarise.
     *  @param category  `Category::Blocker` to mark as a blocker;
     *      `Category::Normal` behaves identically to the single-argument
     *      constructor.
     */
    TxConsequences(STTx const& tx, Category category);

    /** Construct consequences for a transaction that may spend XRP beyond its fee.
     *
     *  Used by `ConsequencesFactoryType::Custom` transactors such as
     *  `Payment` (via `sfSendMax`) and `OfferCreate` (via XRP `TakerGets`).
     *
     *  @param tx  The transaction to summarise.
     *  @param potentialSpend  The maximum additional XRP (above the fee)
     *      the transaction might consume.
     */
    TxConsequences(STTx const& tx, XRPAmount potentialSpend);

    /** Construct consequences for a transaction that burns multiple sequences.
     *
     *  Used by `TicketCreate`, which reserves a range of future sequence slots
     *  in one transaction.  `followingSeq()` accounts for the full range.
     *
     *  @param tx  The transaction to summarise.
     *  @param sequencesConsumed  The total number of sequence slots consumed,
     *      including the transaction's own sequence number.
     */
    TxConsequences(STTx const& tx, std::uint32_t sequencesConsumed);

    TxConsequences(TxConsequences const&) = default;
    TxConsequences&
    operator=(TxConsequences const&) = default;
    TxConsequences(TxConsequences&&) = default;
    TxConsequences&
    operator=(TxConsequences&&) = default;

    /** Transaction fee in drops. */
    [[nodiscard]] XRPAmount
    fee() const
    {
        return fee_;
    }

    /** Maximum XRP spend beyond the fee, in drops.
     *
     *  Zero for most transactions.  Non-zero for `Payment` and `OfferCreate`
     *  when the transaction carries an XRP spending cap (`sfSendMax` /
     *  XRP `TakerGets`).
     */
    [[nodiscard]] XRPAmount const&
    potentialSpend() const
    {
        return potentialSpend_;
    }

    /** Sequence or ticket proxy identifying this transaction's queue slot. */
    [[nodiscard]] SeqProxy
    seqProxy() const
    {
        return seqProx_;
    }

    /** Number of sequence slots consumed by this transaction.
     *
     *  Normally 1 for sequence-based transactions and 0 for ticket-based
     *  ones.  `TicketCreate` returns the number of tickets it creates.
     */
    [[nodiscard]] std::uint32_t
    sequencesConsumed() const
    {
        return sequencesConsumed_;
    }

    /** Return true if this transaction may invalidate subsequent queue entries.
     *
     *  Blockers are key-management operations (`SetRegularKey`, `AccountDelete`,
     *  `SignerListSet`) whose execution can change the account's signing
     *  authority, rendering the cached signatures of queued followers invalid.
     */
    [[nodiscard]] bool
    isBlocker() const
    {
        return isBlocker_;
    }

    /** Return the first `SeqProxy` not consumed by this transaction.
     *
     *  The TxQ uses this to find gaps in the queued sequence range for an
     *  account.  For sequence-based transactions the result is `seqProxy + 1`;
     *  for tickets it is unchanged (tickets do not occupy sequence order); for
     *  `TicketCreate` it advances by `sequencesConsumed`.
     *
     *  @return The `SeqProxy` that should immediately follow this transaction.
     */
    [[nodiscard]] SeqProxy
    followingSeq() const
    {
        SeqProxy following = seqProx_;
        following.advanceBy(sequencesConsumed());
        return following;
    }
};

/** Immutable token produced by the `preflight` stage of the pipeline.
 *
 *  Captures every input and output of ledger-agnostic transaction validation
 *  in a single, copy-constructible object.  All fields are `const` to prevent
 *  callers from fabricating a result without going through `preflight`.
 *
 *  The TxQ caches this object (`MaybeTx::pfResult`) and passes it to
 *  `preclaim` when an application slot opens.  Because ledger rules can change
 *  between the two calls, `preclaim` compares `result.rules` against the
 *  current view and automatically re-runs `preflight` when they differ.
 *
 *  @note Copy-assignment is deleted; copy-construction is allowed so the TxQ
 *      can store results in `std::optional`.
 *  @see preflight, preclaim, doApply, apply
 */
struct PreflightResult
{
public:
    /** The transaction that was checked. */
    STTx const& tx;
    /** Batch group identifier, present when this is an inner batch transaction. */
    std::optional<uint256 const> const parentBatchId;
    /** Amendment rules in effect at the time `preflight` ran.
     *  `preclaim` compares this against the current ledger's rules to detect
     *  stale results that must be re-validated. */
    Rules const rules;
    /** Worst-case XRP cost and queue-ordering impact, valid even when
     *  `ter != tesSUCCESS`. */
    TxConsequences const consequences;
    /** Processing flags supplied to `preflight`. */
    ApplyFlags const flags;
    /** Journal for diagnostics. */
    beast::Journal const j;

    /** Validation result.  `NotTEC` — only `tem*`, `tel*`, or `tesSUCCESS`;
     *  `tec*` codes never appear at the preflight stage. */
    NotTEC const ter;

    /** Construct from a preflight context and its computed result.
     *
     *  @tparam Context  A preflight context type exposing `tx`, `parentBatchId`,
     *      `rules`, `flags`, and `j` members.
     *  @param ctx  The context object used during the preflight call.
     *  @param result  A pair of `(NotTEC, TxConsequences)` returned by the
     *      transactor-specific preflight implementation.
     */
    template <class Context>
    PreflightResult(Context const& ctx, std::pair<NotTEC, TxConsequences> const& result)
        : tx(ctx.tx)
        , parentBatchId(ctx.parentBatchId)
        , rules(ctx.rules)
        , consequences(result.second)
        , flags(ctx.flags)
        , j(ctx.j)
        , ter(result.first)
    {
    }

    PreflightResult(PreflightResult const&) = default;
    /** Deleted to prevent mutation of a cached preflight result. */
    PreflightResult&
    operator=(PreflightResult const&) = delete;
};

/** Immutable token produced by the `preclaim` stage of the pipeline.
 *
 *  Captures every input and output of ledger-dependent transaction validation.
 *  All fields are `const` to prevent callers from fabricating a result without
 *  going through `preclaim`.  The object is not cached by the TxQ; a fresh
 *  `PreclaimResult` is produced each time a transaction is about to be applied.
 *
 *  The `likelyToClaimFee` flag is the primary gate for `doApply`: if false,
 *  the transaction will not be applied and no fee is charged.
 *
 *  @note Copy-assignment is deleted; copy-construction is allowed so callers
 *      can store the result in a local before passing it to `doApply`.
 *  @see preflight, preclaim, doApply, apply
 */
struct PreclaimResult
{
public:
    /** The ledger view against which the transaction was checked.
     *  `doApply` verifies that its view sequence matches this one; a mismatch
     *  (ledger advanced between preclaim and apply) returns `tefEXCEPTION`. */
    ReadView const& view;
    /** The transaction that was checked. */
    STTx const& tx;
    /** Batch group identifier, present when this is an inner batch transaction. */
    std::optional<uint256 const> const parentBatchId;
    /** Processing flags supplied to `preclaim`. */
    ApplyFlags const flags;
    /** Journal for diagnostics. */
    beast::Journal const j;

    /** Validation result.  Full `TER` range: `tes*`, `tec*`, `ter*`,
     *  `tef*`, or `tem*`. */
    TER const ter;

    /** True when the transaction will charge a fee and should be applied.
     *
     *  Computed as `isTesSuccess(ter) || isTecClaimHardFail(ter, flags)`.
     *  When false, `doApply` returns immediately without mutating the ledger. */
    bool const likelyToClaimFee{};

    /** Construct from a preclaim context and its computed `TER`.
     *
     *  @tparam Context  A preclaim context type exposing `view`, `tx`,
     *      `parentBatchId`, `flags`, and `j` members.
     *  @param ctx  The context object used during the preclaim call.
     *  @param ter  The `TER` produced by the transactor-specific preclaim
     *      implementation.
     */
    template <class Context>
    PreclaimResult(Context const& ctx, TER ter)
        : view(ctx.view)
        , tx(ctx.tx)
        , parentBatchId(ctx.parentBatchId)
        , flags(ctx.flags)
        , j(ctx.j)
        , ter(ter)
        , likelyToClaimFee(isTesSuccess(ter) || isTecClaimHardFail(ter, flags))
    {
    }

    PreclaimResult(PreclaimResult const&) = default;
    /** Deleted to prevent mutation of a preclaim result. */
    PreclaimResult&
    operator=(PreclaimResult const&) = delete;
};

/** Perform ledger-agnostic validation of a transaction (stage 1 of 3).
 *
 *  Validates the transaction against all constraints that can be checked
 *  without ledger state: field presence and format, fee field sanity, signing
 *  key structure, flag validity, and any static rules imposed by the active
 *  amendments.  This is the cheapest stage and can be parallelised across
 *  transactions.
 *
 *  The resulting `PreflightResult` — including its `TxConsequences` — may be
 *  cached by the TxQ across ledger boundaries.  If the ledger's amendment
 *  rules change before `preclaim` runs, `preclaim` will re-execute `preflight`
 *  automatically with the updated rules before proceeding.
 *
 *  @param registry  The service registry providing transactor implementations.
 *  @param rules  Amendment rules in effect at the time of the check.
 *  @param tx  The transaction to validate.
 *  @param flags  `ApplyFlags` describing processing options (e.g. `tapRETRY`,
 *      `tapDRY_RUN`).
 *  @param j  Journal for diagnostics.
 *  @return A `PreflightResult` whose `ter` is `tesSUCCESS` if the transaction
 *      is well-formed, or a `tem*`/`tel*` code if it is not.  The
 *      `consequences` field is always populated regardless of `ter`.
 *
 *  @see PreflightResult, preclaim, doApply, apply
 */
/** @{ */
PreflightResult
preflight(
    ServiceRegistry& registry,
    Rules const& rules,
    STTx const& tx,
    ApplyFlags flags,
    beast::Journal j);

/** Perform ledger-agnostic validation of an inner batch transaction (stage 1 of 3).
 *
 *  Identical to the standard overload but associates the result with a
 *  `parentBatchId`, which is stored in `PreflightResult::parentBatchId` and
 *  threaded through to `preclaim` and `doApply`.  Used when an inner
 *  transaction belonging to a `Batch` group is validated independently.
 *
 *  @param registry  The service registry providing transactor implementations.
 *  @param rules  Amendment rules in effect at the time of the check.
 *  @param parentBatchId  The transaction ID of the enclosing `Batch` transaction.
 *  @param tx  The inner transaction to validate.
 *  @param flags  `ApplyFlags` describing processing options.
 *  @param j  Journal for diagnostics.
 *  @return A `PreflightResult` for the inner transaction, carrying the batch
 *      association.
 *
 *  @see PreflightResult, preclaim, doApply, apply
 */
PreflightResult
preflight(
    ServiceRegistry& registry,
    Rules const& rules,
    uint256 const& parentBatchId,
    STTx const& tx,
    ApplyFlags flags,
    beast::Journal j);
/** @} */

/** Perform ledger-dependent validation of a transaction (stage 2 of 3).
 *
 *  Checks all constraints that require read-only access to the current ledger
 *  state: account existence, sequence/ticket validity, fee sufficiency, and
 *  cryptographic signature verification.  If `preflightResult.ter` is not
 *  `tesSUCCESS` this function is a no-op (the pipeline short-circuits).
 *
 *  **Rules-change handling**: if the amendment rules embedded in
 *  `preflightResult` differ from those in `view` (because the ledger advanced
 *  since `preflight` ran), `preclaim` automatically re-executes `preflight`
 *  with the updated rules before proceeding.  Callers do not need to detect or
 *  handle this case.
 *
 *  **Security invariant**: every check up to and including signature
 *  verification must return a `NotTEC` code (never `tec*`).  A `tec` before
 *  the signature check would charge a fee without authentication.
 *
 *  A `tesSUCCESS` or fee-claiming `tec*` result (without `tapRETRY`) sets
 *  `PreclaimResult::likelyToClaimFee`, indicating the transaction is safe to
 *  relay to peers even before it is applied.
 *
 *  @pre `preflightResult` was produced by a successful call to `preflight`.
 *  @param preflightResult  The cached result of a prior `preflight` call.
 *  @param registry  The service registry providing transactor implementations.
 *  @param view  The open ledger the transaction will be applied to.
 *  @return A `PreclaimResult` with the full `TER` and a
 *      `likelyToClaimFee` boolean that gates `doApply`.
 *
 *  @see PreclaimResult, preflight, doApply, apply
 */
PreclaimResult
preclaim(PreflightResult const& preflightResult, ServiceRegistry& registry, OpenView const& view);

/** Return the minimum fee floor for this specific transaction type.
 *
 *  Dispatches through the `with_txn_type` X-macro to the transaction-type's
 *  static `calculateBaseFee` override, so transactors that impose non-standard
 *  fees (e.g., multi-signers add one base fee per signer) return the correct
 *  floor.  The TxQ calls this to compute each transaction's fee level —
 *  the ratio of its actual fee to the floor — for prioritisation.
 *
 *  No validation of the transaction is performed.
 *
 *  @param view  The current open ledger (provides the network base fee).
 *  @param tx  The transaction whose fee floor is required.
 *  @return The minimum acceptable fee in drops.
 *  @note Callers are responsible for handling any exceptions; in practice
 *      none should be thrown and an unhandled exception should terminate.
 */
XRPAmount
calculateBaseFee(ReadView const& view, STTx const& tx);

/** Return the fee floor for a generic "reference" transaction.
 *
 *  Unlike `calculateBaseFee`, this function bypasses transactor-specific
 *  dispatch and calls `Transactor::calculateBaseFee` directly, returning what
 *  a plain transaction would pay.  The TxQ uses this as the denominator when
 *  computing fee levels, ensuring a non-zero reference even when a particular
 *  transaction's own base fee is zero.
 *
 *  @param view  The current open ledger.
 *  @param tx  The transaction (used only for multisigner count, not tx type).
 *  @return The reference base fee in drops.
 */
XRPAmount
calculateDefaultBaseFee(ReadView const& view, STTx const& tx);

/** Apply a pre-validated transaction to an open ledger (stage 3 of 3).
 *
 *  Only runs if `preclaimResult.likelyToClaimFee` is true; otherwise returns
 *  the preclaim `TER` immediately with `applied = false`.
 *
 *  Constructs an `ApplyContext` over `view`, instantiates the concrete
 *  transactor, and invokes `Transactor::operator()()`.  All ledger mutations
 *  are staged in the context and are not committed to `view` until
 *  `ctx_.apply(result)` is called inside the transactor — meaning an early
 *  return, exception, or `tec*` result leaves the view in its original state
 *  (except for the fee and sequence deduction on `tec*` hard-fail paths).
 *
 *  As a defensive check, `doApply` compares the view's ledger sequence against
 *  the one seen during `preclaim`; if they differ (the ledger advanced in the
 *  interim), it returns `tefEXCEPTION` without mutating anything.
 *
 *  @pre `preclaimResult` was produced by a successful call to `preclaim` on
 *      the same transaction and the same open ledger.
 *  @param preclaimResult  The result of a prior `preclaim` call.
 *  @param registry  The service registry providing transactor implementations.
 *  @param view  The mutable open ledger to apply the transaction to.
 *  @return An `ApplyResult` with the final `TER`, an `applied` flag indicating
 *      whether the transaction was committed, and optional `TxMeta`.
 *
 *  @see preflight, preclaim, apply
 */
ApplyResult
doApply(PreclaimResult const& preclaimResult, ServiceRegistry& registry, OpenView& view);

}  // namespace xrpl
