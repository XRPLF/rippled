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
    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto& tx = ctx.tx;
    bool const isSwap = tx.isFieldPresent(sfBatchSigners);
    if (isSwap)
    {
        auto const sigResult = ctx.tx.checkBatchSign();
        if (!sigResult)
            return temBAD_SIGNER;
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

        AccountID const innerAccount = txn.getAccountID(sfAccount);
        if (!isSwap && innerAccount != outerAccount)
        {
            JLOG(ctx.j.debug()) << "Batch: batch signer mismatch.";
            return temBAD_SIGNER;
        }
    }
    return preflight2(ctx);
}

TER
Batch::preclaim(PreclaimContext const& ctx)
{
    if (!ctx.view.rules().enabled(featureBatch))
        return temDISABLED;

    auto const& txns = ctx.tx.getFieldArray(sfRawTransactions);
    for (std::size_t i = 0; i < txns.size(); ++i)
    {
        STObject txn = txns[i];
        if (!txn.isFieldPresent(sfTransactionType))
        {
            JLOG(ctx.j.debug())
                << "Batch: TransactionType missing in array entry.";
            return temMALFORMED;
        }
    }

    return tesSUCCESS;
}

TER
Batch::doApply()
{
    Sandbox sb(&ctx_.view());
    bool changed = false;

    uint32_t flags = ctx_.tx.getFlags();
    if (flags & tfBatchMask)
        return temINVALID_FLAG;

    TER result = tesSUCCESS;
    ApplyViewImpl& avi = dynamic_cast<ApplyViewImpl&>(ctx_.view());
    OpenView subView(&sb);
    std::map<AccountID, std::uint32_t> accountCount;

    auto const& txns = ctx_.tx.getFieldArray(sfRawTransactions);
    for (STObject txn : txns)
    {
        STTx const stx = STTx{std::move(txn)};
        auto const [ter, applied] = ripple::apply(
            ctx_.app, subView, stx, tapFAIL_HARD, ctx_.journal);

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
                std::vector<STObject> batch;
                avi.setHookMetaData(std::move(batch));
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

    for (auto const& [_account, count] : accountCount)
    {
        auto const sleSrcAcc = sb.peek(keylet::account(_account));
        if (!sleSrcAcc)
            return tefINTERNAL;
        
        if (_account == account_)
        {
            // Update Sequence (Source Account)
            sleSrcAcc->setFieldU32(
                sfSequence,
                sleSrcAcc->getFieldU32(sfSequence) + count);
            sb.update(sleSrcAcc);
        }
    }
    
    sb.apply(ctx_.rawView());
    
    return result;
}

XRPAmount
Batch::calculateBaseFee(ReadView const& view, STTx const& tx)
{
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
    return extraFee;
}

}  // namespace ripple