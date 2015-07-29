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
    case ttREGULAR_KEY_SET: return SetRegularKey    ::preflight(ctx);
    case ttSIGNER_LIST_SET: return SetSignerList    ::preflight(ctx);
    case ttTICKET_CANCEL:   return CancelTicket     ::preflight(ctx);
    case ttTICKET_CREATE:   return CreateTicket     ::preflight(ctx);
    case ttTRUST_SET:       return SetTrust         ::preflight(ctx);
    case ttAMENDMENT:
    case ttFEE:             return Change           ::preflight(ctx);
    default:
        return temUNKNOWN;
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
    case ttREGULAR_KEY_SET: { SetRegularKey p(ctx); return p(); }
    case ttSIGNER_LIST_SET: { SetSignerList p(ctx); return p(); }
    case ttTICKET_CANCEL:   { CancelTicket  p(ctx); return p(); }
    case ttTICKET_CREATE:   { CreateTicket  p(ctx); return p(); }
    case ttTRUST_SET:       { SetTrust      p(ctx); return p(); }
    case ttAMENDMENT:
    case ttFEE:             { Change        p(ctx); return p(); }
    default:
        return { temUNKNOWN, false };
    }
}

//------------------------------------------------------------------------------

TER
preflight (Rules const& rules, STTx const& tx,
    ApplyFlags flags, SigVerify verify,
        Config const& config, beast::Journal j)
{
    try
    {
        PreflightContext pfctx(tx,
            rules, flags, verify, config, j);
        return invoke_preflight(pfctx);
    }
    catch (std::exception const& e)
    {
        JLOG(j.fatal) <<
            "apply: " << e.what();
        return tefEXCEPTION;
    }
    catch (...)
    {
        JLOG(j.fatal) <<
            "apply: <unknown exception>";
        return tefEXCEPTION;
    }
}

std::pair<TER, bool>
doapply(OpenView& view, STTx const& tx,
    ApplyFlags flags, Config const& config,
        beast::Journal j)
{
    try
    {
        ApplyContext ctx(
            view, tx, flags, config, j);
        return invoke_apply(ctx);
    }
    catch (std::exception const& e)
    {
        JLOG(j.fatal) <<
            "apply: " << e.what();
        return { tefEXCEPTION, false };
    }
    catch (...)
    {
        JLOG(j.fatal) <<
            "apply: <unknown exception>";
        return { tefEXCEPTION, false };
    }
}

std::pair<TER, bool>
apply (OpenView& view, STTx const& tx,
    ApplyFlags flags, SigVerify verify,
        Config const& config, beast::Journal j)
{
    auto pfresult = preflight(view.rules(),
        tx, flags, verify, config, j);
    if (pfresult != tesSUCCESS)
        return { pfresult, false };
    return doapply(view, tx, flags, config, j);
}

} // ripple
