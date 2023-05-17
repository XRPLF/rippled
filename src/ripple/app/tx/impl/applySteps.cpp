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
#include <ripple/app/tx/impl/ApplyHandler.h>
#include <ripple/app/tx/impl/Change.h>
#include <ripple/app/tx/impl/DeleteAccount.h>
#include <ripple/app/tx/impl/Payment.h>
#include <ripple/app/tx/impl/SetAccount.h>
#include <ripple/app/tx/impl/SetRegularKey.h>
#include <ripple/app/tx/impl/SetSignerList.h>
#include <dlfcn.h>
#include <iostream>
#include <map>

namespace ripple {

typedef NotTEC (*preflightPtr)(PreflightContext const&);
typedef TER (*preclaimPtr)(PreclaimContext const&);
typedef XRPAmount (*calculateBaseFeePtr)(ReadView const& view, STTx const& tx);
typedef TER (*doApplyPtr)(
    ApplyContext& ctx,
    XRPAmount mPriorBalance,
    XRPAmount mSourceBalance);

struct TransactorWrapper
{
    preflightPtr preflight;
    preclaimPtr preclaim;
    calculateBaseFeePtr calculateBaseFee;
    doApplyPtr doApply;
};

std::pair<TER, bool>
doApply_helper(ApplyContext& ctx, doApplyPtr doApplyFn)
{
    ApplyHandler p(ctx, doApplyFn);
    return p();
}

template <class T>
TransactorWrapper
transactor_helper()
{
    return {
        T::preflight,
        T::preclaim,
        T::calculateBaseFee,
        T::doApply,
    };
};

TransactorWrapper
transactor_helper(std::string pathToLib)
{
    void* handle = dlopen(pathToLib.c_str(), RTLD_LAZY);
    return {
        (preflightPtr)dlsym(handle, "preflight"),
        (preclaimPtr)dlsym(handle, "preclaim"),
        (calculateBaseFeePtr)dlsym(handle, "calculateBaseFee"),
        (doApplyPtr)dlsym(handle, "doApply"),
    };
};

std::map<std::uint16_t, TransactorWrapper> transactorMap{
    {0, transactor_helper<Payment>()},
    {3, transactor_helper<SetAccount>()},
    {5, transactor_helper<SetRegularKey>()},
    {12, transactor_helper<SetSignerList>()},
    {21, transactor_helper<DeleteAccount>()},
    {100, transactor_helper<Change>()},
    {101, transactor_helper<Change>()},
    {102, transactor_helper<Change>()},
};

void
addToTransactorMap(std::uint16_t type, std::string dynamicLib)
{
    transactorMap.insert({type, transactor_helper(dynamicLib)});
}

TxConsequences
consequences_helper(PreflightContext const& ctx)
{
    // TODO: add support for Blocker and Custom TxConsequences values
    return TxConsequences(ctx.tx);
}

static std::pair<NotTEC, TxConsequences>
invoke_preflight(PreflightContext const& ctx)
{
    if (auto it = transactorMap.find(ctx.tx.getTxnType());
        it != transactorMap.end())
    {
        auto const tec = it->second.preflight(ctx);
        return {
            tec,
            isTesSuccess(tec) ? consequences_helper(ctx) : TxConsequences{tec}};
    }
    assert(false);
    return {temUNKNOWN, TxConsequences{temUNKNOWN}};
}

/* invoke_preclaim<T> uses name hiding to accomplish
    compile-time polymorphism of (presumably) static
    class functions for Transactor and derived classes.
*/
template <class T>
static TER
invoke_preclaim(PreclaimContext const& ctx)
{
    // If the transactor requires a valid account and the transaction doesn't
    // list one, preflight will have already a flagged a failure.
    auto const id = ctx.tx.getAccountID(sfAccount);

    if (id != beast::zero)
    {
        TER result = Transactor::checkSeqProxy(ctx.view, ctx.tx, ctx.j);

        if (result != tesSUCCESS)
            return result;

        result = Transactor::checkPriorTxAndLastLedger(ctx);

        if (result != tesSUCCESS)
            return result;

        result = Transactor::checkFee(ctx, calculateBaseFee(ctx.view, ctx.tx));

        if (result != tesSUCCESS)
            return result;

        result = Transactor::checkSign(ctx);

        if (result != tesSUCCESS)
            return result;
    }

    return T::preclaim(ctx);
}

static TER
invoke_preclaim(PreclaimContext const& ctx)
{
    if (auto it = transactorMap.find(ctx.tx.getTxnType());
        it != transactorMap.end())
    {
        // If the transactor requires a valid account and the transaction
        // doesn't list one, preflight will have already a flagged a failure.
        auto const id = ctx.tx.getAccountID(sfAccount);

        if (id != beast::zero)
        {
            TER result = Transactor::checkSeqProxy(ctx.view, ctx.tx, ctx.j);

            if (result != tesSUCCESS)
                return result;

            result = Transactor::checkPriorTxAndLastLedger(ctx);

            if (result != tesSUCCESS)
                return result;

            result =
                Transactor::checkFee(ctx, calculateBaseFee(ctx.view, ctx.tx));

            if (result != tesSUCCESS)
                return result;

            result = Transactor::checkSign(ctx);

            if (result != tesSUCCESS)
                return result;
        }

        return it->second.preclaim(ctx);
    }
    assert(false);
    return temUNKNOWN;
}

static XRPAmount
invoke_calculateBaseFee(ReadView const& view, STTx const& tx)
{
    if (auto it = transactorMap.find(tx.getTxnType());
        it != transactorMap.end())
    {
        return it->second.calculateBaseFee(view, tx);
    }
    assert(false);
    return XRPAmount{0};
}

static std::pair<TER, bool>
invoke_apply(ApplyContext& ctx)
{
    if (auto it = transactorMap.find(ctx.tx.getTxnType());
        it != transactorMap.end())
    {
        return doApply_helper(ctx, it->second.doApply);
    }
    assert(false);
    return {temUNKNOWN, false};
}

PreflightResult
preflight(
    Application& app,
    Rules const& rules,
    STTx const& tx,
    ApplyFlags flags,
    beast::Journal j)
{
    PreflightContext const pfctx(app, tx, rules, flags, j);
    try
    {
        return {pfctx, invoke_preflight(pfctx)};
    }
    catch (std::exception const& e)
    {
        JLOG(j.fatal()) << "apply: " << e.what();
        return {pfctx, {tefEXCEPTION, TxConsequences{tx}}};
    }
}

PreclaimResult
preclaim(
    PreflightResult const& preflightResult,
    Application& app,
    OpenView const& view)
{
    std::optional<PreclaimContext const> ctx;
    if (preflightResult.rules != view.rules())
    {
        auto secondFlight = preflight(
            app,
            view.rules(),
            preflightResult.tx,
            preflightResult.flags,
            preflightResult.j);
        ctx.emplace(
            app,
            view,
            secondFlight.ter,
            secondFlight.tx,
            secondFlight.flags,
            secondFlight.j);
    }
    else
    {
        ctx.emplace(
            app,
            view,
            preflightResult.ter,
            preflightResult.tx,
            preflightResult.flags,
            preflightResult.j);
    }
    try
    {
        if (ctx->preflightResult != tesSUCCESS)
            return {*ctx, ctx->preflightResult};
        return {*ctx, invoke_preclaim(*ctx)};
    }
    catch (std::exception const& e)
    {
        JLOG(ctx->j.fatal()) << "apply: " << e.what();
        return {*ctx, tefEXCEPTION};
    }
}

XRPAmount
calculateBaseFee(ReadView const& view, STTx const& tx)
{
    return invoke_calculateBaseFee(view, tx);
}

XRPAmount
calculateDefaultBaseFee(ReadView const& view, STTx const& tx)
{
    return Transactor::calculateBaseFee(view, tx);
}

std::pair<TER, bool>
doApply(PreclaimResult const& preclaimResult, Application& app, OpenView& view)
{
    if (preclaimResult.view.seq() != view.seq())
    {
        // Logic error from the caller. Don't have enough
        // info to recover.
        return {tefEXCEPTION, false};
    }
    try
    {
        if (!preclaimResult.likelyToClaimFee)
            return {preclaimResult.ter, false};
        ApplyContext ctx(
            app,
            view,
            preclaimResult.tx,
            preclaimResult.ter,
            calculateBaseFee(view, preclaimResult.tx),
            preclaimResult.flags,
            preclaimResult.j);
        return invoke_apply(ctx);
    }
    catch (std::exception const& e)
    {
        JLOG(preclaimResult.j.fatal()) << "apply: " << e.what();
        return {tefEXCEPTION, false};
    }
}

}  // namespace ripple
