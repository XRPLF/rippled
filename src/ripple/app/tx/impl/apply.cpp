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

#include <BeastConfig.h>
#include <ripple/app/tx/apply.h>
#include <ripple/app/tx/impl/applyImpl.h>
#include <ripple/app/tx/impl/ApplyContext.h>
#include <ripple/app/tx/impl/CancelOffer.h>
#include <ripple/app/tx/impl/CancelTicket.h>
#include <ripple/app/tx/impl/Change.h>
#include <ripple/app/tx/impl/CreateOffer.h>
#include <ripple/app/tx/impl/CreateTicket.h>
#include <ripple/app/tx/impl/Payment.h>
#include <ripple/app/tx/impl/SetAccount.h>
#include <ripple/app/tx/impl/SetRegularKey.h>
#include <ripple/app/tx/impl/SetSignerList.h>
#include <ripple/app/tx/impl/SetTrust.h>
#include <ripple/app/tx/impl/SusPay.h>

namespace ripple {

static
TER
invoke_preflight (PreflightContext const& ctx)
{
    switch(ctx.tx.getTxnType())
    {
    case ttACCOUNT_SET:     return SetAccount       ::preflight(ctx);
    case ttOFFER_CANCEL:    return CancelOffer      ::preflight(ctx);
    case ttOFFER_CREATE:    return CreateOffer      ::preflight(ctx);
    case ttPAYMENT:         return Payment          ::preflight(ctx);
    case ttSUSPAY_CREATE:   return SusPayCreate     ::preflight(ctx);
    case ttSUSPAY_FINISH:   return SusPayFinish     ::preflight(ctx);
    case ttSUSPAY_CANCEL:   return SusPayCancel     ::preflight(ctx);
    case ttREGULAR_KEY_SET: return SetRegularKey    ::preflight(ctx);
    case ttSIGNER_LIST_SET: return SetSignerList    ::preflight(ctx);
    case ttTICKET_CANCEL:   return CancelTicket     ::preflight(ctx);
    case ttTICKET_CREATE:   return CreateTicket     ::preflight(ctx);
    case ttTRUST_SET:       return SetTrust         ::preflight(ctx);
    case ttAMENDMENT:
    case ttFEE:             return Change           ::preflight(ctx);
    default:
        assert(false);
        return temUNKNOWN;
    }
}

/*
    invoke_preclaim<T> uses name hiding to accomplish
    compile-time polymorphism of (presumably) static
    class functions for Transactor and derived classes.
*/
template<class T>
static
std::pair<TER, std::uint64_t>
invoke_preclaim(PreclaimContext const& ctx)
{
    // If the transactor requires a valid account and the transaction doesn't
    // list one, preflight will have already a flagged a failure.
    auto const id = ctx.tx.getAccountID(sfAccount);
    auto const baseFee = T::calculateBaseFee(ctx);
    TER result;
    if (id != zero)
    {
        result = T::checkSeq(ctx);

        if (result != tesSUCCESS)
            return { result, 0 };

        result = T::checkFee(ctx, baseFee);

        if (result != tesSUCCESS)
            return { result, 0 };

        result = T::checkSign(ctx);

        if (result != tesSUCCESS)
            return { result, 0 };

        result = T::preclaim(ctx);

        if (result != tesSUCCESS)
            return{ result, 0 };
    }
    else
    {
        result = tesSUCCESS;
    }

    return { tesSUCCESS, baseFee };
}

static
std::pair<TER, std::uint64_t>
invoke_preclaim (PreclaimContext const& ctx)
{
    switch(ctx.tx.getTxnType())
    {
    case ttACCOUNT_SET:     return invoke_preclaim<SetAccount>(ctx);
    case ttOFFER_CANCEL:    return invoke_preclaim<CancelOffer>(ctx);
    case ttOFFER_CREATE:    return invoke_preclaim<CreateOffer>(ctx);
    case ttPAYMENT:         return invoke_preclaim<Payment>(ctx);
    case ttSUSPAY_CREATE:   return invoke_preclaim<SusPayCreate>(ctx);
    case ttSUSPAY_FINISH:   return invoke_preclaim<SusPayFinish>(ctx);
    case ttSUSPAY_CANCEL:   return invoke_preclaim<SusPayCancel>(ctx);
    case ttREGULAR_KEY_SET: return invoke_preclaim<SetRegularKey>(ctx);
    case ttSIGNER_LIST_SET: return invoke_preclaim<SetSignerList>(ctx);
    case ttTICKET_CANCEL:   return invoke_preclaim<CancelTicket>(ctx);
    case ttTICKET_CREATE:   return invoke_preclaim<CreateTicket>(ctx);
    case ttTRUST_SET:       return invoke_preclaim<SetTrust>(ctx);
    case ttAMENDMENT:
    case ttFEE:             return invoke_preclaim<Change>(ctx);
    default:
        assert(false);
        return { temUNKNOWN, 0 };
    }
}

static
std::pair<TER, bool>
invoke_apply (ApplyContext& ctx)
{
    switch(ctx.tx.getTxnType())
    {
    case ttACCOUNT_SET:     { SetAccount    p(ctx); return p(); }
    case ttOFFER_CANCEL:    { CancelOffer   p(ctx); return p(); }
    case ttOFFER_CREATE:    { CreateOffer   p(ctx); return p(); }
    case ttPAYMENT:         { Payment       p(ctx); return p(); }
    case ttSUSPAY_CREATE:   { SusPayCreate  p(ctx); return p(); }
    case ttSUSPAY_FINISH:   { SusPayFinish  p(ctx); return p(); }
    case ttSUSPAY_CANCEL:   { SusPayCancel  p(ctx); return p(); }
    case ttREGULAR_KEY_SET: { SetRegularKey p(ctx); return p(); }
    case ttSIGNER_LIST_SET: { SetSignerList p(ctx); return p(); }
    case ttTICKET_CANCEL:   { CancelTicket  p(ctx); return p(); }
    case ttTICKET_CREATE:   { CreateTicket  p(ctx); return p(); }
    case ttTRUST_SET:       { SetTrust      p(ctx); return p(); }
    case ttAMENDMENT:
    case ttFEE:             { Change        p(ctx); return p(); }
    default:
        assert(false);
        return { temUNKNOWN, false };
    }
}

//------------------------------------------------------------------------------

PreflightResult
preflight (Rules const& rules, STTx const& tx,
    ApplyFlags flags, SigVerify verify,
        Config const& config, beast::Journal j)
{
    PreflightContext const pfctx(tx,
        rules, flags, verify, config, j);
    try
    {
        return{ pfctx, invoke_preflight(pfctx) };
    }
    catch (std::exception const& e)
    {
        JLOG(j.fatal) <<
            "apply: " << e.what();
        return{ pfctx, tefEXCEPTION };
    }
    catch (...)
    {
        JLOG(j.fatal) <<
            "apply: <unknown exception>";
        return{ pfctx, tefEXCEPTION };
    }
}

PreclaimResult
preclaim (PreflightResult const& preflightResult,
    Application& app, OpenView const& view)
{
    boost::optional<PreclaimContext const> ctx;
    if (preflightResult.ctx.rules != view.rules())
    {
        auto secondFlight = preflight(view.rules(),
            preflightResult.ctx.tx, preflightResult.ctx.flags,
                preflightResult.ctx.verify, preflightResult.ctx.config,
                    preflightResult.ctx.j);
        ctx.emplace(app, view, secondFlight.ter, secondFlight.ctx.tx,
            secondFlight.ctx.flags, secondFlight.ctx.j);
    }
    else
    {
        ctx.emplace(
            app, view, preflightResult.ter, preflightResult.ctx.tx,
                preflightResult.ctx.flags, preflightResult.ctx.j);
    }
    try
    {
        if (ctx->preflightResult != tesSUCCESS)
            return { *ctx, ctx->preflightResult, 0 };
        return{ *ctx, invoke_preclaim(*ctx) };
    }
    catch (std::exception const& e)
    {
        JLOG(ctx->j.fatal) <<
            "apply: " << e.what();
        return{ *ctx, tefEXCEPTION, 0 };
    }
    catch (...)
    {
        JLOG(ctx->j.fatal) <<
            "apply: <unknown exception>";
        return{ *ctx, tefEXCEPTION, 0 };
    }
}

std::pair<TER, bool>
doApply(PreclaimResult const& preclaimResult,
    Application& app, OpenView& view)
{
    if (preclaimResult.ctx.view.seq() != view.seq())
    {
        // Logic error from the caller. Don't have enough
        // info to recover.
        return{ tefEXCEPTION, false };
    }
    try
    {
        if (preclaimResult.ter != tesSUCCESS
                && !isTecClaim(preclaimResult.ter))
            return{ preclaimResult.ter, false };
        ApplyContext ctx(
            app, view, preclaimResult.ctx.tx, preclaimResult.ter,
            preclaimResult.baseFee, preclaimResult.ctx.flags,
            preclaimResult.ctx.j);
        return invoke_apply(ctx);
    }
    catch (std::exception const& e)
    {
        JLOG(preclaimResult.ctx.j.fatal) <<
            "apply: " << e.what();
        return { tefEXCEPTION, false };
    }
    catch (...)
    {
        JLOG(preclaimResult.ctx.j.fatal) <<
            "apply: <unknown exception>";
        return { tefEXCEPTION, false };
    }
}

std::pair<TER, bool>
apply (Application& app, OpenView& view,
    STTx const& tx, ApplyFlags flags,
        SigVerify verify, Config const& config,
            beast::Journal j)
{
    auto pfresult = preflight(view.rules(),
        tx, flags, verify, config, j);
    auto pcresult = preclaim(pfresult, app, view);
    return doApply(pcresult, app, view);
}

} // ripple
