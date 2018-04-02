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
#include <ripple/app/tx/impl/CancelCheck.h>
#include <ripple/app/tx/impl/CancelOffer.h>
#include <ripple/app/tx/impl/CancelTicket.h>
#include <ripple/app/tx/impl/CashCheck.h>
#include <ripple/app/tx/impl/Change.h>
#include <ripple/app/tx/impl/CreateCheck.h>
#include <ripple/app/tx/impl/CreateOffer.h>
#include <ripple/app/tx/impl/CreateTicket.h>
#include <ripple/app/tx/impl/Escrow.h>
#include <ripple/app/tx/impl/Payment.h>
#include <ripple/app/tx/impl/SetAccount.h>
#include <ripple/app/tx/impl/SetRegularKey.h>
#include <ripple/app/tx/impl/SetSignerList.h>
#include <ripple/app/tx/impl/SetTrust.h>
#include <ripple/app/tx/impl/PayChan.h>

namespace ripple {

static
TER
invoke_preflight (PreflightContext const& ctx)
{
    switch(ctx.tx.getTxnType())
    {
    case ttACCOUNT_SET:     return SetAccount       ::preflight(ctx);
    case ttCHECK_CANCEL:    return CancelCheck      ::preflight(ctx);
    case ttCHECK_CASH:      return CashCheck        ::preflight(ctx);
    case ttCHECK_CREATE:    return CreateCheck      ::preflight(ctx);
    case ttOFFER_CANCEL:    return CancelOffer      ::preflight(ctx);
    case ttOFFER_CREATE:    return CreateOffer      ::preflight(ctx);
    case ttESCROW_CREATE:   return EscrowCreate     ::preflight(ctx);
    case ttESCROW_FINISH:   return EscrowFinish     ::preflight(ctx);
    case ttESCROW_CANCEL:   return EscrowCancel     ::preflight(ctx);
    case ttPAYCHAN_CLAIM:   return PayChanClaim     ::preflight(ctx);
    case ttPAYCHAN_CREATE:  return PayChanCreate    ::preflight(ctx);
    case ttPAYCHAN_FUND:    return PayChanFund      ::preflight(ctx);
    case ttPAYMENT:         return Payment          ::preflight(ctx);
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

/* invoke_preclaim<T> uses name hiding to accomplish
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

    if (id != zero)
    {
        TER result = T::checkSeq(ctx);

        if (result != tesSUCCESS)
            return { result, baseFee };

        result = T::checkFee(ctx, baseFee);

        if (result != tesSUCCESS)
            return { result, baseFee };

        result = T::checkSign(ctx);

        if (result != tesSUCCESS)
            return { result, baseFee };

    }

    return{ T::preclaim(ctx), baseFee };
}

static
std::pair<TER, std::uint64_t>
invoke_preclaim (PreclaimContext const& ctx)
{
    switch(ctx.tx.getTxnType())
    {
    case ttACCOUNT_SET:     return invoke_preclaim<SetAccount>(ctx);
    case ttCHECK_CANCEL:    return invoke_preclaim<CancelCheck>(ctx);
    case ttCHECK_CASH:      return invoke_preclaim<CashCheck>(ctx);
    case ttCHECK_CREATE:    return invoke_preclaim<CreateCheck>(ctx);
    case ttOFFER_CANCEL:    return invoke_preclaim<CancelOffer>(ctx);
    case ttOFFER_CREATE:    return invoke_preclaim<CreateOffer>(ctx);
    case ttESCROW_CREATE:   return invoke_preclaim<EscrowCreate>(ctx);
    case ttESCROW_FINISH:   return invoke_preclaim<EscrowFinish>(ctx);
    case ttESCROW_CANCEL:   return invoke_preclaim<EscrowCancel>(ctx);
    case ttPAYCHAN_CLAIM:   return invoke_preclaim<PayChanClaim>(ctx);
    case ttPAYCHAN_CREATE:  return invoke_preclaim<PayChanCreate>(ctx);
    case ttPAYCHAN_FUND:    return invoke_preclaim<PayChanFund>(ctx);
    case ttPAYMENT:         return invoke_preclaim<Payment>(ctx);
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
std::uint64_t
invoke_calculateBaseFee(PreclaimContext const& ctx)
{
    switch (ctx.tx.getTxnType())
    {
    case ttACCOUNT_SET:     return SetAccount::calculateBaseFee(ctx);
    case ttCHECK_CANCEL:    return CancelCheck::calculateBaseFee(ctx);
    case ttCHECK_CASH:      return CashCheck::calculateBaseFee(ctx);
    case ttCHECK_CREATE:    return CreateCheck::calculateBaseFee(ctx);
    case ttOFFER_CANCEL:    return CancelOffer::calculateBaseFee(ctx);
    case ttOFFER_CREATE:    return CreateOffer::calculateBaseFee(ctx);
    case ttESCROW_CREATE:   return EscrowCreate::calculateBaseFee(ctx);
    case ttESCROW_FINISH:   return EscrowFinish::calculateBaseFee(ctx);
    case ttESCROW_CANCEL:   return EscrowCancel::calculateBaseFee(ctx);
    case ttPAYCHAN_CLAIM:   return PayChanClaim::calculateBaseFee(ctx);
    case ttPAYCHAN_CREATE:  return PayChanCreate::calculateBaseFee(ctx);
    case ttPAYCHAN_FUND:    return PayChanFund::calculateBaseFee(ctx);
    case ttPAYMENT:         return Payment::calculateBaseFee(ctx);
    case ttREGULAR_KEY_SET: return SetRegularKey::calculateBaseFee(ctx);
    case ttSIGNER_LIST_SET: return SetSignerList::calculateBaseFee(ctx);
    case ttTICKET_CANCEL:   return CancelTicket::calculateBaseFee(ctx);
    case ttTICKET_CREATE:   return CreateTicket::calculateBaseFee(ctx);
    case ttTRUST_SET:       return SetTrust::calculateBaseFee(ctx);
    case ttAMENDMENT:
    case ttFEE:             return Change::calculateBaseFee(ctx);
    default:
        assert(false);
        return 0;
    }
}

template<class T>
static
TxConsequences
invoke_calculateConsequences(STTx const& tx)
{
    auto const category = T::affectsSubsequentTransactionAuth(tx) ?
        TxConsequences::blocker : TxConsequences::normal;
    auto const feePaid = T::calculateFeePaid(tx);
    auto const maxSpend = T::calculateMaxSpend(tx);

    return{ category, feePaid, maxSpend };
}

static
TxConsequences
invoke_calculateConsequences(STTx const& tx)
{
    switch (tx.getTxnType())
    {
    case ttACCOUNT_SET:     return invoke_calculateConsequences<SetAccount>(tx);
    case ttCHECK_CANCEL:    return invoke_calculateConsequences<CancelCheck>(tx);
    case ttCHECK_CASH:      return invoke_calculateConsequences<CashCheck>(tx);
    case ttCHECK_CREATE:    return invoke_calculateConsequences<CreateCheck>(tx);
    case ttOFFER_CANCEL:    return invoke_calculateConsequences<CancelOffer>(tx);
    case ttOFFER_CREATE:    return invoke_calculateConsequences<CreateOffer>(tx);
    case ttESCROW_CREATE:   return invoke_calculateConsequences<EscrowCreate>(tx);
    case ttESCROW_FINISH:   return invoke_calculateConsequences<EscrowFinish>(tx);
    case ttESCROW_CANCEL:   return invoke_calculateConsequences<EscrowCancel>(tx);
    case ttPAYCHAN_CLAIM:   return invoke_calculateConsequences<PayChanClaim>(tx);
    case ttPAYCHAN_CREATE:  return invoke_calculateConsequences<PayChanCreate>(tx);
    case ttPAYCHAN_FUND:    return invoke_calculateConsequences<PayChanFund>(tx);
    case ttPAYMENT:         return invoke_calculateConsequences<Payment>(tx);
    case ttREGULAR_KEY_SET: return invoke_calculateConsequences<SetRegularKey>(tx);
    case ttSIGNER_LIST_SET: return invoke_calculateConsequences<SetSignerList>(tx);
    case ttTICKET_CANCEL:   return invoke_calculateConsequences<CancelTicket>(tx);
    case ttTICKET_CREATE:   return invoke_calculateConsequences<CreateTicket>(tx);
    case ttTRUST_SET:       return invoke_calculateConsequences<SetTrust>(tx);
    case ttAMENDMENT:
    case ttFEE:
        // fall through to default
    default:
        assert(false);
        return { TxConsequences::blocker, Transactor::calculateFeePaid(tx),
            beast::zero };
    }
}

static
std::pair<TER, bool>
invoke_apply (ApplyContext& ctx)
{
    switch(ctx.tx.getTxnType())
    {
    case ttACCOUNT_SET:     { SetAccount    p(ctx); return p(); }
    case ttCHECK_CANCEL:    { CancelCheck   p(ctx); return p(); }
    case ttCHECK_CASH:      { CashCheck     p(ctx); return p(); }
    case ttCHECK_CREATE:    { CreateCheck   p(ctx); return p(); }
    case ttOFFER_CANCEL:    { CancelOffer   p(ctx); return p(); }
    case ttOFFER_CREATE:    { CreateOffer   p(ctx); return p(); }
    case ttESCROW_CREATE:   { EscrowCreate  p(ctx); return p(); }
    case ttESCROW_FINISH:   { EscrowFinish  p(ctx); return p(); }
    case ttESCROW_CANCEL:   { EscrowCancel  p(ctx); return p(); }
    case ttPAYCHAN_CLAIM:   { PayChanClaim  p(ctx); return p(); }
    case ttPAYCHAN_CREATE:  { PayChanCreate p(ctx); return p(); }
    case ttPAYCHAN_FUND:    { PayChanFund   p(ctx); return p(); }
    case ttPAYMENT:         { Payment       p(ctx); return p(); }
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

PreflightResult
preflight(Application& app, Rules const& rules,
    STTx const& tx, ApplyFlags flags,
        beast::Journal j)
{
    PreflightContext const pfctx(app, tx,
        rules, flags, j);
    try
    {
        return{ pfctx, invoke_preflight(pfctx) };
    }
    catch (std::exception const& e)
    {
        JLOG(j.fatal()) <<
            "apply: " << e.what();
        return{ pfctx, tefEXCEPTION };
    }
}

PreclaimResult
preclaim (PreflightResult const& preflightResult,
    Application& app, OpenView const& view)
{
    boost::optional<PreclaimContext const> ctx;
    if (preflightResult.rules != view.rules())
    {
        auto secondFlight = preflight(app, view.rules(),
            preflightResult.tx, preflightResult.flags,
                preflightResult.j);
        ctx.emplace(app, view, secondFlight.ter, secondFlight.tx,
            secondFlight.flags, secondFlight.j);
    }
    else
    {
        ctx.emplace(
            app, view, preflightResult.ter, preflightResult.tx,
                preflightResult.flags, preflightResult.j);
    }
    try
    {
        if (ctx->preflightResult != tesSUCCESS)
            return { *ctx, ctx->preflightResult, 0 };
        return{ *ctx, invoke_preclaim(*ctx) };
    }
    catch (std::exception const& e)
    {
        JLOG(ctx->j.fatal()) <<
            "apply: " << e.what();
        return{ *ctx, tefEXCEPTION, 0 };
    }
}

std::uint64_t
calculateBaseFee(Application& app, ReadView const& view,
    STTx const& tx, beast::Journal j)
{
    PreclaimContext const ctx(
        app, view, tesSUCCESS, tx,
            tapNONE, j);

    return invoke_calculateBaseFee(ctx);
}

TxConsequences
calculateConsequences(PreflightResult const& preflightResult)
{
    assert(preflightResult.ter == tesSUCCESS);
    if (preflightResult.ter != tesSUCCESS)
        return{ TxConsequences::blocker,
            Transactor::calculateFeePaid(preflightResult.tx),
                beast::zero };
    return invoke_calculateConsequences(preflightResult.tx);
}

std::pair<TER, bool>
doApply(PreclaimResult const& preclaimResult,
    Application& app, OpenView& view)
{
    if (preclaimResult.view.seq() != view.seq())
    {
        // Logic error from the caller. Don't have enough
        // info to recover.
        return{ tefEXCEPTION, false };
    }
    try
    {
        if (!preclaimResult.likelyToClaimFee)
            return{ preclaimResult.ter, false };
        ApplyContext ctx(app, view,
            preclaimResult.tx, preclaimResult.ter,
                preclaimResult.baseFee, preclaimResult.flags,
                    preclaimResult.j);
        return invoke_apply(ctx);
    }
    catch (std::exception const& e)
    {
        JLOG(preclaimResult.j.fatal()) <<
            "apply: " << e.what();
        return { tefEXCEPTION, false };
    }
}

} // ripple
