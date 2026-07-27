#include <xrpl/tx/applySteps.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/telemetry/SpanGuard.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/detail/TxApplySpanNames.h>

#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string_view>
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

struct UnknownTxnType : std::exception
{
    TxType txnType;
    UnknownTxnType(TxType t) : txnType{t}
    {
    }
};

/**
 * Look up the human-readable transaction type name for span attributes.
 * Returns nullptr if the type is unknown so the caller can skip the
 * attribute rather than emit an empty value.
 */
char const*
txTypeName(TxType txnType)
{
    if (auto const* fmt = TxFormats::getInstance().findByType(txnType))
        return fmt->getName().c_str();
    return nullptr;
}

/**
 * Create a deterministic-trace span for an apply-pipeline stage.
 *
 * The trace_id is derived from txID[0:16] so the preflight, preclaim, and
 * transactor spans of one transaction share a trace even though they run
 * sequentially and often on different threads. Sets the stage, tx_type, and
 * (after the stage runs) ter_result attributes that drive the collector
 * spanmetrics dimensions. A no-op when telemetry is disabled.
 *
 * @param name   Full span name (tx_apply_span::preflight / ::preclaim).
 * @param stage  Stage attribute value (tx_apply_span::val::*).
 * @param tx     The transaction supplying the id and type.
 * @param curLedgerSeq         Seq of the ledger being worked on, set as the
 *                             current_ledger_seq attribute. Passed by the
 *                             view-bearing stages (preclaim); nullopt for the
 *                             stateless preflight, which then omits it.
 * @param curLedgerParentHash  Parent hash of that ledger, set as
 *                             current_ledger_hash. nullptr to omit.
 */
[[nodiscard]] telemetry::SpanGuard
makeStageSpan(
    std::string_view name,
    std::string_view stage,
    STTx const& tx,
    std::optional<LedgerIndex> curLedgerSeq = std::nullopt,
    uint256 const* curLedgerParentHash = nullptr)
{
    auto const txID = tx.getTransactionID();
    auto span = telemetry::SpanGuard::hashSpan(
        telemetry::TraceCategory::Transactions, name, txID.data(), txID.kBytes);
    // Guard the type lookup behind the active check: preflight runs for every
    // transaction, so findByType() must not run when tracing is off/disabled.
    if (span)
    {
        span.setAttribute(telemetry::tx_apply_span::attr::stage, stage);
        if (char const* typeName = txTypeName(tx.getTxnType()))
        {
            span.setAttribute(telemetry::tx_apply_span::attr::txType, typeName);
        }
        // The ledger being worked on — set only by the view-bearing stages
        // (preclaim/transactor). Preflight is stateless and passes nullopt, so
        // it carries no ledger attribute (documented exception).
        if (curLedgerSeq)
        {
            span.setAttribute(
                telemetry::tx_apply_span::attr::currentLedgerSeq,
                static_cast<std::int64_t>(*curLedgerSeq));
        }
        if (curLedgerParentHash != nullptr)
        {
            span.setAttribute(
                telemetry::tx_apply_span::attr::currentLedgerHash,
                to_string(*curLedgerParentHash).c_str());
        }
    }
    return span;
}

// Call a lambda with the concrete transaction type as a template parameter
// throw an "UnknownTxnType" exception on error
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
    // set for every operation only once at least one of the relevant amendments
    // are enabled.
    //
    // See also Transactor::operator().
    //
    std::optional<CurrentTransactionRulesGuard> rulesGuard;
    std::optional<NumberMantissaScaleGuard> mantissaScaleGuard;
    createGuards(rules, rulesGuard, mantissaScaleGuard);

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

// Templates so preflight does the right thing with T::kConsequencesFactory.
//
// This could be done more easily using if constexpr, but Visual Studio
// 2017 doesn't handle if constexpr correctly.  So once we're no longer
// building with Visual Studio 2017 we can consider replacing the four
// templates with a single template function that uses if constexpr.
//
// For ConsequencesFactoryType::Normal
//

