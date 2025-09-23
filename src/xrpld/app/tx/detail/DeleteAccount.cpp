//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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

#include <xrpld/app/misc/DeleteUtils.h>
#include <xrpld/app/tx/detail/DeleteAccount.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/mulDiv.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/CredentialHelpers.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>

namespace ripple {

NotTEC
DeleteAccount::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureDeletableAccounts))
        return temDISABLED;

    if (ctx.tx.isFieldPresent(sfCredentialIDs) &&
        !ctx.rules.enabled(featureCredentials))
        return temDISABLED;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx[sfAccount] == ctx.tx[sfDestination])
        // An account cannot be deleted and give itself the resulting XRP.
        return temDST_IS_SRC;

    if (auto const err = credentials::checkFields(ctx.tx, ctx.j);
        !isTesSuccess(err))
        return err;

    return preflight2(ctx);
}

XRPAmount
DeleteAccount::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // The fee required for AccountDelete is one owner reserve.
    return view.fees().increment;
}

TER
DeleteAccount::preclaim(PreclaimContext const& ctx)
{
    AccountID const account{ctx.tx[sfAccount]};
    AccountID const dest{ctx.tx[sfDestination]};

    if (auto const res = deletePreclaim(ctx, 255, account, dest);
        !isTesSuccess(res))
        return res;
    return tesSUCCESS;
}

TER
DeleteAccount::doApply()
{
    AccountID const account{ctx_.tx[sfAccount]};
    AccountID const dest{ctx_.tx[sfDestination]};
    if (auto const res = deleteDoApply(ctx_, mSourceBalance, account, dest);
        !isTesSuccess(res))
        return res;
    return tesSUCCESS;
}

}  // namespace ripple
