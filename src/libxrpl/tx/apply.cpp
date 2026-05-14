/** @file
 *  Top-level transaction application coordinator for the XRP Ledger.
 *
 *  Bridges the network-level validity cache (`HashRouter`) with the
 *  stateless-then-stateful application pipeline defined in `applySteps.cpp`.
 *  Where `applySteps.cpp` contains the mechanics of each pipeline stage
 *  (`preflight → preclaim → doApply`), this file decides *when* to run them,
 *  how to short-circuit redundant work via the hash-router cache, and how to
 *  handle the multi-transaction semantics of the Batch feature.
 */
#include <xrpl/tx/apply.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/HashRouter.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/tx/applySteps.h>

#include <exception>
#include <string>
#include <utility>

namespace xrpl {

// --- HashRouter private flag aliases (PRIVATE1–PRIVATE4) ---

/** Cached flag: transaction signature failed verification. */
constexpr HashRouterFlags kSF_SIGBAD = HashRouterFlags::PRIVATE1;
/** Cached flag: transaction signature passed verification. */
constexpr HashRouterFlags kSF_SIGGOOD = HashRouterFlags::PRIVATE2;
/** Cached flag: local well-formedness checks failed (`passesLocalChecks`). */
constexpr HashRouterFlags kSF_LOCALBAD = HashRouterFlags::PRIVATE3;
/** Cached flag: local well-formedness checks passed (`passesLocalChecks`). */
constexpr HashRouterFlags kSF_LOCALGOOD = HashRouterFlags::PRIVATE4;

//------------------------------------------------------------------------------

std::pair<Validity, std::string>
checkValidity(HashRouter& router, STTx const& tx, Rules const& rules)
{
    auto const id = tx.getTransactionID();
    auto const flags = router.getFlags(id);

    // Ignore signature check on batch inner transactions
    if (tx.isFlag(tfInnerBatchTxn) && rules.enabled(featureBatch))
    {
        // Defensive Check: These values are also checked in Batch::preflight
        if (tx.isFieldPresent(sfTxnSignature) || !tx.getSigningPubKey().empty() ||
            tx.isFieldPresent(sfSigners))
            return {Validity::SigBad, "Malformed: Invalid inner batch transaction."};

        // This block should probably have never been included in the
        // original `Batch` implementation. An inner transaction never
        // has a valid signature.
        bool const neverValid = rules.enabled(fixBatchInnerSigs);
        if (!neverValid)
        {
            std::string reason;
            if (!passesLocalChecks(tx, reason))
            {
                router.setFlags(id, kSF_LOCALBAD);
                return {Validity::SigGoodOnly, reason};
            }

            router.setFlags(id, kSF_SIGGOOD);
            return {Validity::Valid, ""};
        }
    }

    if (any(flags & kSF_SIGBAD))
        return {Validity::SigBad, "Transaction has bad signature."};

    if (!any(flags & kSF_SIGGOOD))
    {
        auto const sigVerify = tx.checkSign(rules);
        if (!sigVerify)
        {
            router.setFlags(id, kSF_SIGBAD);
            return {Validity::SigBad, sigVerify.error()};
        }
        router.setFlags(id, kSF_SIGGOOD);
    }

    // Signature is now known good.
    if (any(flags & kSF_LOCALBAD))
        return {Validity::SigGoodOnly, "Local checks failed."};

    if (any(flags & kSF_LOCALGOOD))
        return {Validity::Valid, ""};

    std::string reason;
    if (!passesLocalChecks(tx, reason))
    {
        router.setFlags(id, kSF_LOCALBAD);
        return {Validity::SigGoodOnly, reason};
    }
    router.setFlags(id, kSF_LOCALGOOD);
    return {Validity::Valid, ""};
}

void
forceValidity(HashRouter& router, uint256 const& txid, Validity validity)
{
    HashRouterFlags flags = HashRouterFlags::UNDEFINED;
    switch (validity)
    {
        case Validity::Valid:
            flags |= kSF_LOCALGOOD;
            [[fallthrough]];
        case Validity::SigGoodOnly:
            flags |= kSF_SIGGOOD;
            [[fallthrough]];
        case Validity::SigBad:
            // No flag to set: calling forceValidity with SigBad is intentionally a no-op.
            break;
    }
    if (any(flags))
        router.setFlags(txid, flags);
}

/** Core apply implementation: invoke a preflight callable, then run preclaim and doApply.
 *
 *  Accepting a callable rather than a `PreflightResult` keeps the sequencing
 *  enforced: preflight results can be produced on a different thread (they hold
 *  no ledger references), but preclaim and doApply must run together against
 *  the same view. The `NumberSO` RAII guard is installed here — before preclaim
 *  — so the correct fixed-point arithmetic mode is active for the entire
 *  post-preflight pipeline.
 *
 *  @tparam PreflightChecks Callable returning a `PreflightResult`.
 *  @param registry The service registry providing transactor implementations.
 *  @param view The open ledger view for preclaim and doApply.
 *  @param preflightChecks Callable that produces the `PreflightResult`.
 *  @return An `ApplyResult` with the TER code and whether mutations were committed.
 */
template <typename PreflightChecks>
ApplyResult
apply(ServiceRegistry& registry, OpenView& view, PreflightChecks&& preflightChecks)
{
    NumberSO const stNumberSO{view.rules().enabled(fixUniversalNumber)};
    return doApply(preclaim(preflightChecks(), registry, view), registry, view);
}

ApplyResult
apply(ServiceRegistry& registry, OpenView& view, STTx const& tx, ApplyFlags flags, beast::Journal j)
{
    return apply(
        registry, view, [&]() mutable { return preflight(registry, view.rules(), tx, flags, j); });
}

/** Apply a batch inner transaction, supplying the enclosing batch's ID.
 *
 *  The `parentBatchId` is threaded into the preflight context so the transactor
 *  can enforce inner-batch signing rules (no `sfTxnSignature`, empty
 *  `sfSigningPubKey`). The `TapBatch` flag should be set in `flags`.
 *
 *  @param registry The service registry providing transactor implementations.
 *  @param view The per-transaction batch sub-view created by `applyBatchTransactions`.
 *  @param parentBatchId Transaction ID of the enclosing `ttBATCH` transaction.
 *  @param tx The inner transaction to apply.
 *  @param flags Apply flags; must include `TapBatch`.
 *  @param j Logging sink.
 *  @return An `ApplyResult` with the TER code and whether mutations were committed.
 */
ApplyResult
apply(
    ServiceRegistry& registry,
    OpenView& view,
    uint256 const& parentBatchId,
    STTx const& tx,
    ApplyFlags flags,
    beast::Journal j)
{
    return apply(registry, view, [&]() mutable {
        return preflight(registry, view.rules(), parentBatchId, tx, flags, j);
    });
}

/** Execute the inner transactions of a committed `ttBATCH` transaction.
 *
 *  The outer batch transaction must already be applied before this is called;
 *  its fee and sequence have been consumed. Inner transactions run against a
 *  two-level view stack: `batchView` (all-or-nothing relative to the caller's
 *  outer view) wraps a per-transaction `perTxBatchView`. If an inner transaction
 *  is applied, its changes are promoted from `perTxBatchView` to `batchView`.
 *
 *  Execution policy is read from the batch transaction's flags:
 *  - `tfAllOrNothing`: any failure causes immediate return of `false`.
 *  - `tfUntilFailure`: iteration stops at the first failure; prior successes kept.
 *  - `tfOnlyOne`: stops after the first success; subsequent transactions skipped.
 *  - `tfIndependent` (no special flag): all inner transactions run independently.
 *
 *  @param registry The service registry providing transactor implementations.
 *  @param batchView The whole-batch `OpenView` wrapping the outer ledger view.
 *      The caller promotes this to the outer view only if the function returns
 *      `true`.
 *  @param batchTxn The outer `ttBATCH` transaction containing `sfRawTransactions`.
 *  @param j Logging sink; each inner result is logged with a `BatchTrace` prefix.
 *  @return `true` if at least one inner transaction was applied and the batch
 *      execution policy allows the aggregate to be committed; `false` otherwise.
 */
static bool
applyBatchTransactions(
    ServiceRegistry& registry,
    OpenView& batchView,
    STTx const& batchTxn,
    beast::Journal j)
{
    XRPL_ASSERT(
        batchTxn.getTxnType() == ttBATCH && !batchTxn.getFieldArray(sfRawTransactions).empty(),
        "Batch transaction missing sfRawTransactions");

    auto const parentBatchId = batchTxn.getTransactionID();
    auto const mode = batchTxn.getFlags();

    auto applyOneTransaction = [&registry, &j, &parentBatchId, &batchView](STTx const& tx) {
        OpenView perTxBatchView(kBATCH_VIEW, batchView);

        auto const ret = apply(registry, perTxBatchView, parentBatchId, tx, TapBatch, j);
        XRPL_ASSERT(
            ret.applied == (isTesSuccess(ret.ter) || isTecClaim(ret.ter)),
            "Inner transaction should not be applied");

        JLOG(j.debug()) << "BatchTrace[" << parentBatchId << "]: " << tx.getTransactionID() << " "
                        << (ret.applied ? "applied" : "failure") << ": " << transToken(ret.ter);

        if (ret.applied && (isTesSuccess(ret.ter) || isTecClaim(ret.ter)))
            perTxBatchView.apply(batchView);

        return ret;
    };

    int applied = 0;

    for (STObject rb : batchTxn.getFieldArray(sfRawTransactions))
    {
        auto const result = applyOneTransaction(STTx{std::move(rb)});
        XRPL_ASSERT(
            result.applied == (isTesSuccess(result.ter) || isTecClaim(result.ter)),
            "Outer Batch failure, inner transaction should not be applied");

        if (result.applied)
            ++applied;

        if (!isTesSuccess(result.ter))
        {
            if ((mode & tfAllOrNothing) != 0u)
                return false;

            if ((mode & tfUntilFailure) != 0u)
                break;
        }
        else if ((mode & tfOnlyOne) != 0u)
        {
            break;
        }
    }

    return applied != 0;
}

ApplyTransactionResult
applyTransaction(
    ServiceRegistry& registry,
    OpenView& view,
    STTx const& txn,
    bool retryAssured,
    ApplyFlags flags,
    beast::Journal j)
{
    if (retryAssured)
        flags = flags | TapRetry;

    JLOG(j.debug()) << "TXN " << txn.getTransactionID() << (retryAssured ? "/retry" : "/final");

    try
    {
        auto const result = apply(registry, view, txn, flags, j);

        if (result.applied)
        {
            JLOG(j.debug()) << "Transaction applied: " << transToken(result.ter);

            if (isTesSuccess(result.ter) && txn.getTxnType() == ttBATCH)
            {
                OpenView wholeBatchView(kBATCH_VIEW, view);

                if (applyBatchTransactions(registry, wholeBatchView, txn, j))
                    wholeBatchView.apply(view);
            }

            return ApplyTransactionResult::Success;
        }

        if (isTefFailure(result.ter) || isTemMalformed(result.ter) || isTelLocal(result.ter))
        {
            JLOG(j.debug()) << "Transaction failure: " << transHuman(result.ter);
            return ApplyTransactionResult::Fail;
        }

        JLOG(j.debug()) << "Transaction retry: " << transHuman(result.ter);
        return ApplyTransactionResult::Retry;
    }
    catch (std::exception const& ex)
    {
        JLOG(j.warn()) << "Throws: " << ex.what();
        return ApplyTransactionResult::Fail;
    }
}

}  // namespace xrpl