template <class T>
    requires(T::kConsequencesFactory == Transactor::ConsequencesFactoryType::Normal)
TxConsequences
consequencesHelper(PreflightContext const& ctx)
{
    return TxConsequences(ctx.tx);
};

// For ConsequencesFactoryType::Blocker
template <class T>
    requires(T::kConsequencesFactory == Transactor::ConsequencesFactoryType::Blocker)
TxConsequences
consequencesHelper(PreflightContext const& ctx)
{
    return TxConsequences(ctx.tx, TxConsequences::Category::Blocker);
};

// For ConsequencesFactoryType::Custom
template <class T>
    requires(T::kConsequencesFactory == Transactor::ConsequencesFactoryType::Custom)
TxConsequences
consequencesHelper(PreflightContext const& ctx)
{
    return T::makeTxConsequences(ctx);
};

static std::pair<NotTEC, TxConsequences>
invokePreflight(PreflightContext const& ctx)
{
    // Trace the preflight stage. The span shares the transaction's
    // deterministic trace_id so it correlates with preclaim and transactor.
    auto span = makeStageSpan(
        telemetry::tx_apply_span::preflight, telemetry::tx_apply_span::val::preflight, ctx.tx);
    try
    {
        auto result = withTxnType(ctx.rules, ctx.tx.getTxnType(), [&]<typename T>() {
            auto const tec = Transactor::invokePreflight<T>(ctx);
            return std::make_pair(
                tec, isTesSuccess(tec) ? consequencesHelper<T>(ctx) : TxConsequences{tec});
        });
        if (span)
        {
            span.setAttribute(
                telemetry::tx_apply_span::attr::terResult, transToken(result.first).c_str());
            // Mark the span as errored when preflight rejects the transaction so
            // failed stages surface in span-status error counts.
            if (!isTesSuccess(result.first))
                span.setError(transToken(result.first));
        }
        return result;
    }
    catch (UnknownTxnType const& e)
    {
        // Should never happen
        // LCOV_EXCL_START
        JLOG(ctx.j.fatal()) << "Unknown transaction type in preflight: " << e.txnType;
        span.recordException(e);
        UNREACHABLE("xrpl::invokePreflight : unknown transaction type");
        return {temUNKNOWN, TxConsequences{temUNKNOWN}};
        // LCOV_EXCL_STOP
    }
    catch (std::exception const& e)
    {
        // The caller's preflight() maps this to tefEXCEPTION. Record it on the
        // span before unwinding so per-stage error counts include exceptions.
        span.setAttribute(
            telemetry::tx_apply_span::attr::terResult, transToken(tefEXCEPTION).c_str());
        span.recordException(e);
        throw;
    }
}

