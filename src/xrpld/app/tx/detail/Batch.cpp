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

NotTEC
Batch::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureBatch))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto const flags = ctx.tx.getFlags();
    if (flags & tfBatchMask)
    {
        JLOG(ctx.j.trace()) << "Batch: invalid flags.";
        return temINVALID_FLAG;
    }

    if (std::popcount(flags & tfBatchSubTx) != 1)
    {
        JLOG(ctx.j.trace()) << "Batch: too many flags.";
        return temMALFORMED;
    }

    AccountID const outerAccount = ctx.tx.getAccountID(sfAccount);

    auto const& txns = ctx.tx.getFieldArray(sfRawTransactions);
    STVector256 const& hashes = ctx.tx.getFieldV256(sfTxIDs);
    if (hashes.size() != txns.size())
    {
        JLOG(ctx.j.trace()) << "Batch: hashes array size does not match txns.";
        return temMALFORMED;
    }

    if (txns.empty())
    {
        JLOG(ctx.j.trace()) << "Batch: txns array empty.";
        return temARRAY_EMPTY;
    }

    if (txns.size() > 8)
    {
        JLOG(ctx.j.trace()) << "Batch: txns array exceeds 8 entries.";
        return temARRAY_TOO_LARGE;
    }

    auto const ret = preflight2(ctx);
    if (!isTesSuccess(ret))
        return ret;

    std::set<AccountID> batchSignersSet;
    if (ctx.tx.isFieldPresent(sfBatchSigners))
    {
        STArray const signers = ctx.tx.getFieldArray(sfBatchSigners);

        // Check that the batch signers array is not too large.
        if (signers.size() > 8)
        {
            JLOG(ctx.j.trace()) << "Batch: signers array exceeds 8 entries.";
            return temARRAY_TOO_LARGE;
        }

        // Add the batch signers to the set.
        for (auto const& signer : signers)
        {
            AccountID const innerAccount = signer.getAccountID(sfAccount);
            if (!batchSignersSet.insert(innerAccount).second)
            {
                JLOG(ctx.j.trace())
                    << "Batch: Duplicate signer found: " << innerAccount;
                return temINVALID_BATCH;
            }
        }

        // Check the batch signers signatures.
        auto const requireCanonicalSig =
            ctx.rules.enabled(featureRequireFullyCanonicalSig)
            ? STTx::RequireFullyCanonicalSig::yes
            : STTx::RequireFullyCanonicalSig::no;
        auto const sigResult =
            ctx.tx.checkBatchSign(requireCanonicalSig, ctx.rules);

        if (!sigResult)
        {
            JLOG(ctx.j.trace()) << "Batch: invalid batch txn signature.";
            return temBAD_SIGNATURE;
        }
    }

    std::set<AccountID> uniqueSigners;
    std::set<uint256> uniqueHashes;
    for (int i = 0; i < txns.size(); ++i)
    {
        if (!uniqueHashes.insert(hashes[i]).second)
        {
            JLOG(ctx.j.trace()) << "Batch: duplicate TxID found.";
            return temMALFORMED;
        }

        STTx const stx = STTx{STObject(txns[i])};
        if (stx.getTransactionID() != hashes[i])
        {
            JLOG(ctx.j.trace()) << "Batch: txn hash does not match TxIDs hash."
                                << "index: " << i;
            return temMALFORMED;
        }

        if (!stx.isFieldPresent(sfTransactionType))
        {
            JLOG(ctx.j.trace())
                << "Batch: TransactionType missing in inner txn."
                << "index: " << i;
            return temINVALID_BATCH;
        }

        if (stx.getFieldU16(sfTransactionType) == ttBATCH)
        {
            JLOG(ctx.j.trace())
                << "Batch: batch cannot have an inner batch txn."
                << "index: " << i;
            return temINVALID_BATCH;
        }

        auto const innerAccount = stx.getAccountID(sfAccount);
        if (stx.getFieldU16(sfTransactionType) == ttACCOUNT_DELETE &&
            innerAccount == outerAccount)
        {
            JLOG(ctx.j.trace())
                << "Batch: inner txn cannot be account delete when inner and "
                   "outer accounts are the same."
                << "index: " << i;
            return temINVALID_BATCH;
        }

        // If the inner account is the same as the outer account, continue.
        // 1. We do not add it to the unique signers set.
        // 2. We do not check a signature for the inner account exist.
        if (innerAccount == outerAccount)
            continue;

        // Add the inner account to the unique signers set.
        uniqueSigners.insert(innerAccount);

        // Validate that the account for this (inner) txn has a signature in the
        // batch signers array.
        if (ctx.tx.isFieldPresent(sfBatchSigners) &&
            batchSignersSet.find(innerAccount) == batchSignersSet.end())
        {
            JLOG(ctx.j.trace()) << "Batch: no account signature for inner txn."
                                << "index: " << i;
            return temBAD_SIGNER;
        }
    }

    if (ctx.tx.isFieldPresent(sfBatchSigners) &&
        uniqueSigners.size() != ctx.tx.getFieldArray(sfBatchSigners).size())
    {
        JLOG(ctx.j.trace())
            << "Batch: unique signers does not match batch signers.";
        return temBAD_SIGNER;
    }

    return tesSUCCESS;
}

TER
Batch::preclaim(PreclaimContext const& ctx)
{
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
                JLOG(ctx_.journal.trace()) << "Batch: Inner txn failed." << ter;
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

}  // namespace ripple