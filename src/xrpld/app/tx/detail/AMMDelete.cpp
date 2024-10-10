//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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

#include <xrpld/app/tx/detail/AMMDelete.h>

#include <xrpld/app/misc/AMMUtils.h>
#include <xrpld/ledger/Sandbox.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
AMMDelete::preflight(PreflightContext const& ctx)
{
    if (!ammEnabled(ctx.rules))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
    {
        JLOG(ctx.j.debug()) << "AMM Delete: invalid flags.";
        return temINVALID_FLAG;
    }

    return preflight2(ctx);
}

TER
AMMDelete::preclaim(PreclaimContext const& ctx)
{
    auto const ammSle =
        ctx.view.read(keylet::amm(ctx.tx[sfAsset], ctx.tx[sfAsset2]));
    if (!ammSle)
    {
        JLOG(ctx.j.debug()) << "AMM Delete: Invalid asset pair.";
        return terNO_AMM;
    }

    auto const lpTokensBalance = (*ammSle)[sfLPTokenBalance];
    if (lpTokensBalance != beast::zero)
        return tecAMM_NOT_EMPTY;

    return tesSUCCESS;
}

TER
AMMDelete::doApply()
{
    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    Sandbox sb(&ctx_.view());

    auto const ter = deleteAMMAccount(
        sb, ctx_.tx[sfAsset].get<Issue>(), ctx_.tx[sfAsset2].get<Issue>(), j_);
    if (ter == tesSUCCESS || ter == tecINCOMPLETE)
        sb.apply(ctx_.rawView());

    return ter;
}

}  // namespace ripple