static TER
invokePreclaim(PreclaimContext const& ctx)
{
    // Trace the preclaim stage under the transaction's deterministic trace_id.
    // Preclaim has a ledger view, so tag the span with the ledger being worked
    // on (seq + parent hash) to correlate it to the ledger/consensus trace.
    auto span = makeStageSpan(
        telemetry::tx_apply_span::preclaim,
        telemetry::tx_apply_span::val::preclaim,
        ctx.tx,
        ctx.view.seq(),
        &ctx.view.header().parentHash);
    try
    {
        // use name hiding to accomplish compile-time polymorphism of static
        // class functions for Transactor and derived classes.
        TER const preclaimTer =
            withTxnType(ctx.view.rules(), ctx.tx.getTxnType(), [&]<typename T>() -> TER {
                // preclaim functionality is divided into two sections:
                // 1. Up to and including the signature check: returns NotTEC.
                //    All transaction checks before and including checkSign
                //    MUST return NotTEC, or something more restrictive.
                //    Allowing tec results in these steps risks theft or
                //    destruction of funds, as a fee will be charged before the
                //    signature is checked.
                // 2. After the signature check: returns TER.

                // If the transactor requires a valid account and the
                // transaction doesn't list one, preflight will have already
                // a flagged a failure.
                auto const id = ctx.tx.getAccountID(sfAccount);

                if (id != beast::kZero)
                {
                    if (NotTEC const preSigResult = [&]() -> NotTEC {
                            if (NotTEC const result = T::checkSeqProxy(ctx.view, ctx.tx, ctx.j))
                                return result;

                            if (NotTEC const result = T::checkPriorTxAndLastLedger(ctx))
                                return result;

                            if (NotTEC const result = T::checkSponsor(ctx.view, ctx.tx))
                                return result;

                            if (NotTEC const result =
                                    Transactor::invokeCheckPermission<T>(ctx.view, ctx.tx))
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
        if (span)
        {
            span.setAttribute(
                telemetry::tx_apply_span::attr::terResult, transToken(preclaimTer).c_str());
        }
        return preclaimTer;
    }
    catch (UnknownTxnType const& e)
    {
        // Should never happen
        // LCOV_EXCL_START
        JLOG(ctx.j.fatal()) << "Unknown transaction type in preclaim: " << e.txnType;
        span.recordException(e);
        UNREACHABLE("xrpl::invokePreclaim : unknown transaction type");
        return temUNKNOWN;
        // LCOV_EXCL_STOP
    }
    catch (std::exception const& e)
    {
        // The caller's preclaim() maps this to tefEXCEPTION. Record it on the
        // span before unwinding so per-stage error counts include exceptions.
        span.setAttribute(
            telemetry::tx_apply_span::attr::terResult, transToken(tefEXCEPTION).c_str());
        span.recordException(e);
        throw;
    }
}

/**
 * @brief Calculates the base fee for a given transaction.
 *
 * This function determines the base fee required for the specified transaction
 * by invoking the appropriate fee calculation logic based on the transaction
 * type. It uses a type-dispatch mechanism to select the correct calculation
 * method.
 *
 * @param view The ledger view to use for fee calculation.
 * @param tx The transaction for which the base fee is to be calculated.
 * @return The calculated base fee as an XRPAmount.
 *
 * @throws std::exception If an error occurs during fee calculation, including
 * but not limited to unknown transaction types or internal errors, the function
 * logs an error and returns an XRPAmount of zero.
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

TxConsequences::TxConsequences(NotTEC pfResult)
    : isBlocker_(false)
    , fee_(beast::kZero)
    , potentialSpend_(beast::kZero)
    , seqProx_(SeqProxy::sequence(0))
    , sequencesConsumed_(0)
{
    XRPL_ASSERT(
        !isTesSuccess(pfResult), "xrpl::TxConsequences::TxConsequences : is not tesSUCCESS");
}

TxConsequences::TxConsequences(STTx const& tx)
    : isBlocker_(false)
    , fee_(tx[sfFee].native() && !tx[sfFee].negative() ? tx[sfFee].xrp() : beast::kZero)
    , potentialSpend_(beast::kZero)
    , seqProx_(tx.getSeqProxy())
    , sequencesConsumed_(tx.getSeqProxy().isSeq() ? 1 : 0)
{
}

TxConsequences::TxConsequences(STTx const& tx, Category category) : TxConsequences(tx)
{
    isBlocker_ = (category == TxConsequences::Category::Blocker);
}

TxConsequences::TxConsequences(STTx const& tx, XRPAmount potentialSpend) : TxConsequences(tx)
{
    potentialSpend_ = potentialSpend;
}

TxConsequences::TxConsequences(STTx const& tx, std::uint32_t sequencesConsumed) : TxConsequences(tx)
{
    sequencesConsumed_ = sequencesConsumed;
}

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

// Test-only factory — not part of the public API.
// The returned Transactor holds a raw reference to ctx; the caller must ensure
// the ApplyContext outlives the Transactor.
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
