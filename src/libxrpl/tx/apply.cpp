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

// These are the same flags defined as HashRouterFlags::PRIVATE1-4 in
// HashRouter.h
constexpr HashRouterFlags kSfSigbad = HashRouterFlags::PRIVATE1;     // Signature is bad
constexpr HashRouterFlags kSfSiggood = HashRouterFlags::PRIVATE2;    // Signature is good
constexpr HashRouterFlags kSfLocalbad = HashRouterFlags::PRIVATE3;   // Local checks failed
constexpr HashRouterFlags kSfLocalgood = HashRouterFlags::PRIVATE4;  // Local checks passed

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
                router.setFlags(id, kSfLocalbad);
                return {Validity::SigGoodOnly, reason};
            }

            router.setFlags(id, kSfSiggood);
            return {Validity::Valid, ""};
        }
    }

    if (any(flags & kSfSigbad))
    {
        // Signature is known bad
        return {Validity::SigBad, "Transaction has bad signature."};
    }

    if (!any(flags & kSfSiggood))
    {
        auto const sigVerify = tx.checkSign(rules);
        if (!sigVerify)
        {
            router.setFlags(id, kSfSigbad);
            return {Validity::SigBad, sigVerify.error()};
        }
        router.setFlags(id, kSfSiggood);
    }

    // Signature is now known good
    if (any(flags & kSfLocalbad))
    {
        // ...but the local checks
        // are known bad.
        return {Validity::SigGoodOnly, "Local checks failed."};
    }

    if (any(flags & kSfLocalgood))
    {
        // ...and the local checks
        // are known good.
        return {Validity::Valid, ""};
    }

    // Do the local checks
    std::string reason;
    if (!passesLocalChecks(tx, reason))
    {
        router.setFlags(id, kSfLocalbad);
        return {Validity::SigGoodOnly, reason};
    }
    router.setFlags(id, kSfLocalgood);
    return {Validity::Valid, ""};
}

void
forceValidity(HashRouter& router, uint256 const& txid, Validity validity)
{
    HashRouterFlags flags = HashRouterFlags::UNDEFINED;
    switch (validity)
    {
        case Validity::Valid:
            flags |= kSfLocalgood;
            [[fallthrough]];
        case Validity::SigGoodOnly:
            flags |= kSfSiggood;
            [[fallthrough]];
        case Validity::SigBad:
            // would be silly to call directly
            break;
    }
    if (any(flags))
        router.setFlags(txid, flags);
}

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
        OpenView perTxBatchView(kBatchView, batchView);

        auto const ret = apply(registry, perTxBatchView, parentBatchId, tx, TapBatch, j);
        XRPL_ASSERT(
            ret.applied == (isTesSuccess(ret.ter) || isTecClaim(ret.ter)),
            "Inner transaction should not be applied");

        JLOG(j.debug()) << "BatchTrace[" << parentBatchId << "]: " << tx.getTransactionID() << " "
                        << (ret.applied ? "applied" : "failure") << ": " << transToken(ret.ter);

        // If the transaction should be applied push its changes to the
        // whole-batch view.
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
    // Returns false if the transaction has need not be retried.
    if (retryAssured)
        flags = flags | TapRetry;

    JLOG(j.debug()) << "TXN " << txn.getTransactionID() << (retryAssured ? "/retry" : "/final");

    try
    {
        auto const result = apply(registry, view, txn, flags, j);

        if (result.applied)
        {
            JLOG(j.debug()) << "Transaction applied: " << transToken(result.ter);

            // The batch transaction was just applied; now we need to apply
            // its inner transactions as necessary.
            if (isTesSuccess(result.ter) && txn.getTxnType() == ttBATCH)
            {
                OpenView wholeBatchView(kBatchView, view);

                if (applyBatchTransactions(registry, wholeBatchView, txn, j))
                    wholeBatchView.apply(view);
            }

            return ApplyTransactionResult::Success;
        }

        if (isTefFailure(result.ter) || isTemMalformed(result.ter) || isTelLocal(result.ter))
        {
            // failure
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
