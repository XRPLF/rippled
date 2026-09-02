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

// This file owns HashRouterFlags::PRIVATE1-4 and PRIVATE7-8 in HashRouter.h.
// These are the first four; the other two are below.
constexpr HashRouterFlags kSfSigbad = HashRouterFlags::PRIVATE1;     // Signature is bad
constexpr HashRouterFlags kSfSiggood = HashRouterFlags::PRIVATE2;    // Signature is good
constexpr HashRouterFlags kSfLocalbad = HashRouterFlags::PRIVATE3;   // Local checks failed
constexpr HashRouterFlags kSfLocalgood = HashRouterFlags::PRIVATE4;  // Local checks passed

// Before fixCleanup3_4_0, a signature in an alternate role field, such as
// sfSponsorSignature, covered the same bytes as the top level signature. Which
// bytes a role signature must cover therefore depends on whether the fix is
// enabled, but the four flags above record only the verdict, not the rules that
// produced it. A verdict reached under one prefix would otherwise be reused
// under the other.
//
// The two flags below hold the verdict for the pre-fix prefixes, so the pre-fix
// and post-fix verdicts occupy separate slots and neither is ever read in the
// other's era. Nothing is cleared when the amendment activates: setFlags only
// sets bits, so a stale pre-fix verdict simply stops being read and ages out
// with the rest of the routing table.
//
// This is not one switchover at a single instant. The era is chosen per call
// from the rules passed in, and callers do not agree on the rules: relay and
// submit verify against the validated rules, which lag the open ledger rules
// that preflight2 verifies against. At the amendment's flag ledger the same
// transaction can therefore be checked under both prefixes, on the same node,
// at the same time.
//
// Remove these two flags, and oldPrefixSig below, when Cleanup3_4_0 is retired
// in features.macro.
constexpr HashRouterFlags kSfSigbadOldPrefix = HashRouterFlags::PRIVATE7;
constexpr HashRouterFlags kSfSiggoodOldPrefix = HashRouterFlags::PRIVATE8;

//------------------------------------------------------------------------------

std::pair<Validity, std::string>
checkValidity(HashRouter& router, STTx const& tx, Rules const& rules)
{
    auto const id = tx.getTransactionID();
    auto const flags = router.getFlags(id);

    // Batch inner transactions are never independently valid: they are applied
    // within their batch, not through checkValidity. Reaching here means one was
    // relayed or submitted on its own, so mark it bad regardless of the
    // amendment (like PeerImp and NetworkOPs).
    if (tx.isFlag(tfInnerBatchTxn))
    {
        router.setFlags(id, kSfSigbad);
        return {Validity::SigBad, "Batch inner transactions are never considered validly signed."};
    }

    // Pick the cache slot for this call's era; see kSfSiggoodOldPrefix above.
    // Only a transaction that carries a role signature, and only while the fix
    // is disabled, uses the separate slot. Every other transaction, and every
    // transaction once the fix is enabled, uses the ordinary flags and verifies
    // exactly once, so there is no steady state cost.
    //
    // Both directions matter. A good verdict from before the fix must not let a
    // signature moved between roles survive the amendment, and a bad verdict
    // from before the fix must not condemn a transaction that the new prefixes
    // accept.
    //
    // Whether a transaction carries a role signature is fixed for its ID: the
    // fields are kNotSigning, so they are excluded from the signed bytes, but
    // they are still covered by the transaction ID. Repeat calls for one ID
    // therefore always agree on which slot pair to use.
    bool const oldPrefixSig = !rules.enabled(fixCleanup3_4_0) &&
        (tx.isFieldPresent(sfSponsorSignature) || tx.isFieldPresent(sfCounterpartySignature));
    auto const sigbadFlag = oldPrefixSig ? kSfSigbadOldPrefix : kSfSigbad;
    auto const siggoodFlag = oldPrefixSig ? kSfSiggoodOldPrefix : kSfSiggood;

    if (any(flags & sigbadFlag))
    {
        // Signature is known bad
        return {Validity::SigBad, "Transaction has bad signature."};
    }

    if (!any(flags & siggoodFlag))
    {
        auto const sigVerify = tx.checkSign(rules);
        if (!sigVerify)
        {
            router.setFlags(id, sigbadFlag);
            return {Validity::SigBad, sigVerify.error()};
        }
        router.setFlags(id, siggoodFlag);
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
    // Callers reach here when they deliberately skip signature verification,
    // such as a cluster peer that trusts its neighbor's checks. No signature
    // was verified, so there is no prefix era to record, and this writes the
    // ordinary flags whether or not fixCleanup3_4_0 is enabled.
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
        // NOTE: each inner tx is individually capped at kOversizeMetaDataCap;
        // there is no aggregate cap here. Bounded by kMaxBatchTxCount * cap,
        // which standalone txns can already produce in one ledger.
        if (ret.applied && (isTesSuccess(ret.ter) || isTecClaim(ret.ter)))
            perTxBatchView.apply(batchView);

        return ret;
    };

    int applied = 0;

    for (auto const& stx : batchTxn.getBatchTransactions())
    {
        auto const result = applyOneTransaction(*stx);
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
