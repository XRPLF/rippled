/** @file
 *  Orchestration hub for the XRPL transaction processing pipeline.
 *
 *  Every transaction passes through four entry points in order: `preflight`,
 *  `preclaim`, `calculateBaseFee`, and `doApply`.  This file contains no
 *  transaction-specific business logic; its sole responsibility is the
 *  compile-time type-dispatch machinery (`withTxnType`) that routes each
 *  transaction to the correct `Transactor` subclass, and the assembly of the
 *  structured `PreflightResult`, `PreclaimResult`, and `ApplyResult` tokens
 *  that higher layers consume.
 *
 *  @note Transactor headers must not be included directly here.  All
 *      type-specific behavior is accessed through the X-macro dispatch in
 *      `transactions.macro`.  See the comment at line 37 and the macro file
 *      itself for the rationale.
 */
#include <xrpl/tx/applySteps.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <utility>
#pragma push_macro("TRANSACTION")
#undef TRANSACTION

// Do nothing
#define TRANSACTION(...)
#define TRANSACTION_INCLUDE 1

#include <xrpl/protocol/detail/transactions.macro>

#undef TRANSACTION
#pragma pop_macro("TRANSACTION")

// DO NOT INCLUDE TRANSACTOR HEADER FILES HERE.
// See the instructions at the top of transactions.macro instead.

#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/protocol/TxFormats.h>

