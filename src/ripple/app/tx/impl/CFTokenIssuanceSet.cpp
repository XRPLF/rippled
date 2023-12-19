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

#include <ripple/app/tx/impl/CFTokenIssuanceSet.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/st.h>

namespace ripple {

NotTEC
CFTokenIssuanceSet::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureCFTokensV1))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto const txFlags = ctx.tx.getFlags();

    // check flags
    if (txFlags & tfCFTokenIssuanceSetMask)
        return temINVALID_FLAG;
    // fails if both flags are set
    else if ((txFlags & tfCFTLock) && (txFlags & tfCFTUnlock))
        return temINVALID_FLAG;

    auto const accountID = ctx.tx[sfAccount];
    auto const holderID = ctx.tx[~sfCFTokenHolder];
    if (holderID && accountID == holderID)
        return temMALFORMED;

    return preflight2(ctx);
}

TER
CFTokenIssuanceSet::preclaim(PreclaimContext const& ctx)
{
    // ensure that issuance exists
    auto const sleCftIssuance =
        ctx.view.read(keylet::cftIssuance(ctx.tx[sfCFTokenIssuanceID]));
    if (!sleCftIssuance)
        return tecOBJECT_NOT_FOUND;

    // if the cft has disabled locking
    if (!((*sleCftIssuance)[sfFlags] & lsfCFTCanLock))
        return tecNO_PERMISSION;

    // ensure it is issued by the tx submitter
    if ((*sleCftIssuance)[sfIssuer] != ctx.tx[sfAccount])
        return tecNO_PERMISSION;

    if (auto const holderID = ctx.tx[~sfCFTokenHolder])
    {
        // make sure holder account exists
        if (!ctx.view.exists(keylet::account(*holderID)))
            return tecNO_DST;

        // the cftoken must exist
        if (!ctx.view.exists(
                keylet::cftoken(ctx.tx[sfCFTokenIssuanceID], *holderID)))
            return tecOBJECT_NOT_FOUND;
    }

    return tesSUCCESS;
}

TER
CFTokenIssuanceSet::doApply()
{
    auto const cftIssuanceID = ctx_.tx[sfCFTokenIssuanceID];
    auto const txFlags = ctx_.tx.getFlags();
    auto const holderID = ctx_.tx[~sfCFTokenHolder];
    std::shared_ptr<SLE> sle;

    if (holderID)
        sle = view().peek(keylet::cftoken(cftIssuanceID, *holderID));
    else
        sle = view().peek(keylet::cftIssuance(cftIssuanceID));

    if (!sle)
        return tecINTERNAL;

    std::uint32_t const flagsIn = sle->getFieldU32(sfFlags);
    std::uint32_t flagsOut = flagsIn;

    if (txFlags & tfCFTLock)
        flagsOut |= lsfCFTLocked;
    else if (txFlags & tfCFTUnlock)
        flagsOut &= ~lsfCFTLocked;

    if (flagsIn != flagsOut)
        sle->setFieldU32(sfFlags, flagsOut);

    view().update(sle);

    return tesSUCCESS;
}

}  // namespace ripple