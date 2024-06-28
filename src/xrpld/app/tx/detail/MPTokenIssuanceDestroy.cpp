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

#include <xrpld/app/tx/detail/MPTokenIssuanceDestroy.h>
#include <xrpld/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/st.h>

namespace ripple {

NotTEC
MPTokenIssuanceDestroy::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureMPTokensV1))
        return temDISABLED;

    // check flags
    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfMPTokenIssuanceDestroyMask)
        return temINVALID_FLAG;

    return preflight2(ctx);
}

TER
MPTokenIssuanceDestroy::preclaim(PreclaimContext const& ctx)
{
    // ensure that issuance exists
    auto const sleMPT =
        ctx.view.read(keylet::mptIssuance(ctx.tx[sfMPTokenIssuanceID]));
    if (!sleMPT)
        return tecOBJECT_NOT_FOUND;

    // ensure it is issued by the tx submitter
    if ((*sleMPT)[sfIssuer] != ctx.tx[sfAccount])
        return tecNO_PERMISSION;

    // ensure it has no outstanding balances
    if ((*sleMPT)[~sfOutstandingAmount] != 0)
        return tecHAS_OBLIGATIONS;

    return tesSUCCESS;
}

TER
MPTokenIssuanceDestroy::doApply()
{
    auto const mpt =
        view().peek(keylet::mptIssuance(ctx_.tx[sfMPTokenIssuanceID]));
    auto const issuer = (*mpt)[sfIssuer];

    if (!view().dirRemove(
            keylet::ownerDir(issuer), (*mpt)[sfOwnerNode], mpt->key(), false))
        return tefBAD_LEDGER;

    view().erase(mpt);

    adjustOwnerCount(view(), view().peek(keylet::account(issuer)), -1, j_);

    return tesSUCCESS;
}

}  // namespace ripple