namespace xrpl {

namespace {

/** Internal sentinel thrown when a runtime `TxType` has no entry in
 *  `transactions.macro`.
 *
 *  All four public entry points catch this; the catch blocks are marked
 *  `LCOV_EXCL` because an unrecognised type should have been rejected well
 *  before reaching this layer.
 */
struct UnknownTxnType : std::exception
{
    TxType txnType;
    UnknownTxnType(TxType t) : txnType{t}
    {
    }
};

/** Invoke a generic callable with the concrete `Transactor` subclass as a
 *  compile-time template parameter, selected by runtime `txnType`.
 *
 *  The switch statement is generated at compile time by re-including
 *  `transactions.macro` with `TRANSACTION` defined to emit one `case` label
 *  per known transaction type.  Each case calls
 *  `f.template operator()<NamedTransactor>()`, resolving all type-specific
 *  behavior without virtual dispatch and without requiring this translation
 *  unit to include any transactor headers.
 *
 *  Before dispatch, thread-local RAII guards configure numeric precision for
 *  the entire processing step:
 *  - When `featureSingleAssetVault` or `featureLendingProtocol` is active:
 *    `CurrentTransactionRulesGuard` installs `rules` into a thread-local slot,
 *    and `NumberSO` configures floating-point-style arithmetic (or legacy mode
 *    when `fixUniversalNumber` is absent).
 *  - Otherwise: `NumberMantissaScaleGuard` forces the legacy small-mantissa
 *    behavior to preserve historical correctness across all three pipeline
 *    phases.
 *
 *  @tparam F  A generic callable whose `operator()<T>()` will be invoked with
 *      `T` bound to the concrete transactor for `txnType`.
 *  @param rules  Amendment rules for the current ledger, used to select the
 *      numeric precision mode.
 *  @param txnType  The runtime transaction type to dispatch on.
 *  @param f  The callable to invoke.
 *  @return The return value of `f.template operator()<T>()`.
 *  @throws UnknownTxnType if `txnType` does not appear in `transactions.macro`.
 */
template <class F>
auto
withTxnType(Rules const& rules, TxType txnType, F&& f)
{
    // These global updates really should have been for every Transaction
    // step: preflight, preclaim, calculateBaseFee, and doApply. Unfortunately,
    // they were only included in doApply (via Transactor::operator()). That may
    // have been sufficient when the changes were only related to operations
    // that mutated data, but some features will now change how they read data,
    // so these need to be more global.
    //
    // To prevent unintentional side effects on existing checks, they will be
    // set for every operation only once SingleAssetVault (or later
    // LendingProtocol) are enabled.
    //
    // See also Transactor::operator().
    //
    std::optional<NumberSO> stNumberSO;
    std::optional<CurrentTransactionRulesGuard> rulesGuard;
    std::optional<NumberMantissaScaleGuard> mantissaScaleGuard;
    if (rules.enabled(featureSingleAssetVault) || rules.enabled(featureLendingProtocol))
    {
        // fixUniversalNumber predates the rulesGuard and should be replaced.
        stNumberSO.emplace(rules.enabled(fixUniversalNumber));
        rulesGuard.emplace(rules);
    }
    else
    {
        mantissaScaleGuard.emplace(MantissaRange::MantissaScale::Small);
    }

    switch (txnType)
    {
#pragma push_macro("TRANSACTION")
#undef TRANSACTION

#define TRANSACTION(tag, value, name, ...) \
    case tag:                              \
        return f.template operator()<name>();

#include <xrpl/protocol/detail/transactions.macro>

#undef TRANSACTION
#pragma pop_macro("TRANSACTION")
        default:
            throw UnknownTxnType(txnType);
    }
}
}  // namespace

/** Build a `TxConsequences` for a transaction whose transactor declares
 *  `ConsequencesFactory = Normal`.
 *
 *  Constructs standard fee-and-sequence consequences from the raw `STTx`.
 *  Selected at compile time by the C++20 `requires` clause on
 *  `T::kCONSEQUENCES_FACTORY`.
 *
 *  @tparam T  The concrete `Transactor` subclass.
 *  @param ctx  The preflight context carrying the transaction.
 *  @return A default `TxConsequences` summarising the fee and sequence.
 */
template <class T>
    requires(T::kCONSEQUENCES_FACTORY == Transactor::ConsequencesFactoryType::Normal)
TxConsequences
consequencesHelper(PreflightContext const& ctx)
{
    return TxConsequences(ctx.tx);
};

/** Build a `TxConsequences` for a transaction whose transactor declares
 *  `ConsequencesFactory = Blocker`.
 *
 *  Sets `Category::Blocker`, signalling to the TxQ that this transaction
 *  (e.g. `SetRegularKey`, `AccountDelete`, `SignerListSet`) may invalidate
 *  the signatures on subsequent queued transactions from the same account.
 *
 *  @tparam T  The concrete `Transactor` subclass.
 *  @param ctx  The preflight context carrying the transaction.
 *  @return A `TxConsequences` marked as a blocker.
 */
template <class T>
    requires(T::kCONSEQUENCES_FACTORY == Transactor::ConsequencesFactoryType::Blocker)
TxConsequences
consequencesHelper(PreflightContext const& ctx)
{
    return TxConsequences(ctx.tx, TxConsequences::Category::Blocker);
};

/** Build a `TxConsequences` for a transaction whose transactor declares
 *  `ConsequencesFactory = Custom`.
 *
 *  Delegates entirely to `T::makeTxConsequences(ctx)`, which the transactor
 *  must implement.  Used by `Payment` (`sfSendMax` XRP spend), `OfferCreate`
 *  (XRP `TakerGets`), `TicketCreate` (multi-sequence), `AccountSet`
 *  (conditional blocker), and `LoanSet` (counterparty signers).
 *
 *  @tparam T  The concrete `Transactor` subclass; must provide a static
 *      `makeTxConsequences(PreflightContext const&)`.
 *  @param ctx  The preflight context carrying the transaction.
 *  @return The `TxConsequences` produced by `T::makeTxConsequences`.
 */
template <class T>
    requires(T::kCONSEQUENCES_FACTORY == Transactor::ConsequencesFactoryType::Custom)
TxConsequences
consequencesHelper(PreflightContext const& ctx)
{
    return T::makeTxConsequences(ctx);
};

/** Run the preflight phase for the transaction in `ctx`.
 *
 *  Dispatches to `Transactor::invokePreflight<T>` for the concrete transactor
 *  type, then builds `TxConsequences` via `consequencesHelper<T>` on success,
 *  or from the error code on failure.
 *
 *  @param ctx  The preflight context.
 *  @return A pair of `(NotTEC, TxConsequences)`: the validation result and the
 *      worst-case queue cost estimate.
 */
static std::pair<NotTEC, TxConsequences>
invokePreflight(PreflightContext const& ctx)
{
    try
    {
        return withTxnType(ctx.rules, ctx.tx.getTxnType(), [&]<typename T>() {
            auto const tec = Transactor::invokePreflight<T>(ctx);
            return std::make_pair(
                tec, isTesSuccess(tec) ? consequencesHelper<T>(ctx) : TxConsequences{tec});
        });
    }
    catch (UnknownTxnType const& e)
    {
        // Should never happen
        // LCOV_EXCL_START
        JLOG(ctx.j.fatal()) << "Unknown transaction type in preflight: " << e.txnType;
        UNREACHABLE("xrpl::invokePreflight : unknown transaction type");
        return {temUNKNOWN, TxConsequences{temUNKNOWN}};
        // LCOV_EXCL_STOP
    }
}

/** Run the preclaim phase for the transaction in `ctx`.
 *
 *  Executes a fixed sequence of static checks via compile-time name hiding
 *  (not virtual dispatch): `checkSeqProxy`, `checkPriorTxAndLastLedger`,
 *  `checkPermission`, `checkSign`, then `checkFee`, and finally
 *  `T::preclaim(ctx)` for transactor-specific ledger validation.
 *
 *  @param ctx  The preclaim context, carrying a read-only ledger view.
 *  @return A `TER` result: `tesSUCCESS`, a `tec*` fee-claiming failure, or a
 *      non-claiming `tem*`/`tef*`/`ter*` code.
 *
 *  @note **Security invariant:** every check up to and including `checkSign`
 *      must return `NotTEC` (never a `tec*` code).  A `tec*` result from a
 *      pre-signature check would cause a fee to be charged before the sender's
 *      identity is authenticated, enabling theft or destruction of funds.
 *      Only `checkFee` and the transactor-specific `preclaim` may return the
 *      full `TER` range.
 */
static TER
invokePreclaim(PreclaimContext const& ctx)
{
    try
    {
        return withTxnType(ctx.view.rules(), ctx.tx.getTxnType(), [&]<typename T>() -> TER {
            auto const id = ctx.tx.getAccountID(sfAccount);

            if (id != beast::kZERO)
            {
                if (NotTEC const preSigResult = [&]() -> NotTEC {
                        if (NotTEC const result = T::checkSeqProxy(ctx.view, ctx.tx, ctx.j))
                            return result;

                        if (NotTEC const result = T::checkPriorTxAndLastLedger(ctx))
                            return result;

                        if (NotTEC const result = T::checkPermission(ctx.view, ctx.tx))
                            return result;

                        if (NotTEC const result = T::checkSign(ctx))
                            return result;

                        return tesSUCCESS;
                    }())
                    return preSigResult;

                if (TER const result = T::checkFee(ctx, calculateBaseFee(ctx.view, ctx.tx)))
                    return result;
            }

            return T::preclaim(ctx);
        });
    }
    catch (UnknownTxnType const& e)
    {
        // Should never happen
        // LCOV_EXCL_START
        JLOG(ctx.j.fatal()) << "Unknown transaction type in preclaim: " << e.txnType;
        UNREACHABLE("xrpl::invokePreclaim : unknown transaction type");
        return temUNKNOWN;
        // LCOV_EXCL_STOP
    }
}

/** Compute the base fee for `tx` by dispatching to the concrete transactor's
 *  `calculateBaseFee` static method.
 *
 *  @param view  Read-only ledger view used by fee overrides (e.g. multi-signer
 *      count from `SignerListSet`).
 *  @param tx    The transaction whose fee is being calculated.
 *  @return The base fee in drops.  Returns zero if the transaction type is
 *      unrecognised (unreachable in production).
 */
static XRPAmount
invokeCalculateBaseFee(ReadView const& view, STTx const& tx)
{
    try
    {
        return withTxnType(view.rules(), tx.getTxnType(), [&]<typename T>() {
            return T::calculateBaseFee(view, tx);
        });
    }
    catch (UnknownTxnType const& e)
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::invoke_calculateBaseFee : unknown transaction type");
        return XRPAmount{0};
        // LCOV_EXCL_STOP
    }
}

