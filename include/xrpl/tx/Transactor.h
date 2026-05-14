/**
 * @file Transactor.h
 *
 * Base class and context structures for the XRPL transaction-processing
 * pipeline.
 *
 * Every transaction type (Payment, OfferCreate, AMM, NFT, etc.) inherits
 * from `Transactor` and participates in a strict three-phase pipeline:
 *
 *  - **preflight** — stateless, no ledger access; validates format, flags, and
 *    signature syntax via `invokePreflight<T>()`.
 *  - **preclaim**  — read-only `ReadView`; checks sequence, fee balance, and
 *    signature validity against ledger state.
 *  - **doApply**   — mutable `ApplyView`; applies state changes; only reached
 *    when preclaim returns `tesSUCCESS`.
 *
 * Compile-time polymorphism is achieved through name hiding, not virtual
 * dispatch: derived classes define static methods (`preflight`,
 * `checkExtraFeatures`, `getFlagsMask`, `preflightSigValidated`) that are
 * resolved by the `invokePreflight<T>` template at compile time.
 *
 * @see PreflightContext, PreclaimContext, ApplyContext
 */

#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/WrappedSink.h>
#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/applySteps.h>

#include <utility>

namespace xrpl {

/** Immutable context passed to all preflight checks.
 *
 *  Carries everything a stateless preflight validation step needs: the raw
 *  transaction, the active ledger rules, apply flags, and — for batch inner
 *  transactions — the hash of the enclosing batch.  No ledger view is
 *  included because preflight must not access ledger state.
 *
 *  Two constructors enforce the batch/non-batch invariant at construction
 *  time: the batch constructor asserts `TapBatch` is set and records the
 *  `parentBatchId`; the non-batch constructor asserts `TapBatch` is clear
 *  and leaves `parentBatchId` empty.
 */
struct PreflightContext
{
public:
    /** Service registry providing network ID, hash router, and load fees. */
    std::reference_wrapper<ServiceRegistry> registry;
    /** The transaction being validated. */
    STTx const& tx;
    /** Active ledger rules (amendments) at the time of validation. */
    Rules const rules;
    /** Apply flags controlling validation behavior (e.g., `TapDryRun`, `TapBatch`). */
    ApplyFlags flags;
    /** Hash of the enclosing batch transaction, present only for batch inner transactions. */
    std::optional<uint256 const> parentBatchId;
    /** Journal for diagnostic logging. */
    beast::Journal const j;

