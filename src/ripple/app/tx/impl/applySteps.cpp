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
#include <ripple/app/tx/impl/Change.h>
#include <ripple/app/tx/impl/DeleteAccount.h>
#include <ripple/app/tx/impl/Payment.h>
#include <ripple/app/tx/impl/SetAccount.h>
#include <ripple/app/tx/impl/SetRegularKey.h>
#include <ripple/app/tx/impl/SetSignerList.h>

namespace ripple {

// Templates so preflight does the right thing with T::ConsequencesFactory.
//
// This could be done more easily using if constexpr, but Visual Studio
// 2017 doesn't handle if constexpr correctly.  So once we're no longer
// building with Visual Studio 2017 we can consider replacing the four
// templates with a single template function that uses if constexpr.
//
// For Transactor::Normal
template <
    class T,
    std::enable_if_t<T::ConsequencesFactory == Transactor::Normal, int> = 0>
TxConsequences
consequences_helper(PreflightContext const& ctx)
{
    return TxConsequences(ctx.tx);
};

// For Transactor::Blocker
template <
    class T,
    std::enable_if_t<T::ConsequencesFactory == Transactor::Blocker, int> = 0>
TxConsequences
consequences_helper(PreflightContext const& ctx)
{
    return TxConsequences(ctx.tx, TxConsequences::blocker);
};

// For Transactor::Custom
template <
    class T,
    std::enable_if_t<T::ConsequencesFactory == Transactor::Custom, int> = 0>
TxConsequences
consequences_helper(PreflightContext const& ctx)
{
    return T::makeTxConsequences(ctx);
};

template <class T>
std::pair<NotTEC, TxConsequences>
invoke_preflight_helper(PreflightContext const& ctx)
{
    auto const tec = T::preflight(ctx);
    return {
        tec,
        isTesSuccess(tec) ? consequences_helper<T>(ctx) : TxConsequences{tec}};
}

static std::pair<NotTEC, TxConsequences>
invoke_preflight(PreflightContext const& ctx)
{
    switch (ctx.tx.getTxnType())
    {
        case ttACCOUNT_DELETE:
            return invoke_preflight_helper<DeleteAccount>(ctx);
        case ttACCOUNT_SET:
            return invoke_preflight_helper<SetAccount>(ctx);
        case ttPAYMENT:
            return invoke_preflight_helper<Payment>(ctx);
        case ttREGULAR_KEY_SET:
            return invoke_preflight_helper<SetRegularKey>(ctx);
        case ttSIGNER_LIST_SET:
            return invoke_preflight_helper<SetSignerList>(ctx);
        case ttAMENDMENT:
        case ttFEE:
        case ttUNL_MODIFY:
            return invoke_preflight_helper<Change>(ctx);
        default:
            assert(false);
            return {temUNKNOWN, TxConsequences{temUNKNOWN}};
    }
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
        TER result = T::checkSeqProxy(ctx.view, ctx.tx, ctx.j);

        if (result != tesSUCCESS)
            return result;

        result = T::checkPriorTxAndLastLedger(ctx);

        if (result != tesSUCCESS)
            return result;

        result = T::checkFee(ctx, calculateBaseFee(ctx.view, ctx.tx));

        if (result != tesSUCCESS)
            return result;

        result = T::checkSign(ctx);

        if (result != tesSUCCESS)
            return result;
    }

    return T::preclaim(ctx);
}

static TER
invoke_preclaim(PreclaimContext const& ctx)
{
    switch (ctx.tx.getTxnType())
    {
        case ttACCOUNT_DELETE:
            return invoke_preclaim<DeleteAccount>(ctx);
        case ttACCOUNT_SET:
            return invoke_preclaim<SetAccount>(ctx);
        case ttPAYMENT:
            return invoke_preclaim<Payment>(ctx);
        case ttREGULAR_KEY_SET:
            return invoke_preclaim<SetRegularKey>(ctx);
        case ttSIGNER_LIST_SET:
            return invoke_preclaim<SetSignerList>(ctx);
        case ttAMENDMENT:
        case ttFEE:
        case ttUNL_MODIFY:
            return invoke_preclaim<Change>(ctx);
        default:
            assert(false);
            return temUNKNOWN;
    }
}

static XRPAmount
invoke_calculateBaseFee(ReadView const& view, STTx const& tx)
{
    switch (tx.getTxnType())
    {
        case ttACCOUNT_DELETE:
            return DeleteAccount::calculateBaseFee(view, tx);
        case ttACCOUNT_SET:
            return SetAccount::calculateBaseFee(view, tx);
        case ttPAYMENT:
            return Payment::calculateBaseFee(view, tx);
        case ttREGULAR_KEY_SET:
            return SetRegularKey::calculateBaseFee(view, tx);
        case ttSIGNER_LIST_SET:
            return SetSignerList::calculateBaseFee(view, tx);
        case ttAMENDMENT:
        case ttFEE:
        case ttUNL_MODIFY:
            return Change::calculateBaseFee(view, tx);
        default:
            assert(false);
            return XRPAmount{0};
    }
}

TxConsequences::TxConsequences(NotTEC pfresult)
    : isBlocker_(false)
    , fee_(beast::zero)
    , potentialSpend_(beast::zero)
    , seqProx_(SeqProxy::sequence(0))
    , sequencesConsumed_(0)
{
    assert(!isTesSuccess(pfresult));
}

TxConsequences::TxConsequences(STTx const& tx)
    : isBlocker_(false)
    , fee_(!tx[sfFee].negative() ? tx[sfFee].xrp() : beast::zero)
    , potentialSpend_(beast::zero)
    , seqProx_(tx.getSeqProxy())
    , sequencesConsumed_(tx.getSeqProxy().isSeq() ? 1 : 0)
{
}

TxConsequences::TxConsequences(STTx const& tx, Category category)
    : TxConsequences(tx)
{
    isBlocker_ = (category == blocker);
}

TxConsequences::TxConsequences(STTx const& tx, XRPAmount potentialSpend)
    : TxConsequences(tx)
{
    potentialSpend_ = potentialSpend;
}

TxConsequences::TxConsequences(STTx const& tx, std::uint32_t sequencesConsumed)
    : TxConsequences(tx)
{
    sequencesConsumed_ = sequencesConsumed;
}

static std::pair<TER, bool>
invoke_apply(ApplyContext& ctx)
{
    switch (ctx.tx.getTxnType())
    {
        case ttACCOUNT_DELETE: {
            DeleteAccount p(ctx);
            return p();
        }
        case ttACCOUNT_SET: {
            SetAccount p(ctx);
            return p();
        }
        case ttPAYMENT: {
            Payment p(ctx);
            return p();
        }
        case ttREGULAR_KEY_SET: {
            SetRegularKey p(ctx);
            return p();
        }
        case ttSIGNER_LIST_SET: {
            SetSignerList p(ctx);
            return p();
        }
        case ttAMENDMENT:
        case ttFEE:
        case ttUNL_MODIFY: {
            Change p(ctx);
            return p();
        }
        default:
            assert(false);
            return {temUNKNOWN, false};
    }
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