/** @see TxConsequences::TxConsequences(NotTEC) */
TxConsequences::TxConsequences(NotTEC pfResult)
    : isBlocker_(false)
    , fee_(beast::kZERO)
    , potentialSpend_(beast::kZERO)
    , seqProx_(SeqProxy::sequence(0))
    , sequencesConsumed_(0)
{
    XRPL_ASSERT(
        !isTesSuccess(pfResult), "xrpl::TxConsequences::TxConsequences : is not tesSUCCESS");
}

/** @see TxConsequences::TxConsequences(STTx const&)
 *
 *  @note `fee_` is extracted from `sfFee` only when the amount is native XRP
 *      and non-negative — a defensive guard against malformed or exotic fee
 *      fields that would otherwise corrupt queue accounting.
 */
TxConsequences::TxConsequences(STTx const& tx)
    : isBlocker_(false)
    , fee_(tx[sfFee].native() && !tx[sfFee].negative() ? tx[sfFee].xrp() : beast::kZERO)
    , potentialSpend_(beast::kZERO)
    , seqProx_(tx.getSeqProxy())
    , sequencesConsumed_(tx.getSeqProxy().isSeq() ? 1 : 0)
{
}

/** @see TxConsequences::TxConsequences(STTx const&, Category) */
TxConsequences::TxConsequences(STTx const& tx, Category category) : TxConsequences(tx)
{
    isBlocker_ = (category == TxConsequences::Category::Blocker);
}