    /** Construct a context for a batch inner transaction.
     *
     *  @param registry     Service registry.
     *  @param tx           The inner transaction.
     *  @param parentBatchId Hash of the outer batch transaction.
     *  @param rules        Active ledger rules.
     *  @param flags        Apply flags; `TapBatch` must be set.
     *  @param j            Journal for logging.
     */
    PreflightContext(
        ServiceRegistry& registry,
        STTx const& tx,
        uint256 parentBatchId,
        Rules rules,
        ApplyFlags flags,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : registry(registry)
        , tx(tx)
        , rules(std::move(rules))
        , flags(flags)
        , parentBatchId(parentBatchId)
        , j(j)
    {
        XRPL_ASSERT((flags & TapBatch) == TapBatch, "Batch apply flag should be set");
    }

    /** Construct a context for an ordinary (non-batch) transaction.
     *
     *  @param registry Service registry.
     *  @param tx       The transaction.
     *  @param rules    Active ledger rules.
     *  @param flags    Apply flags; `TapBatch` must NOT be set.
     *  @param j        Journal for logging.
     */
    PreflightContext(
        ServiceRegistry& registry,
        STTx const& tx,
        Rules rules,
        ApplyFlags flags,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : registry(registry), tx(tx), rules(std::move(rules)), flags(flags), j(j)
    {
        XRPL_ASSERT((flags & TapBatch) == 0, "Batch apply flag should not be set");
    }

    PreflightContext&
    operator=(PreflightContext const&) = delete;
};

/** Immutable context passed to all preclaim checks.
 *
 *  Extends `PreflightContext` with a read-only `ReadView` so that preclaim
 *  can verify account existence, sequence validity, fee sufficiency, and
 *  signature correctness against the current ledger state.  The result of
 *  the earlier preflight phase is carried forward in `preflightResult` so
 *  that preclaim helpers can short-circuit when preflight already failed.
 *
 *  The same batch/non-batch constructor duality as `PreflightContext`
 *  applies: `parentBatchId` presence must match the `TapBatch` flag,
 *  enforced by assertion in the unified constructor.
 */
struct PreclaimContext
{
public:
    /** Service registry providing network ID, hash router, and load fees. */
    std::reference_wrapper<ServiceRegistry> registry;
    /** Read-only view of the ledger against which preclaim checks are evaluated. */
    ReadView const& view;
    /** The `NotTEC` code returned by the earlier preflight phase. */
    TER preflightResult;
    /** Apply flags (e.g., `TapDryRun`, `TapBatch`, `TapUnlimited`). */
    ApplyFlags flags;
    /** The transaction being evaluated. */
    STTx const& tx;
    /** Hash of the enclosing batch transaction; set iff `TapBatch` is active. */
    std::optional<uint256 const> const parentBatchId;
    /** Journal for diagnostic logging. */
    beast::Journal const j;

    /** Construct for a batch inner transaction (or ordinary tx with explicit batch ID).
     *
     *  Asserts that `parentBatchId.has_value() == ((flags & TapBatch) == TapBatch)`.
     *
     *  @param registry       Service registry.
     *  @param view           Read-only ledger view.
     *  @param preflightResult Result from the preflight phase.
     *  @param tx             The transaction.
     *  @param flags          Apply flags.
     *  @param parentBatchId  Hash of the outer batch, or `std::nullopt`.
     *  @param j              Journal for logging.
     */
    PreclaimContext(
        ServiceRegistry& registry,
        ReadView const& view,
        TER preflightResult,
        STTx const& tx,
        ApplyFlags flags,
        std::optional<uint256> parentBatchId,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : registry(registry)
        , view(view)
        , preflightResult(preflightResult)
        , flags(flags)
        , tx(tx)
        , parentBatchId(parentBatchId)
        , j(j)
    {
        XRPL_ASSERT(
            parentBatchId.has_value() == ((flags & TapBatch) == TapBatch),
            "Parent Batch ID should be set if batch apply flag is set");
    }

    /** Construct for an ordinary (non-batch) transaction.
     *
     *  @param registry       Service registry.
     *  @param view           Read-only ledger view.
     *  @param preflightResult Result from the preflight phase.
     *  @param tx             The transaction.
     *  @param flags          Apply flags; `TapBatch` must NOT be set.
     *  @param j              Journal for logging.
     */
    PreclaimContext(
        ServiceRegistry& registry,
        ReadView const& view,
        TER preflightResult,
        STTx const& tx,
        ApplyFlags flags,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : PreclaimContext(registry, view, preflightResult, tx, flags, std::nullopt, j)
    {
        XRPL_ASSERT((flags & TapBatch) == 0, "Batch apply flag should not be set");
    }

    PreclaimContext&
    operator=(PreclaimContext const&) = delete;
};

class TxConsequences;
struct PreflightResult;
// Needed for preflight specialization
class Change;

/** Base class for all XRPL transaction processors.
 *
 *  Implements the three-phase transaction pipeline: preflight (stateless
 *  validation), preclaim (read-only ledger checks), and doApply (mutable
 *  ledger application).  Every concrete transaction type (Payment,
 *  OfferCreate, AMMCreate, etc.) inherits from this class.
 *
 *  Polymorphism in the preflight phase is achieved through compile-time
 *  name hiding rather than virtual dispatch.  Derived classes define
 *  static methods — `preflight`, `preclaim`, `getFlagsMask`,
 *  `checkExtraFeatures`, `preflightSigValidated` — that are resolved by
 *  `invokePreflight<T>()` at the call site.  See the comment block on
 *  `invokePreflight` for the rules on what derived classes must and must
 *  not define.
 *
 *  The single virtual entry point for state mutation is `doApply()`.
 *  `operator()()` is the top-level dispatch called by the apply loop;
 *  it drives all three phases, handles fee claiming on failure, runs
 *  invariant checks, and manages `tapDRY_RUN` simulation semantics.
 *
 *  @note Instances are not copyable.  One transactor object is created
 *      per transaction application.
 */
class Transactor
{
protected:
    /** Apply context holding the sandboxed ledger view and transaction. */
    ApplyContext& ctx_;
    /** Wrapped journal sink that prepends the transaction ID to each log line. */
    beast::WrappedSink sink_;
    /** Journal backed by `sink_`; use this for all logging inside transactors. */
    beast::Journal const j_;

    /** The account that submitted the transaction (`sfAccount`). */
    AccountID const account_;
    /** Account balance captured immediately before fee deduction in `apply()`.
     *
     *  Reserve checks in `doApply` must use this value rather than the
     *  post-fee balance to allow accounts to dip into their reserve to pay
     *  the fee without violating the reserve requirement for new objects.
     */
    XRPAmount preFeeBalance_{};

public:
    virtual ~Transactor() = default;
    Transactor(Transactor const&) = delete;
    Transactor&
    operator=(Transactor const&) = delete;

    /** Controls how `TxConsequences` are produced for the transaction queue.
     *
     *  - `Normal`  — standard fee/sequence consequences (most transactors).
     *  - `Blocker` — signals that applying this transaction may prevent
     *                subsequent queued transactions from the same account from
     *                claiming fees (e.g., `SetRegularKey`, `AccountDelete`).
     *  - `Custom`  — the transactor implements `makeTxConsequences()` for
     *                type-specific cost modeling (e.g., `Payment`, `OfferCreate`).
     *
     *  Each derived class must declare:
     *  @code
     *  static constexpr ConsequencesFactoryType ConsequencesFactory{...};
     *  @endcode
     *  The correct factory is selected at compile time in `applySteps.cpp`
     *  via C++20 `requires` constraints.
     */
    enum class ConsequencesFactoryType { Normal, Blocker, Custom };

    /** Execute the full transaction pipeline for this transactor instance.
     *
     *  Called by the apply loop after preclaim succeeds.  Runs:
     *  1. RAII numeric-rule guards (`NumberSO`, `CurrentTransactionRulesGuard`).
     *  2. Debug-mode serialization round-trip check.
     *  3. Optional debug trap (`trapTransaction`).
     *  4. `apply()` if preclaim returned `tesSUCCESS`; otherwise returns the
     *     preclaim error directly.
     *  5. `tecOVERSIZE` roll-back: if metadata grew too large, discards all
     *     mutations, re-deducts fee only, and removes unfunded offers found
     *     during the failed apply.
     *  6. `tapFAIL_HARD`: on a `tec*` result, discards everything including
     *     the fee.
     *  7. Invariant checks via `checkInvariants`; a failing invariant triggers
     *     a second reset and fee-only commit.
     *  8. Forces `applied = false` when `tapDRY_RUN` is set.
     *
     *  @return `{result, applied, metadata}`.  `applied` is false when the
     *      transaction produces no ledger changes (dry-run, `tef*`, `tem*`,
     *      or invariant escalation to `tefINVARIANT_FAILED`).
     */
    ApplyResult
    operator()();

    /** Return the mutable apply view for this transaction. */
    ApplyView&
    view()
    {
        return ctx_.view();
    }

    /** Return the read-only apply view for this transaction. */
    [[nodiscard]] ApplyView const&
    view() const
    {
        return ctx_.view();
    }

    /** Check all invariants for the current transaction.
     *
     *  Runs transaction-specific invariants first (visitInvariantEntry +
     *  finalizeInvariants), then protocol-level invariants.  Both layers
     *  always run; the worst failure code is returned.
     *
     *  @param result  the tentative TER from transaction processing.
     *  @param fee     the fee consumed by the transaction.
     *
     *  @return the final TER after all invariant checks.
     */
    [[nodiscard]] TER
    checkInvariants(TER result, XRPAmount fee);

    // ---- Preclaim-phase static helpers (overridable via name hiding) --------
    //
    // These static functions are called from the preclaim dispatch in
    // applySteps.cpp using name hiding to accomplish compile-time
    // polymorphism.  Derived classes can shadow them to add or replace
    // validation logic.  They are NOT virtual; the compiler provides no
    // protection against incorrect overrides.

    /** Verify the transaction's sequence number or ticket against the ledger.
     *
     *  Returns `terNO_ACCOUNT` if the source account does not exist,
     *  `terPRE_SEQ` / `tefPAST_SEQ` for sequence-number mismatches, and
     *  `terPRE_TICKET` / `tefNO_TICKET` for ticket-based transactions.
     *
     *  @param view  Read-only ledger view.
     *  @param tx    The transaction.
     *  @param j     Journal for trace logging.
     *  @return `tesSUCCESS` if the sequence/ticket is consumable.
     */
    static NotTEC
    checkSeqProxy(ReadView const& view, STTx const& tx, beast::Journal j);

    /** Verify `sfAccountTxnID`, `sfLastLedgerSequence`, and duplicate detection.
     *
     *  Returns `tefWRONG_PRIOR` if `sfAccountTxnID` does not match the
     *  account's last transaction hash, `tefMAX_LEDGER` if the current ledger
     *  sequence exceeds `sfLastLedgerSequence`, and `tefALREADY` if the
     *  transaction is already in the ledger.
     *
     *  @param ctx  Preclaim context.
     *  @return `tesSUCCESS` or a `tef*` / `ter*` error.
     */
    static NotTEC
    checkPriorTxAndLastLedger(PreclaimContext const& ctx);

    /** Verify that the fee attached to the transaction is sufficient.
     *
     *  For open-ledger transactions, the fee must meet the load-scaled
     *  minimum returned by `minimumFee()`.  Also checks that the fee payer's
     *  account exists and has sufficient balance.
     *
     *  @param ctx      Preclaim context.
     *  @param baseFee  Unscaled base fee computed by `calculateBaseFee()`.
     *  @return `tesSUCCESS`, `telINSUF_FEE_P`, `tecINSUFF_FEE`,
     *          `terINSUF_FEE_B`, or `terNO_ACCOUNT`.
     */
    static TER
    checkFee(PreclaimContext const& ctx, XRPAmount baseFee);

    /** Verify the cryptographic signature for an ordinary transaction.
     *
     *  Dispatches to `checkMultiSign()` when `sfSigners` is present, or
     *  `checkSingleSign()` otherwise.  Skips the check for batch inner
     *  transactions (authorized by the outer batch) and dry-run simulations
     *  without a signing key.  Rejects pseudo-account signers when
     *  `featureLendingProtocol` is active.
     *
     *  @param ctx  Preclaim context.
     *  @return `tesSUCCESS` or a `tef*` error code.
     */
    static NotTEC
    checkSign(PreclaimContext const& ctx);

    /** Verify the `sfBatchSigners` array for an outer batch transaction.
     *
     *  Iterates the batch signers, dispatching to `checkMultiSign()` or
     *  `checkSingleSign()` as appropriate.  Allows a signer for an
     *  account that does not yet exist in the ledger, provided the signing
     *  key matches the account's master key (used for fund-on-creation inner
     *  transactions).
     *
     *  @param ctx  Preclaim context for the outer batch transaction.
     *  @return `tesSUCCESS` or a `tef*` error code.
     */
    static NotTEC
    checkBatchSign(PreclaimContext const& ctx);

    /** Compute the base transaction fee in drops, unscaled for load.
     *
     *  Base fee = ledger's configured base fee + one extra base fee per
     *  multisignature in `sfSigners`.  Does not account for server load;
     *  use `minimumFee()` for the load-adjusted value.
     *
     *  @param view  Read-only ledger view (supplies `fees().base`).
     *  @param tx    The transaction.
     *  @return Fee in drops (XRPAmount).
     */
    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    /** Compile-time preflight dispatch for transaction type `T`.
     *
     *  The canonical entry point for preflight validation.  Executes the
     *  following steps in order, returning on the first non-`tesSUCCESS`:
     *
     *  1. Amendment gate: returns `temDISABLED` if the transaction's
     *     required amendment is not active.
     *  2. `T::checkExtraFeatures(ctx)` — additional amendment gates defined
     *     by the derived class (return `temDISABLED` on failure).
     *  3. `preflight1(ctx, T::getFlagsMask(ctx))` — validates account field,
     *     fee field, signing key format, network ID, flags, and
     *     ticket/AccountTxnID exclusivity.
     *  4. `T::preflight(ctx)` — transaction-specific field validation.
     *  5. `preflight2(ctx)` — cryptographic signature check via hash-router
     *     cache.  Skipped for batch inner transactions.
     *  6. `T::preflightSigValidated(ctx)` — optional post-signature checks
     *     (e.g., expensive crypto conditions).
     *
     *  @note Do NOT define `invokePreflight` in a derived class.  Instead,
     *      define any combination of the static methods above.  Do NOT call
     *      `preflight1` or `preflight2` directly; they are called in the
     *      correct order by this template.  Do NOT gate on amendments in
     *      `preflight`; use `checkExtraFeatures` for that.  Do NOT validate
     *      flags in `preflight`; define `getFlagsMask` instead.
     *
     *  @note The explicit specialization `invokePreflight<Change>` is
     *      defined in `Change.cpp` and uses entirely different logic because
     *      `Change` is a pseudo-transaction with no real sender.
     *
     *  @tparam T  The concrete transactor type.
     *  @param ctx Preflight context.
     *  @return `tesSUCCESS` or a `tem*` / `tel*` error.
     */
    template <class T>
    static NotTEC
    invokePreflight(PreflightContext const& ctx);

    /** Base-class preclaim hook; most transactors do not need to override this.
     *
     *  The sequence/fee/sign checks are called directly by the preclaim
     *  dispatch in `applySteps.cpp` before this method.  Override only to
     *  add extra read-only ledger checks that cannot be expressed as field
     *  validation in `preflight`.
     *
     *  @param ctx  Preclaim context.
     *  @return `tesSUCCESS` (base implementation).
     */
    static TER
    preclaim(PreclaimContext const& ctx)
    {
        return tesSUCCESS;
    }

    /** Verify delegate permissions if `sfDelegate` is present.
     *
     *  If the transaction carries an `sfDelegate` field, reads the
     *  `DelegateObject` at `keylet::delegate(account, delegate)` and
     *  verifies that its permission set covers this transaction type.
     *  Returns `terNO_DELEGATE_PERMISSION` if the object is missing or the
     *  permission is not granted.
     *
     *  Called as a static method during preclaim so the ledger check
     *  happens before any mutation.
     *
     *  @param view  Read-only ledger view.
     *  @param tx    The transaction (may contain `sfDelegate`).
     *  @return `tesSUCCESS` or `terNO_DELEGATE_PERMISSION`.
     */
    static NotTEC
    checkPermission(ReadView const& view, STTx const& tx);
    // -------------------------------------------------------------------------

    /** Remove a single Ticket SLE and adjust the owner's ticket count and reserve.
     *
     *  Used by `AccountDelete` (via a static interface) and by
     *  `consumeSeqProxy` when a ticket-based transaction is applied.
     *  Removes the ticket from the owner directory, decrements `sfTicketCount`
     *  on the account root, adjusts the owner reserve count, and erases the
     *  ticket SLE.
     *
     *  @param view         Mutable ledger view.
     *  @param account      Owner of the ticket.
     *  @param ticketIndex  Ledger index of the Ticket SLE.
     *  @param j            Journal for fatal-error logging.
     *  @return `tesSUCCESS` or `tefBAD_LEDGER` if the ledger is corrupt.
     */
    static TER
    ticketDelete(
        ApplyView& view,
        AccountID const& account,
        uint256 const& ticketIndex,
        beast::Journal j);

protected:
    /** Run the sequence/fee/state-mutation steps for a validated transaction.
     *
     *  Called by `operator()()` when preclaim returned `tesSUCCESS`.
     *  Snapshots `preFeeBalance_`, advances the sequence (or consumes the
     *  ticket), deducts the fee, updates `sfAccountTxnID`, then calls
     *  `doApply()`.
     *
     *  @return The TER returned by `doApply()`, or a `tef*` code if the
     *      sequence/fee bookkeeping fails (indicates ledger corruption).
     */
    TER
    apply();

    /** Construct a transactor bound to the given apply context.
     *
     *  Initialises `account_` from `ctx.tx[sfAccount]` and sets up the
     *  transaction-ID-prefixed journal sink.
     */
    explicit Transactor(ApplyContext& ctx);

    /** Perform any pre-apply computation that should not repeat per-ledger.
     *
     *  Called at the start of `apply()` before `consumeSeqProxy` and
     *  `payFee`.  The base implementation asserts that `account_` is
     *  non-zero.  Derived classes may cache expensive lookups here.
     */
    virtual void
    preCompute();

    /** Apply the transaction's state changes to the mutable ledger view.
     *
     *  The sole virtual method in the pipeline.  Only called when all
     *  preflight and preclaim checks have passed and the fee/sequence have
     *  been consumed.
     *
     *  Implementations must return `tesSUCCESS` for a full commit.
     *  Returning a `tec*` code causes `operator()()` to roll back all
     *  mutations via `reset()` and re-apply the fee only.  The tec rollback
     *  is automatic — there is no need to order mutations defensively or
     *  undo partial changes before returning `tec*`.
     *
     *  @return `tesSUCCESS` or a `tec*` error.  Must not return `tem*`,
     *      `tef*`, or `ter*` codes (those belong in preflight/preclaim).
     */
    virtual TER
    doApply() = 0;

    /** Inspect a single ledger entry modified by this transaction.
     *
     *  Called once for every SLE created, modified, or deleted by the
     *  transaction, before finalizeInvariants.  Implementations should
     *  accumulate whatever state they need to verify transaction-specific
     *  post-conditions.
     *
     *  @param isDelete  true if the entry was erased from the ledger.
     *  @param before    the entry's state before the transaction (nullptr
     *                   for newly created entries).
     *  @param after     the entry's state as supplied by the apply logic
     *                   for this transaction. For deletions, this is the
     *                   SLE being erased and is not guaranteed to be null;
     *                   callers must use isDelete rather than after == nullptr
     *                   to detect deletions.
     */
    virtual void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) = 0;

    /** Check transaction-specific post-conditions after all entries have
     *  been visited.
     *
     *  Called once after every modified ledger entry has been passed to
     *  visitInvariantEntry.  Returns true if all transaction-specific
     *  invariants hold, or false to fail the transaction with
     *  tecINVARIANT_FAILED.
     *
     *  @param tx    the transaction being applied.
     *  @param result the tentative TER result so far.
     *  @param fee   the fee consumed by the transaction.
     *  @param view  read-only view of the ledger after the transaction.
     *  @param j     journal for logging invariant failures.
     *
     *  @return true if all invariants pass; false otherwise.
     */
    [[nodiscard]] virtual bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) = 0;

    /** Compute the load-scaled minimum fee required to relay this transaction.
     *
     *  Scales `baseFee` using the node's current `LoadFeeTrack`.  The
     *  `TapUnlimited` flag suppresses load scaling (used for locally-submitted
     *  or admin transactions).
     *
     *  @param registry  Service registry (provides `getFeeTrack()`).
     *  @param baseFee   Unscaled base fee from `calculateBaseFee()`.
     *  @param fees      Fee schedule from the current ledger.
     *  @param flags     Apply flags; `TapUnlimited` disables load scaling.
     *  @return Minimum fee in drops that the network will accept.
     */
    static XRPAmount
    minimumFee(ServiceRegistry& registry, XRPAmount baseFee, Fees const& fees, ApplyFlags flags);

    /** Return the owner-reserve increment as a fee, in drops.
     *
     *  Used by transactions that create a ledger object and wish to charge
     *  one full reserve increment as the transaction fee (e.g.,
     *  `AccountDelete`, `AMMCreate`, `LoanBrokerSet`).
     *  Asserts that the reserve increment is at least 100× the base fee,
     *  ensuring the anti-spam reserve is meaningful.
     *
     *  @param view  Read-only ledger view (supplies `fees().increment`).
     *  @param tx    The transaction (unused; present for uniformity).
     *  @return `fees().increment` in drops.
     */
    static XRPAmount
    calculateOwnerReserveFee(ReadView const& view, STTx const& tx);

    /** Low-level signature check used by both the preclaim and batch paths.
     *
     *  Selects between `checkMultiSign` and `checkSingleSign` based on
     *  transaction contents.  Handles the special cases for batch inner
     *  transactions (no signature required), dry-run simulation (no key or
     *  signers is valid), and pseudo-account rejection under
     *  `featureLendingProtocol`.
     *
     *  The public `checkSign(PreclaimContext const&)` overload is a thin
     *  wrapper around this one.
     *
     *  @param view           Read-only ledger view.
     *  @param flags          Apply flags.
     *  @param parentBatchId  Set for batch inner transactions; suppresses sig check.
     *  @param idAccount      The account whose key must authorize the transaction.
     *  @param sigObject      The STObject containing `sfSigningPubKey` /
     *                        `sfSigners` (usually `ctx.tx`).
     *  @param j              Journal for trace logging.
     *  @return `tesSUCCESS` or a `tef*` error.
     */
    static NotTEC
    checkSign(
        ReadView const& view,
        ApplyFlags flags,
        std::optional<uint256 const> const& parentBatchId,
        AccountID const& idAccount,
        STObject const& sigObject,
        beast::Journal const j);

    /** Amendment gate hook — override to gate the transaction on amendments.
     *
     *  Called by `invokePreflight<T>` before `preflight1`.  The base
     *  implementation always returns `true` (no extra gating).  Derived
     *  classes that depend on amendments not listed in `transactions.macro`
     *  should override this method; return `false` to produce `temDISABLED`.
     *
     *  @param ctx  Preflight context.
     *  @return `true` if the transaction is permitted; `false` to disable it.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Flag-mask hook — override to declare valid flags for this transaction.
     *
     *  The returned mask is passed to `preflight0` to reject unknown flag bits.
     *  The base implementation returns `tfUniversalMask`.  Derived classes
     *  should override this to OR in their transaction-specific flag bits.
     *
     *  @param ctx  Preflight context.
     *  @return Bitmask of all valid flag bits for this transaction type.
     */
    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    /** Post-signature preflight hook — override for expensive post-sig checks.
     *
     *  Called by `invokePreflight<T>` after `preflight2` (signature
     *  verification).  The base implementation returns `tesSUCCESS`.
     *  Derived classes that need to perform expensive checks that can only
     *  run after the signature is verified (e.g., crypto-condition validation
     *  in `EscrowFinish`) should override this.
     *
     *  @param ctx  Preflight context.
     *  @return `tesSUCCESS` or a `tem*` error.
     */
    static NotTEC
    preflightSigValidated(PreflightContext const& ctx);

    /** Validate an optional blob field's length.
     *
     *  Returns `false` if the slice is present but empty or exceeds
     *  `maxLength`; returns `true` if absent or within bounds.
     *
     *  @param slice      Optional blob (e.g., from `tx[~sfURI]`).
     *  @param maxLength  Maximum permitted byte length.
     *  @return `true` if the length is valid.
     */
    static bool
    validDataLength(std::optional<Slice> const& slice, std::size_t maxLength);

    /** Validate that an optional numeric field is within `[min, max]`.
     *
     *  An absent optional (`std::nullopt`) is treated as valid — only
     *  present values are range-checked.  This reflects the convention that
     *  optional fields are legal to omit.
     *
     *  @tparam T    Numeric type (must support `<=` comparison).
     *  @param value Optional field value.
     *  @param max   Inclusive upper bound.
     *  @param min   Inclusive lower bound (default-constructed, usually 0).
     *  @return `true` if absent or within `[min, max]`.
     */
    template <class T>
    static bool
    validNumericRange(std::optional<T> value, T max, T min = T{});

    /** Validate an optional strong-unit numeric field within `[min, max]`.
     *
     *  Overload for `unit::ValueUnit<Unit, T>` bounds to maintain type
     *  safety across unit systems.  Delegates to the plain-value overload.
     *
     *  @tparam T     Underlying numeric type.
     *  @tparam Unit  Unit tag.
     *  @param value  Optional field value (raw numeric).
     *  @param max    Inclusive upper bound (unit-typed).
     *  @param min    Inclusive lower bound (unit-typed, default zero).
     *  @return `true` if absent or within `[min, max]`.
     */
    template <class T, class Unit>
    static bool
    validNumericRange(
        std::optional<T> value,
        unit::ValueUnit<Unit, T> max,
        unit::ValueUnit<Unit, T> min = unit::ValueUnit<Unit, T>{});

    /** Validate that an optional numeric field is at least `min`.
     *
     *  An absent optional is treated as valid.
     *
     *  @tparam T    Numeric type.
     *  @param value Optional field value.
     *  @param min   Inclusive lower bound (default-constructed, usually 0).
     *  @return `true` if absent or `>= min`.
     */
    template <class T>
    static bool
    validNumericMinimum(std::optional<T> value, T min = T{});

    /** Validate an optional strong-unit numeric field against a minimum.
     *
     *  Overload for `unit::ValueUnit<Unit, T>` bounds.  Delegates to the
     *  plain-value overload.
     *
     *  @tparam T     Underlying numeric type.
     *  @tparam Unit  Unit tag.
     *  @param value  Optional field value.
     *  @param min    Inclusive lower bound (unit-typed, default zero).
     *  @return `true` if absent or `>= min`.
     */
    template <class T, class Unit>
    static bool
    validNumericMinimum(
        std::optional<T> value,
        unit::ValueUnit<Unit, T> min = unit::ValueUnit<Unit, T>{});

private:
    /** Roll back all doApply mutations and re-apply fee deduction only.
     *
     *  Calls `ctx_.discard()` to discard all ledger changes, then
     *  re-deducts the fee from the fee payer's balance (clamped to the
     *  available balance), and re-consumes the sequence/ticket.  Used for
     *  fee-claiming `tec*` results and after invariant failures.
     *
     *  @param fee  Requested fee in drops; clamped to available balance.
     *  @return `{tesSUCCESS, actualFee}` on success, or
     *          `{tefINTERNAL, 0}` if the account SLE is missing (ledger
     *          corruption).
     */
    std::pair<TER, XRPAmount>
    reset(XRPAmount fee);

    /** Advance `sfSequence` or consume the Ticket for this transaction.
     *
     *  For sequence-based transactions, increments `sfSequence` by one.
     *  For ticket-based transactions, delegates to `ticketDelete`.
     *
     *  @param sleAccount  Mutable SLE for the submitting account.
     *  @return `tesSUCCESS` or `tefBAD_LEDGER` if the ticket is missing.
     */
    TER
    consumeSeqProxy(SLE::pointer const& sleAccount);

    /** Deduct the transaction fee from the fee payer's balance.
     *
     *  Reads `sfFee` from the transaction and subtracts it from the fee
     *  payer's `sfBalance`.  The caller is responsible for calling
     *  `view().update(sle)` to commit the change.
     *
     *  @return `tesSUCCESS` or `tefINTERNAL` if the payer account is absent.
     */
    TER
    payFee();

    /** Verify a single-signature transaction against the account root.
     *
     *  Checks, in priority order: regular key → enabled master key →
     *  disabled master key (`tefMASTER_DISABLED`) → unknown key
     *  (`tefBAD_AUTH`).
     *
     *  @param view        Read-only ledger view.
     *  @param idSigner    AccountID derived from the signing public key.
     *  @param idAccount   AccountID from `sfAccount` (the authorizing account).
     *  @param sleAccount  AccountRoot SLE for `idAccount`.
     *  @param j           Journal for trace logging.
     *  @return `tesSUCCESS`, `tefMASTER_DISABLED`, or `tefBAD_AUTH`.
     */
    static NotTEC
    checkSingleSign(
        ReadView const& view,
        AccountID const& idSigner,
        AccountID const& idAccount,
        std::shared_ptr<SLE const> sleAccount,
        beast::Journal const j);

    /** Verify a multi-signature against the account's SignerList.
     *
     *  Performs an O(n) linear merge of the sorted `sfSigners` array from
     *  the transaction against the sorted `SignerEntry` list from the
     *  account's signer list SLE.  Every signer in the transaction must
     *  appear in the account's signer list and pass key verification.
     *  Returns `tefBAD_QUORUM` if the accumulated weight is below
     *  `sfSignerQuorum`.
     *
     *  @param view       Read-only ledger view.
     *  @param flags      Apply flags (used for dry-run simulation handling).
     *  @param id         The account whose signer list governs authorization.
     *  @param sigObject  The STObject containing `sfSigners`.
     *  @param j          Journal for trace logging.
     *  @return `tesSUCCESS`, `tefNOT_MULTI_SIGNING`, `tefBAD_SIGNATURE`,
     *          `tefMASTER_DISABLED`, or `tefBAD_QUORUM`.
     */
    static NotTEC
    checkMultiSign(
        ReadView const& view,
        ApplyFlags flags,
        AccountID const& id,
        STObject const& sigObject,
        beast::Journal const j);

    /** Named breakpoint for replaying specific transactions under a debugger.
     *
     *  Does nothing except log at debug level.  Set a breakpoint here to
     *  pause execution when a specific transaction (identified by its hash
     *  in the service registry's trap configuration) is being applied.
     */
    void trapTransaction(uint256) const;

    /** Early sanity checks on the account field, fee field, and flags.
     *
     *  Called as step 3 of `invokePreflight<T>` (after
     *  `checkExtraFeatures`, before `T::preflight`).  Validates:
     *  - `sfDelegate` presence (requires `featurePermissionDelegationV1_1`)
     *  - `preflight0` (network ID, txid, flags via `flagMask`)
     *  - `sfAccount` is non-zero
     *  - `sfFee` is native XRP and non-negative
     *  - signing key format
     *  - ticket / AccountTxnID mutual exclusivity
     *  - `tfInnerBatchTxn` requires `featureBatch`
     *
     *  @note Do not call this from `preflight()` in derived classes.  It is
     *      invoked automatically by `invokePreflight<T>`.
     *
     *  @param ctx       Preflight context.
     *  @param flagMask  Bitmask of valid flags from `T::getFlagsMask()`.
     *  @return `tesSUCCESS` or a `tem*` / `tel*` error.
     */
    static NotTEC
    preflight1(PreflightContext const& ctx, std::uint32_t flagMask);

    /** Validate the cryptographic signature via the hash-router cache.
     *
     *  Called as step 5 of `invokePreflight<T>` (after `T::preflight`,
     *  before `T::preflightSigValidated`).  Skips the check entirely for
     *  batch inner transactions (`tfInnerBatchTxn` + `featureBatch`) since
     *  they are authorized by the outer batch's signature.  For simulation
     *  (`TapDryRun`), validates key/signer consistency but skips
     *  cryptographic verification.
     *
     *  @note Do not call this from `preflight()` in derived classes.  It is
     *      invoked automatically by `invokePreflight<T>`.
     *
     *  @param ctx  Preflight context.
     *  @return `tesSUCCESS` or `temINVALID`.
     */
    static NotTEC
    preflight2(PreflightContext const& ctx);

    /** Check transaction-specific invariants only.
     *
     *  Walks every modified ledger entry via visitInvariantEntry, then
     *  calls finalizeInvariants on the derived transactor.  Returns
     *  tecINVARIANT_FAILED if any transaction invariant is violated.
     *
     *  @param result  the tentative TER from transaction processing.
     *  @param fee     the fee consumed by the transaction.
     *
     *  @return the original result if all invariants pass, or
     *          tecINVARIANT_FAILED otherwise.
     */
    [[nodiscard]] TER
    checkTransactionInvariants(TER result, XRPAmount fee);
};

inline bool
Transactor::checkExtraFeatures(PreflightContext const& ctx)
{
    return true;
}

/** Early sanity checks on the transaction ID, network ID, and flag bits.
 *
 *  The very first check in the preflight pipeline, called from `preflight1`.
 *  Validates:
 *  - Pseudo-transactions may not carry `tfInnerBatchTxn`.
 *  - `sfNetworkID` presence/absence rules: legacy networks (ID ≤ 1024) must
 *    not include `sfNetworkID`; newer networks must include it and it must
 *    match the local node.
 *  - Transaction ID must not be all-zeros.
 *  - No flag bits outside `flagMask` may be set.
 *
 *  @param ctx       Preflight context.
 *  @param flagMask  Bitmask of valid flags for this transaction type.
 *  @return `tesSUCCESS` or a `tel*` / `tem*` error.
 */
NotTEC
preflight0(PreflightContext const& ctx, std::uint32_t flagMask);

namespace detail {

/** Validate the format of the signing public key in a transaction or signer.
 *
 *  Returns `temBAD_SIGNATURE` if the `sfSigningPubKey` field is non-empty
 *  but not a recognized key type (secp256k1 or Ed25519).  An empty key is
 *  valid (indicates multi-signing or batch inner transaction).
 *
 *  Called from `preflight1` with the transaction object.
 *
 *  @param sigObject  The STObject containing `sfSigningPubKey`.
 *  @param j          Journal for debug logging.
 *  @return `tesSUCCESS` or `temBAD_SIGNATURE`.
 */
NotTEC
preflightCheckSigningKey(STObject const& sigObject, beast::Journal j);

/** Validate signing-key state for dry-run simulation transactions.
 *
 *  Called from `preflight2` when `TapDryRun` is set.  A simulation
 *  transaction is valid if it has neither a signature nor a multi-signer
 *  list, or if it uses multi-signers with empty individual signatures.
 *  Returns `std::nullopt` when `TapDryRun` is not set (the caller should
 *  proceed to normal signature verification).
 *
 *  @param flags      Apply flags; must have `TapDryRun` set to take effect.
 *  @param sigObject  The transaction's STObject.
 *  @param j          Journal for debug logging.
 *  @return `tesSUCCESS` or `temINVALID` if the simulation keys are
 *      inconsistent; `std::nullopt` if not in simulation mode.
 */
std::optional<NotTEC>
preflightCheckSimulateKeys(ApplyFlags flags, STObject const& sigObject, beast::Journal j);
}  // namespace detail

/** Explicit preflight specialization for `Change` pseudo-transactions.
 *
 *  `Change` is a validator-generated pseudo-transaction with no real sender;
 *  its preflight logic is entirely different from normal transactions.
 *  Defined in `Change.cpp`.
 */
template <>
NotTEC
Transactor::invokePreflight<Change>(PreflightContext const& ctx);

template <class T>
NotTEC
Transactor::invokePreflight(PreflightContext const& ctx)
{
    // Using this lookup does NOT require checking the fixDelegateV1_1. The data
    // exists regardless of whether it is enabled.
    auto const feature = Permission::getInstance().getTxFeature(ctx.tx.getTxnType());

    if (feature && !ctx.rules.enabled(*feature))
        return temDISABLED;

    if (!T::checkExtraFeatures(ctx))
        return temDISABLED;

    if (auto const ret = preflight1(ctx, T::getFlagsMask(ctx)))
        return ret;

    if (auto const ret = T::preflight(ctx))
        return ret;

    if (auto const ret = preflight2(ctx))
        return ret;

    return T::preflightSigValidated(ctx);
}

template <class T>
bool
Transactor::validNumericRange(std::optional<T> value, T max, T min)
{
    if (!value)
        return true;
    return value >= min && value <= max;
}

template <class T, class Unit>
bool
Transactor::validNumericRange(
    std::optional<T> value,
    unit::ValueUnit<Unit, T> max,
    unit::ValueUnit<Unit, T> min)
{
    return validNumericRange(value, max.value(), min.value());
}

template <class T>
bool
Transactor::validNumericMinimum(std::optional<T> value, T min)
{
    if (!value)
        return true;
    return value >= min;
}

template <class T, class Unit>
bool
Transactor::validNumericMinimum(std::optional<T> value, unit::ValueUnit<Unit, T> min)
{
    return validNumericMinimum(value, min.value());
}

}  // namespace xrpl
