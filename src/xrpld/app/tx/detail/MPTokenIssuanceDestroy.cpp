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

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

std::uint32_t
MPTokenIssuanceDestroy::getFlagsMask(PreflightContext const& ctx)
{
    return tfMPTokenIssuanceDestroyMask;
}

NotTEC
MPTokenIssuanceDestroy::preflight(PreflightContext const& ctx)
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
    if ((*sleMPT)[sfOutstandingAmount] != 0)
        return tecHAS_OBLIGATIONS;

    if ((*sleMPT)[~sfLockedAmount].value_or(0) != 0)
        return tecHAS_OBLIGATIONS;  // LCOV_EXCL_LINE

    return tesSUCCESS;
}

TER
MPTokenIssuanceDestroy::doApply()
{
    auto const mpt =
        view().peek(keylet::mptIssuance(ctx_.tx[sfMPTokenIssuanceID]));
    if (account_ != mpt->getAccountID(sfIssuer))
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (!view().dirRemove(
            keylet::ownerDir(account_), (*mpt)[sfOwnerNode], mpt->key(), false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    view().erase(mpt);

    adjustOwnerCount(view(), view().peek(keylet::account(account_)), -1, j_);

    return tesSUCCESS;
}

}  // namespace ripple