/** @see TxConsequences::TxConsequences(STTx const&, XRPAmount) */
TxConsequences::TxConsequences(STTx const& tx, XRPAmount potentialSpend) : TxConsequences(tx)
{
    potentialSpend_ = potentialSpend;
}

/** @see TxConsequences::TxConsequences(STTx const&, std::uint32_t) */
TxConsequences::TxConsequences(STTx const& tx, std::uint32_t sequencesConsumed) : TxConsequences(tx)
{
    sequencesConsumed_ = sequencesConsumed;
}

/** Instantiate the concrete `Transactor` for the transaction in `ctx` and
 *  execute it.
 *
 *  Dispatches to `withTxnType`, which constructs `T p(ctx)` and calls `p()`.
 *  The call operator in `Transactor::operator()()` runs `preCompute`,
 *  `consumeSeqProxy`, `payFee`, `doApply`, and invariant checking in sequence,
 *  returning an `ApplyResult` that reflects whether the transaction was
 *  committed to the ledger.
 *
 *  @param ctx  The mutable apply context wrapping the open ledger view.
 *  @return An `ApplyResult` with the `TER` and `applied` flag.
 */
static ApplyResult
invokeApply(ApplyContext& ctx)
{
    try
    {
        return withTxnType(ctx.view().rules(), ctx.tx.getTxnType(), [&]<typename T>() {
            T p(ctx);
            return p();
        });
    }
    catch (UnknownTxnType const& e)
    {
        // Should never happen
        // LCOV_EXCL_START
        JLOG(ctx.journal.fatal()) << "Unknown transaction type in apply: " << e.txnType;
        UNREACHABLE("xrpl::invokeApply : unknown transaction type");
        return {temUNKNOWN, false};
        // LCOV_EXCL_STOP
    }
}

/** Construct a concrete `Transactor` for the transaction in `ctx`.
 *
 *  Test-only factory — not part of the public API.  Allows unit tests to
 *  instantiate and inspect a transactor without running the full apply loop.
 *
 *  @param ctx  The apply context; must outlive the returned object because the
 *      transactor holds a raw reference to it.
 *  @return A heap-allocated `Transactor` subclass for the transaction type.
 *  @throws UnknownTxnType if the transaction type is not in `transactions.macro`.
 */
std::unique_ptr<Transactor>
makeTransactor(ApplyContext& ctx)
{
    return withTxnType(
        ctx.view().rules(), ctx.tx.getTxnType(), [&]<typename T>() -> std::unique_ptr<Transactor> {
            return std::make_unique<T>(ctx);
        });
}

PreflightResult
preflight(
    ServiceRegistry& registry,
    Rules const& rules,
    STTx const& tx,
    ApplyFlags flags,
    beast::Journal j)
{
    PreflightContext const pfCtx(registry, tx, rules, flags, j);
    try
    {
        return {pfCtx, invokePreflight(pfCtx)};
    }
    catch (std::exception const& e)
    {
        JLOG(j.fatal()) << "apply (preflight): " << e.what();
        return {pfCtx, {tefEXCEPTION, TxConsequences{tx}}};
    }
}

