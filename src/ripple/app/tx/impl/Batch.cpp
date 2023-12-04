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

#include <ripple/app/tx/applySteps.h>
#include <ripple/app/tx/impl/ApplyContext.h>
#include <ripple/app/tx/impl/Batch.h>
#include <ripple/app/tx/impl/Invoke.h>
#include <ripple/basics/Log.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>

namespace ripple {

PreclaimContext
makePreclaimTx(
    PreclaimContext const& ctx,
    TxType const& tt,
    STObject const& txn)
{
    auto const stx = STTx(tt, [&txn](STObject& obj) { obj = std::move(txn); });
    PreclaimContext const pcctx(
        ctx.app, ctx.view, tesSUCCESS, stx, ctx.flags, ctx.j);
    return pcctx;
}

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

    auto const& txns = tx.getFieldArray(sfEmittedTxns);
    if (txns.empty())
    {
        JLOG(ctx.j.warn()) << "Batch: txns array empty.";
        return temMALFORMED;
    }

    if (txns.size() > 400)
    {
        JLOG(ctx.j.warn()) << "Batch: txns array exceeds 400 entries.";
        return temMALFORMED;
    }

    for (auto const& txn : txns)
    {
        if (!txn.isFieldPresent(sfTransactionType))
        {
            JLOG(ctx.j.warn())
                << "Batch: TransactionType missing in array entry.";
            return temMALFORMED;
        }

        auto const tt = txn.getFieldU16(sfTransactionType);
        auto const account = txn.getAccountID(sfAccount);
        std::cout << "account: " << account << "\n";
        auto const stx =
            STTx(ttINVOKE, [&txn](STObject& obj) { obj = std::move(txn); });

        auto const txBlob = strHex(stx.getSerializer().slice());
        std::cout << "txBlob: " << txBlob << "\n";

        PreflightContext const pfctx(ctx.app, stx, ctx.rules, ctx.flags, ctx.j);

        switch (tt)
        {
            case ttINVOKE:
                std::cout << "tt: "
                          << "ttINVOKE"
                          << "\n";
                // DA: Create array of responses
                Invoke::preflight(pfctx);
            default:
                std::cout << "tt: "
                          << "temUNKNOWN"
                          << "\n";
        }
    }

    return preflight2(ctx);
}

TER
Batch::preclaim(PreclaimContext const& ctx)
{
    if (!ctx.view.rules().enabled(featureHooks))
        return temDISABLED;

    auto const& txns = ctx.tx.getFieldArray(sfEmittedTxns);
    for (auto const& txn : txns)
    {
        if (!txn.isFieldPresent(sfTransactionType))
        {
            JLOG(ctx.j.warn())
                << "Batch: TransactionType missing in array entry.";
            return temMALFORMED;
        }

        auto const tt = txn.getFieldU16(sfTransactionType);

        auto const stx =
            STTx(ttINVOKE, [&txn](STObject& obj) { obj = std::move(txn); });
        PreclaimContext const pcctx(
            ctx.app, ctx.view, tesSUCCESS, stx, ctx.flags, ctx.j);

        switch (tt)
        {
            case ttINVOKE:
                std::cout << "tt: "
                          << "ttINVOKE"
                          << "\n";
                // auto const pcctx1 = makePreclaimTx(ctx, ttINVOKE, txn);
                // DA: Create array of responses
                Invoke::preclaim(pcctx);
                break;
            default:
                std::cout << "tt: "
                          << "temUNKNOWN"
                          << "\n";
        }
    }

    return tesSUCCESS;
}

TER
Batch::doApply()
{
    auto const& txns = ctx_.tx.getFieldArray(sfEmittedTxns);
    for (auto const& txn : txns)
    {
        if (!txn.isFieldPresent(sfTransactionType))
        {
            JLOG(ctx_.journal.warn())
                << "Batch: TransactionType missing in array entry.";
            return temMALFORMED;
        }

        auto const tt = txn.getFieldU16(sfTransactionType);
        auto const stx =
            STTx(ttINVOKE, [&txn](STObject& obj) { obj = std::move(txn); });
        ApplyContext actx(
            ctx_.app,
            ctx_.base_,
            stx,
            tesSUCCESS,
            XRPAmount(1),
            view().flags(),
            ctx_.journal);
        Invoke p(actx);

        switch (tt)
        {
            case ttINVOKE:
                std::cout << "tt: "
                          << "ttINVOKE"
                          << "\n";
                // DA: Create array of responses
                p();
            default:
                std::cout << "tt: "
                          << "temUNKNOWN"
                          << "\n";
        }
    }
    return tesSUCCESS;
}

XRPAmount
Batch::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    XRPAmount extraFee{0};
    // if (tx.isFieldPresent(sfEmittedTxns))
    // {
    //     XRPAmount txFees{0};
    //     auto const& txns = tx.getFieldArray(sfEmittedTxns);
    //     for (auto const& txn : txns)
    //     {
    //         txFees += txn.isFieldPresent(sfFee) ? txn.getFieldAmount(sfFee) :
    //         XRPAmount{0};
    //     }
    //     extraFee += txFees;
    // }
    return Transactor::calculateBaseFee(view, tx) + extraFee;
}

}  // namespace ripple
