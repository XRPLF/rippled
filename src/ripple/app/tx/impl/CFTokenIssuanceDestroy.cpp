//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2023 Ripple Labs Inc.

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

#include <ripple/app/tx/impl/CFTokenIssuanceDestroy.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/st.h>

namespace ripple {

NotTEC
CFTokenIssuanceDestroy::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureCFTokensV1))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    return preflight2(ctx);
}

TER
CFTokenIssuanceDestroy::preclaim(PreclaimContext const& ctx)
{
    // ensure that issuance exists
    auto const sleCFT =
        ctx.view.read(keylet::cftIssuance(ctx.tx[sfCFTokenIssuanceID]));
    if (!sleCFT)
        return tecOBJECT_NOT_FOUND;

    // ensure it is issued by the tx submitter
    if ((*sleCFT)[sfIssuer] != ctx.tx[sfAccount])
        return tecNO_PERMISSION;

    // ensure it has no outstanding balances
    if ((*sleCFT)[~sfOutstandingAmount] != 0)
        return tecHAS_OBLIGATIONS;

    return tesSUCCESS;
}

TER
CFTokenIssuanceDestroy::doApply()
{
    auto const cft =
        view().peek(keylet::cftIssuance(ctx_.tx[sfCFTokenIssuanceID]));
    auto const issuer = (*cft)[sfIssuer];

    if (!view().dirRemove(
            keylet::ownerDir(issuer), (*cft)[sfOwnerNode], cft->key(), false))
        return tefBAD_LEDGER;

    view().erase(cft);

    adjustOwnerCount(
        view(),
        view().peek(keylet::account(issuer)),
        -1,
        beast::Journal{beast::Journal::getNullSink()});

    return tesSUCCESS;
}

}  // namespace ripple