PreflightResult
preflight(
    ServiceRegistry& registry,
    Rules const& rules,
    uint256 const& parentBatchId,
    STTx const& tx,
    ApplyFlags flags,
    beast::Journal j)
{
    PreflightContext const pfCtx(registry, tx, parentBatchId, rules, flags, j);
    try
    {
        return {pfCtx, invokePreflight(pfCtx)};
    }
    catch (std::exception const& e)
    {
        JLOG(j.fatal()) << "apply (preflight): " << e.what();
        return {pfCtx, {tefEXCEPTION, TxConsequences{tx}}};
    }
}

/** @copydoc preclaim(PreflightResult const&, ServiceRegistry&, OpenView const&)
 *
 *  @note If the amendment rules recorded in `preflightResult` differ from
 *      `view.rules()` (the ledger advanced between preflight and preclaim),
 *      this function silently re-runs `preflight` with the new rules before
 *      constructing the `PreclaimContext`.  Callers do not need to handle
 *      this race; the result always reflects the current ledger's rules.
 */
PreclaimResult
preclaim(PreflightResult const& preflightResult, ServiceRegistry& registry, OpenView const& view)
{
    std::optional<PreclaimContext const> ctx;
    if (preflightResult.rules != view.rules())
    {
        auto secondFlight = [&]() {
            if (preflightResult.parentBatchId)
            {
                return preflight(
                    registry,
                    view.rules(),
                    preflightResult.parentBatchId.value(),
                    preflightResult.tx,
                    preflightResult.flags,
                    preflightResult.j);
            }

            return preflight(
                registry,
                view.rules(),
                preflightResult.tx,
                preflightResult.flags,
                preflightResult.j);
        }();

        ctx.emplace(
            registry,
            view,
            secondFlight.ter,
            secondFlight.tx,
            secondFlight.flags,
            secondFlight.parentBatchId,
            secondFlight.j);
    }
    else
    {
        ctx.emplace(
            registry,
            view,
            preflightResult.ter,
            preflightResult.tx,
            preflightResult.flags,
            preflightResult.parentBatchId,
            preflightResult.j);
    }

    try
    {
        if (!isTesSuccess(ctx->preflightResult))
            return {*ctx, ctx->preflightResult};
        return {*ctx, invokePreclaim(*ctx)};
    }
    catch (std::exception const& e)
    {
        JLOG(ctx->j.fatal()) << "apply (preclaim): " << e.what();
        return {*ctx, tefEXCEPTION};
    }
}

XRPAmount
calculateBaseFee(ReadView const& view, STTx const& tx)
{
    return invokeCalculateBaseFee(view, tx);
}

XRPAmount
calculateDefaultBaseFee(ReadView const& view, STTx const& tx)
{
    return Transactor::calculateBaseFee(view, tx);
}

/** @copydoc doApply(PreclaimResult const&, ServiceRegistry&, OpenView&)
 *
 *  @note Returns `{tefEXCEPTION, false}` immediately — without applying — if
 *      the ledger sequence recorded in `preclaimResult.view` does not match
 *      `view.seq()`.  This represents a caller logic error (preclaim and
 *      doApply were called against different ledger generations) from which
 *      there is insufficient context to recover.
 */
ApplyResult
doApply(PreclaimResult const& preclaimResult, ServiceRegistry& registry, OpenView& view)
{
    if (preclaimResult.view.seq() != view.seq())
    {
        // Logic error from the caller. Don't have enough
        // info to recover.
        return {tefEXCEPTION, false};
    }
    try
    {
        if (!preclaimResult.likelyToClaimFee)
            return {preclaimResult.ter, false};
        ApplyContext ctx(
            registry,
            view,
            preclaimResult.parentBatchId,
            preclaimResult.tx,
            preclaimResult.ter,
            calculateBaseFee(view, preclaimResult.tx),
            preclaimResult.flags,
            preclaimResult.j);
        return invokeApply(ctx);
    }
    catch (std::exception const& e)
    {
        JLOG(preclaimResult.j.fatal()) << "apply: " << e.what();
        return {tefEXCEPTION, false};
    }
}

}  // namespace xrpl
