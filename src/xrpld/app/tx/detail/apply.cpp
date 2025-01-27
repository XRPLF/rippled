//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpld/app/misc/HashRouter.h>
#include <xrpld/app/tx/apply.h>
#include <xrpld/app/tx/applySteps.h>
#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

// These are the same flags defined as SF_PRIVATE1-4 in HashRouter.h
#define SF_SIGBAD SF_PRIVATE1     // Signature is bad
#define SF_SIGGOOD SF_PRIVATE2    // Signature is good
#define SF_LOCALBAD SF_PRIVATE3   // Local checks failed
#define SF_LOCALGOOD SF_PRIVATE4  // Local checks passed

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

    if (flags & SF_SIGBAD)
        // Signature is known bad
        return {Validity::SigBad, "Transaction has bad signature."};

    if (!(flags & SF_SIGGOOD))
    {
        // Don't know signature state. Check it.
        auto const requireCanonicalSig =
            rules.enabled(featureRequireFullyCanonicalSig)
            ? STTx::RequireFullyCanonicalSig::yes
            : STTx::RequireFullyCanonicalSig::no;

        auto const sigVerify = tx.checkSign(requireCanonicalSig, rules);
        if (!sigVerify)
        {
            router.setFlags(id, SF_SIGBAD);
            return {Validity::SigBad, sigVerify.error()};
        }
        router.setFlags(id, SF_SIGGOOD);
    }

    // Signature is now known good
    if (flags & SF_LOCALBAD)
        // ...but the local checks
        // are known bad.
        return {Validity::SigGoodOnly, "Local checks failed."};

    if (flags & SF_LOCALGOOD)
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
    int flags = 0;
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
    if (flags)
        router.setFlags(txid, flags);
}

template <typename PreflightChecks>
std::pair<TER, bool>
apply(Application& app, OpenView& view, PreflightChecks&& preflightChecks)
{
    STAmountSO stAmountSO{view.rules().enabled(fixSTAmountCanonicalize)};
    NumberSO stNumberSO{view.rules().enabled(fixUniversalNumber)};

    return doApply(preclaim(preflightChecks(), app, view), app, view);
}

std::pair<TER, bool>
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

std::pair<TER, bool>
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

    auto applyOneTransaction = [&app, &j, &parentBatchId, &batchView](
                                   STTx&& tx) {
        OpenView perTxBatchView(batch_view, batchView);

        auto const ret =
            apply(app, perTxBatchView, parentBatchId, tx, tapBATCH, j);
        XRPL_ASSERT(
            ret.second == (isTesSuccess(ret.first) || isTecClaim(ret.first)),
            "Inner transaction should not be applied");

        JLOG(j.trace()) << "BatchTrace[" << parentBatchId
                        << "]: " << tx.getTransactionID() << " "
                        << (ret.second ? "applied" : "failure") << ": "
                        << transToken(ret.first);

        // If the transaction should be applied push its changes to the
        // whole-batch view.
        if (ret.second && (isTesSuccess(ret.first) || isTecClaim(ret.first)))
            perTxBatchView.apply(batchView);

        return ret;
    };

    int applied = 0;

    for (STObject rb : batchTxn.getFieldArray(sfRawTransactions))
    {
        auto const result = applyOneTransaction(STTx{std::move(rb)});
        XRPL_ASSERT(
            result.second ==
                (isTesSuccess(result.first) || isTecClaim(result.first)),
            "Outer Batch failure, inner transaction should not be applied");

        if (result.second)
            ++applied;

        if (!isTesSuccess(result.first))
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

ApplyResult
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

        if (result.second)
        {
            JLOG(j.debug())
                << "Transaction applied: " << transToken(result.first);

            // The batch transaction was just applied; now we need to apply
            // its inner transactions as necessary.
            if (isTesSuccess(result.first) && txn.getTxnType() == ttBATCH)
            {
                OpenView wholeBatchView(batch_view, view);

                if (applyBatchTransactions(app, wholeBatchView, txn, j))
                    wholeBatchView.apply(view);
            }

            return ApplyResult::Success;
        }

        if (isTefFailure(result.first) || isTemMalformed(result.first) ||
            isTelLocal(result.first))
        {
            // failure
            JLOG(j.debug())
                << "Transaction failure: " << transToken(result.first);
            return ApplyResult::Fail;
        }

        JLOG(j.debug()) << "Transaction retry: " << transToken(result.first);
        return ApplyResult::Retry;
    }
    catch (std::exception const& ex)
    {
        JLOG(j.warn()) << "Throws: " << ex.what();
        return ApplyResult::Fail;
    }
}

}  // namespace ripple
