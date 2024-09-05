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
        JLOG(ctx.j.debug()) << "Batch: invalid flags.";
        return temINVALID_FLAG;
    }

    AccountID const outerAccount = tx.getAccountID(sfAccount);

    auto const& txns = tx.getFieldArray(sfRawTransactions);
    if (txns.empty())
    {
        JLOG(ctx.j.debug()) << "Batch: txns array empty.";
        return temMALFORMED;
    }

    if (txns.size() > 8)
    {
        JLOG(ctx.j.debug()) << "Batch: txns array exceeds 12 entries.";
        return temMALFORMED;
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
            JLOG(ctx.j.debug()) << "Batch: invalid batch txn signature.";
            return temBAD_SIGNATURE;
        }
    }

    for (STObject txn : txns)
    {
        if (!txn.isFieldPresent(sfTransactionType))
        {
            JLOG(ctx.j.debug())
                << "Batch: TransactionType missing in array entry.";
            return temMALFORMED;
        }
        if (txn.getFieldU16(sfTransactionType) == ttBATCH)
        {
            JLOG(ctx.j.debug()) << "Batch: batch cannot have inner batch txn.";
            return temMALFORMED;
        }

        if (auto const innerAccount = txn.getAccountID(sfAccount);
            innerAccount != outerAccount)
        {
            if (tx.getFieldArray(sfBatchSigners).end() ==
                std::find_if(
                    tx.getFieldArray(sfBatchSigners).begin(),
                    tx.getFieldArray(sfBatchSigners).end(),
                    [innerAccount](STObject const& signer) {
                        return signer.getAccountID(sfAccount) == innerAccount;
                    }))
            {
                JLOG(ctx.j.debug())
                    << "Batch: inner txn not signed by the right user.";
                return temBAD_SIGNER;
            }
        }
    }
    return preflight2(ctx);
}

TER
Batch::preclaim(PreclaimContext const& ctx)
{
    if (!ctx.view.rules().enabled(featureBatch))
        return temDISABLED;

    return tesSUCCESS;
}

TER
Batch::doApply()
{
    Sandbox sb(&ctx_.view());
    bool changed = false;
    auto const flags = ctx_.tx.getFlags();

    TER result = tesSUCCESS;
    ApplyViewImpl& avi = dynamic_cast<ApplyViewImpl&>(ctx_.view());
    OpenView subView(&sb);
    std::map<AccountID, std::uint32_t> accountCount;

    auto const& txns = ctx_.tx.getFieldArray(sfRawTransactions);
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
        meta.setFieldU8(sfTransactionResult, TERtoInt(ter));
        meta.setFieldU16(sfTransactionType, stx.getTxnType());
        if (ter == tesSUCCESS || isTecClaim(ter))
            meta.setFieldH256(sfTransactionHash, stx.getTransactionID());
        avi.addBatchExecutionMetaData(std::move(meta));

        // Update Account:Count Map
        accountCount[stx.getAccountID(sfAccount)] += 1;

        if (ter != tesSUCCESS)
        {
            // Atomic Revert on non tec failure
            if (!isTecClaim(ter))
            {
                accountCount.clear();
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
                accountCount.clear();
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

    // Apply SubView
    if (changed)
    {
        ctx_.applyOpenView(subView);
    }

    // Clean Up
    // 1. Update the account_ sfSequence to include any tes/tec inner txns

    // Reason: The sequence (1) is consumed before the inner batch txns
    // however we dont know how many of the inner txns will succeed depending
    // on the batch type. (This could be moved to `getSeqProxy()`)

    // 2. Set the batch prevFields so they are included in metadata

    // Reason: When the outer batch is applied (at the end), the Sequence and
    // Balance have already been updated, therefore there are no PreviousFields
    // when adding the metadata. This adds them.
    {
        auto const sleSrcAcc = sb.peek(keylet::account(account_));
        if (!sleSrcAcc)
            return tefINTERNAL;

        STAmount const txFee = ctx_.tx.getFieldAmount(sfFee);
        std::uint32_t const txSeq = ctx_.tx.getFieldU32(sfSequence);
        STAmount const accBal = sleSrcAcc->getFieldAmount(sfBalance);
        std::uint32_t const accSeq = sleSrcAcc->getFieldU32(sfSequence);

        // only update if the account_ batch has tes/tec inner txns
        uint32_t const count = accountCount[account_];
        if (count != 0)
        {
            sleSrcAcc->setFieldU32(sfSequence, accSeq + count);
            sb.update(sleSrcAcc);
        }

        // update the batch prev fields
        STObject prevFields{sfPreviousFields};
        prevFields.setFieldU32(sfSequence, txSeq);
        prevFields.setFieldAmount(sfBalance, accBal + txFee);
        avi.addBatchPrevMetaData(std::move(prevFields));
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