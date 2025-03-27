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

#include <xrpld/app/tx/detail/MPTokenIssuanceDestroy.h>
#include <xrpld/ledger/View.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

bool
MPTokenIssuanceDestroy::isEnabled(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureMPTokensV1);
}

std::uint32_t
MPTokenIssuanceDestroy::getFlagsMask(PreflightContext const& ctx)
{
    return tfMPTokenIssuanceDestroyMask;
}

NotTEC
MPTokenIssuanceDestroy::doPreflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
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
MPTokenIssuanceDestroy::destroy(
    ApplyView& view,
    beast::Journal journal,
    MPTDestroyArgs const& args)
{
    auto const mpt = view.peek(keylet::mptIssuance(args.issuanceID));
    if (!mpt)
        return tecOBJECT_NOT_FOUND;

    if ((*mpt)[sfIssuer] != args.account)
        return tecNO_PERMISSION;
    auto const& issuer = args.account;

    if ((*mpt)[~sfOutstandingAmount] != 0)
        return tecHAS_OBLIGATIONS;

    if (!view.dirRemove(
            keylet::ownerDir(issuer), (*mpt)[sfOwnerNode], mpt->key(), false))
        return tefBAD_LEDGER;

    view.erase(mpt);
    adjustOwnerCount(view, view.peek(keylet::account(issuer)), -1, journal);
    return tesSUCCESS;
}

TER
MPTokenIssuanceDestroy::doApply()
{
    return destroy(
        view(),
        j_,
        {.account = ctx_.tx[sfAccount],
         .issuanceID = ctx_.tx[sfMPTokenIssuanceID]});
}

}  // namespace ripple
