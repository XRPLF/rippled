#include <xrpld/app/misc/HashRouter.h>
#include <xrpld/app/tx/apply.h>
#include <xrpld/app/tx/applySteps.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

// These are the same flags defined as HashRouterFlags::PRIVATE1-4 in
// HashRouter.h
constexpr HashRouterFlags SF_SIGBAD =
    HashRouterFlags::PRIVATE1;  // Signature is bad
constexpr HashRouterFlags SF_SIGGOOD =
    HashRouterFlags::PRIVATE2;  // Signature is good
constexpr HashRouterFlags SF_LOCALBAD =
    HashRouterFlags::PRIVATE3;  // Local checks failed
constexpr HashRouterFlags SF_LOCALGOOD =
    HashRouterFlags::PRIVATE4;  // Local checks passed

//------------------------------------------------------------------------------

std::pair<Validity, std::string>
checkValidity(
    HashRouter& router,
    STTx const& tx,
    Rules const& rules,
    Config const& config)
{
    auto const id = tx.getTransactionID();
    auto const flags = router.getFlags(id);

    // Ignore signature check on batch inner transactions
    if (tx.isFlag(tfInnerBatchTxn) && rules.enabled(featureBatch))
    {
        // Defensive Check: These values are also checked in Batch::preflight
        if (tx.isFieldPresent(sfTxnSignature) ||
            !tx.getSigningPubKey().empty() || tx.isFieldPresent(sfSigners))
            return {
                Validity::SigBad,
                "Malformed: Invalid inner batch transaction."};

        std::string reason;
        if (!passesLocalChecks(tx, reason))
        {
            router.setFlags(id, SF_LOCALBAD);
            return {Validity::SigGoodOnly, reason};
        }

        router.setFlags(id, SF_SIGGOOD);
        return {Validity::Valid, ""};
    }

    if (any(flags & SF_SIGBAD))
        // Signature is known bad
        return {Validity::SigBad, "Transaction has bad signature."};

    if (!any(flags & SF_SIGGOOD))
    {
        auto const sigVerify = tx.checkSign(rules);
        if (!sigVerify)
        {
            router.setFlags(id, SF_SIGBAD);
            return {Validity::SigBad, sigVerify.error()};
        }
        router.setFlags(id, SF_SIGGOOD);
    }

    // Signature is now known good
    if (any(flags & SF_LOCALBAD))
        // ...but the local checks
        // are known bad.
        return {Validity::SigGoodOnly, "Local checks failed."};

    if (any(flags & SF_LOCALGOOD))
        // ...and the local checks
        // are known good.
        return {Validity::Valid, ""};

    // Do the local checks
    std::string reason;
    if (!passesLocalChecks(tx, reason))
    {
        router.setFlags(id, SF_LOCALBAD);
        return {Validity::SigGoodOnly, reason};
    }
    router.setFlags(id, SF_LOCALGOOD);
    return {Validity::Valid, ""};
}

void
forceValidity(HashRouter& router, uint256 const& txid, Validity validity)
{
    HashRouterFlags flags = HashRouterFlags::UNDEFINED;
    switch (validity)
    {
        case Validity::Valid:
            flags |= SF_LOCALGOOD;
            [[fallthrough]];
        case Validity::SigGoodOnly:
            flags |= SF_SIGGOOD;
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
apply(Application& app, OpenView& view, PreflightChecks&& preflightChecks)
{
    NumberSO stNumberSO{view.rules().enabled(fixUniversalNumber)};
    return doApply(preclaim(preflightChecks(), app, view), app, view);
}

ApplyResult
apply(
    Application& app,
    OpenView& view,
    STTx const& tx,
    ApplyFlags flags,
    beast::Journal j)
{
    return apply(app, view, [&]() mutable {
        return preflight(app, view.rules(), tx, flags, j);
    });
}

ApplyResult
apply(
    Application& app,
    OpenView& view,
    uint256 const& parentBatchId,
    STTx const& tx,
    ApplyFlags flags,
    beast::Journal j)
{
    return apply(app, view, [&]() mutable {
        return preflight(app, view.rules(), parentBatchId, tx, flags, j);
    });
}

static bool
applyBatchTransactions(
    Application& app,
    OpenView& batchView,
    STTx const& batchTxn,
    beast::Journal j)
{
    XRPL_ASSERT(
        batchTxn.getTxnType() == ttBATCH &&
            batchTxn.getFieldArray(sfRawTransactions).size() != 0,
        "Batch transaction missing sfRawTransactions");

    auto const parentBatchId = batchTxn.getTransactionID();
    auto const mode = batchTxn.getFlags();

    auto applyOneTransaction =
        [&app, &j, &parentBatchId, &batchView](STTx&& tx) {
            OpenView perTxBatchView(batch_view, batchView);

            auto const ret =
                apply(app, perTxBatchView, parentBatchId, tx, tapBATCH, j);
            XRPL_ASSERT(
                ret.applied == (isTesSuccess(ret.ter) || isTecClaim(ret.ter)),
                "Inner transaction should not be applied");

            JLOG(j.debug()) << "BatchTrace[" << parentBatchId
                            << "]: " << tx.getTransactionID() << " "
                            << (ret.applied ? "applied" : "failure") << ": "
                            << transToken(ret.ter);

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
            result.applied ==
                (isTesSuccess(result.ter) || isTecClaim(result.ter)),
            "Outer Batch failure, inner transaction should not be applied");

        if (result.applied)
            ++applied;

        if (!isTesSuccess(result.ter))
        {
            if (mode & tfAllOrNothing)
                return false;

            if (mode & tfUntilFailure)
                break;
        }
        else if (mode & tfOnlyOne)
            break;
    }

    return applied != 0;
}

ApplyTransactionResult
applyTransaction(
    Application& app,
    OpenView& view,
    STTx const& txn,
    bool retryAssured,
    ApplyFlags flags,
    beast::Journal j)
{
    // Returns false if the transaction has need not be retried.
    if (retryAssured)
        flags = flags | tapRETRY;

    JLOG(j.debug()) << "TXN " << txn.getTransactionID()
                    << (retryAssured ? "/retry" : "/final");

    try
    {
        auto const result = apply(app, view, txn, flags, j);

        if (result.applied)
        {
            JLOG(j.debug())
                << "Transaction applied: " << transToken(result.ter);

            // The batch transaction was just applied; now we need to apply
            // its inner transactions as necessary.
            if (isTesSuccess(result.ter) && txn.getTxnType() == ttBATCH)
            {
                OpenView wholeBatchView(batch_view, view);

                if (applyBatchTransactions(app, wholeBatchView, txn, j))
                    wholeBatchView.apply(view);
            }

            return ApplyTransactionResult::Success;
        }

        if (isTefFailure(result.ter) || isTemMalformed(result.ter) ||
            isTelLocal(result.ter))
        {
            // failure
            JLOG(j.debug())
                << "Transaction failure: " << transHuman(result.ter);
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

}  // namespace ripple
