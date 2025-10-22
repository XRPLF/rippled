//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2025 Ripple Labs Inc.

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

#include <xrpld/app/tx/detail/ConfidentialMergeInbox.h>

#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
ConfidentialMergeInbox::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureConfidentialTransfer))
        return temDISABLED;

    // issuer cannot merge
    if (MPTIssue(ctx.tx[sfMPTokenIssuanceID]).getIssuer() == ctx.tx[sfAccount])
        return temMALFORMED;

    return tesSUCCESS;
}

TER
ConfidentialMergeInbox::preclaim(PreclaimContext const& ctx)
{
    auto const sleMptoken = ctx.view.read(
        keylet::mptoken(ctx.tx[sfMPTokenIssuanceID], ctx.tx[sfAccount]));
    if (!sleMptoken)
        return tecOBJECT_NOT_FOUND;

    if (!sleMptoken->isFieldPresent(sfConfidentialBalanceInbox) ||
        !sleMptoken->isFieldPresent(sfConfidentialBalanceSpending))
        return tecNO_PERMISSION;

    return tesSUCCESS;
}

TER
ConfidentialMergeInbox::doApply()
{
    auto const mptIssuanceID = ctx_.tx[sfMPTokenIssuanceID];
    auto sleMptoken = view().peek(keylet::mptoken(mptIssuanceID, account_));
    if (!sleMptoken)
        return tecINTERNAL;

    // homomorphically add holder's encrypted balance
    Buffer sum(ecGamalEncryptedTotalLength);
    if (TER const ter = homomorphicAdd(
            (*sleMptoken)[sfConfidentialBalanceSpending],
            (*sleMptoken)[sfConfidentialBalanceInbox],
            sum);
        !isTesSuccess(ter))
        return tecINTERNAL;

    (*sleMptoken)[sfConfidentialBalanceSpending] = sum;

    Buffer zeroEncyption;
    zeroEncyption = encryptCanonicalZeroAmount(
        (*sleMptoken)[sfHolderElGamalPublicKey], account_, mptIssuanceID);
    (*sleMptoken)[sfConfidentialBalanceInbox] = zeroEncyption;

    // it's fine if it reaches max uint32, it just resets to 0
    (*sleMptoken)[sfConfidentialBalanceVersion] =
        (*sleMptoken)[~sfConfidentialBalanceVersion].value_or(0u) + 1u;

    view().update(sleMptoken);
    return tesSUCCESS;
}

}  // namespace ripple
