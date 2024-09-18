//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#include <xrpld/app/tx/apply.h>
#include <xrpld/app/tx/detail/Batch.h>
#include <xrpld/ledger/Sandbox.h>
#include <xrpld/ledger/View.h>
#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

TxConsequences
Batch::makeTxConsequences(PreflightContext const& ctx)
{
    return TxConsequences{ctx.tx, TxConsequences::normal};
}

NotTEC
Batch::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureBatch))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto& tx = ctx.tx;

    if (tx.getFlags() & tfBatchMask)
    {
        JLOG(ctx.j.warn()) << "Batch: invalid flags.";
        return temINVALID_FLAG;
    }

    AccountID const outerAccount = tx.getAccountID(sfAccount);

    auto const& txns = tx.getFieldArray(sfRawTransactions);
    STVector256 const hashes = tx.getFieldV256(sfTxIDs);
    if (hashes.size() != txns.size())
    {
        JLOG(ctx.j.warn()) << "Batch: Hashes array size does not match txns.";
        return temINVALID;
    }

    if (txns.empty())
    {
        JLOG(ctx.j.warn()) << "Batch: txns array empty.";
        return temARRAY_EMPTY;
    }

    if (txns.size() > 8)
    {
        JLOG(ctx.j.warn()) << "Batch: txns array exceeds 12 entries.";
        return temARRAY_TOO_LARGE;
    }

    for (int i = 0; i < txns.size(); ++i)
    {
        STTx const stx = STTx{STObject(txns[i])};
        if (stx.getTransactionID() != hashes[i])
        {
            JLOG(ctx.j.warn()) << "Batch: Hashes array does not match txns.";
            return temINVALID;
        }

        auto const innerAccount = stx.getAccountID(sfAccount);
        if (!stx.isFieldPresent(sfTransactionType))
        {
            JLOG(ctx.j.warn())
                << "Batch: TransactionType missing in array entry.";
            return temINVALID_BATCH;
        }
        if (stx.getFieldU16(sfTransactionType) == ttBATCH)
        {
            JLOG(ctx.j.warn()) << "Batch: batch cannot have inner batch txn.";
            return temINVALID_BATCH;
        }

        if (stx.getFieldU16(sfTransactionType) == ttACCOUNT_DELETE &&
            innerAccount == outerAccount)
        {
            JLOG(ctx.j.warn())
                << "Batch: inner txn cannot be account delete when inner and "
                   "outer accounts are the same.";
            return temINVALID_BATCH;
        }

        if (innerAccount != outerAccount)
        {
            if (tx.getFieldArray(sfBatchSigners).end() ==
                std::find_if(
                    tx.getFieldArray(sfBatchSigners).begin(),
                    tx.getFieldArray(sfBatchSigners).end(),
                    [innerAccount](STObject const& signer) {
                        return signer.getAccountID(sfAccount) == innerAccount;
                    }))
            {
                JLOG(ctx.j.warn())
                    << "Batch: inner txn not signed by the right user.";
                return temBAD_SIGNER;
            }
        }
    }

    if (tx.isFieldPresent(sfBatchSigners))
    {
        auto const requireCanonicalSig =
            ctx.rules.enabled(featureRequireFullyCanonicalSig)
            ? STTx::RequireFullyCanonicalSig::yes
            : STTx::RequireFullyCanonicalSig::no;
        auto const sigResult =
            ctx.tx.checkBatchSign(requireCanonicalSig, ctx.rules);
        if (!sigResult)
        {
            JLOG(ctx.j.warn()) << "Batch: invalid batch txn signature.";
            return temBAD_SIGNATURE;
        }
    }

    return preflight2(ctx);
}

TER
Batch::preclaim(PreclaimContext const& ctx)
{
    if (!ctx.view.rules().enabled(featureBatch))
        return temDISABLED;

    for (STObject txn : ctx.tx.getFieldArray(sfRawTransactions))
    {
        auto const innerAccount = txn.getAccountID(sfAccount);
        auto const sle = ctx.view.read(keylet::account(innerAccount));
        if (!sle)
        {
            JLOG(ctx.j.warn()) << "Batch: delay: inner account does not exist.";
            return terNO_ACCOUNT;
        }
    }
    return tesSUCCESS;
}

TER
Batch::doApply()
{
    Sandbox sb(&ctx_.view());
    bool changed = false;
    auto const flags = ctx_.tx.getFlags();

    AccountID const outerAccount = ctx_.tx.getAccountID(sfAccount);

    TER result = tesSUCCESS;
    ApplyViewImpl& avi = dynamic_cast<ApplyViewImpl&>(ctx_.view());
    OpenView subView(&sb);

    auto const& txns = ctx_.tx.getFieldArray(sfRawTransactions);
    bool const not3rdParty = std::any_of(
        txns.begin(), txns.end(), [outerAccount](STObject const& txn) {
            return txn.getAccountID(sfAccount) == outerAccount;
        });

    for (STObject txn : txns)
    {
        OpenView innerView(&subView);

        STTx const stx = STTx{std::move(txn)};
        auto const [ter, applied] =
            ripple::apply(ctx_.app, innerView, stx, tapFAIL_HARD, ctx_.journal);

        if (applied)
            innerView.apply(subView);

        changed = true;

        // Add Inner Txn Metadata
        STObject meta{sfBatchExecution};
        std::string res = transToken(ter);
        meta.setFieldVL(sfInnerResult, ripple::Slice{res.data(), res.size()});
        meta.setFieldU16(sfTransactionType, stx.getTxnType());
        if (ter == tesSUCCESS || isTecClaim(ter))
            meta.setFieldH256(sfTransactionHash, stx.getTransactionID());
        avi.addBatchExecutionMetaData(std::move(meta));

        if (ter != tesSUCCESS)
        {
            // Atomic Revert on non tec failure
            if (!isTecClaim(ter))
            {
                JLOG(ctx_.journal.warn()) << "Batch: Inner txn failed." << ter;
                result = tecBATCH_FAILURE;
                changed = false;
                break;
            }

            if (flags & tfUntilFailure)
            {
                result = tesSUCCESS;
                break;
            }
            if (flags & tfOnlyOne)
            {
                continue;
            }
            if (flags & tfAllOrNothing)
            {
                result = tecBATCH_FAILURE;
                changed = false;
                break;
            }
        }

        if (ter == tesSUCCESS && flags & tfOnlyOne)
        {
            result = tesSUCCESS;
            break;
        }
    }

    // Apply SubView & PreviousFields
    if (changed)
    {
        // Only required when not 3rd Party
        if (not3rdParty)
            ctx_.batchPrevious(avi);

        ctx_.applyOpenView(subView);
    }

    sb.apply(ctx_.rawView());
    return result;
}

XRPAmount
Batch::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // Calculate the Inner Txn Fees
    XRPAmount extraFee{0};
    if (tx.isFieldPresent(sfRawTransactions))
    {
        XRPAmount txFees{0};
        auto const& txns = tx.getFieldArray(sfRawTransactions);
        for (STObject txn : txns)
        {
            STTx const stx = STTx{std::move(txn)};
            txFees += Transactor::calculateBaseFee(view, tx);
        }
        extraFee += txFees;
    }

    // Calculate the BatchSigners Fees
    if (tx.isFieldPresent(sfBatchSigners))
    {
        auto const signers = tx.getFieldArray(sfBatchSigners);
        extraFee += (signers.size() + 2) * view.fees().base;
    }

    return extraFee;
}

}  // namespace ripple