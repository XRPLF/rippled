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

#include <xrpld/app/misc/DelegateUtils.h>
#include <xrpld/app/tx/detail/MPTokenIssuanceSet.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

bool
MPTokenIssuanceSet::isEnabled(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureMPTokensV1);
}

std::uint32_t
MPTokenIssuanceSet::getFlagsMask(PreflightContext const& ctx)
{
    return tfMPTokenIssuanceSetMask;
}

NotTEC
MPTokenIssuanceSet::preflight(PreflightContext const& ctx)
{
    auto const txFlags = ctx.tx.getFlags();

    // fails if both flags are set
    if ((txFlags & tfMPTLock) && (txFlags & tfMPTUnlock))
        return temINVALID_FLAG;

    auto const accountID = ctx.tx[sfAccount];
    auto const holderID = ctx.tx[~sfHolder];
    if (holderID && accountID == holderID)
        return temMALFORMED;

    return tesSUCCESS;
}

TER
MPTokenIssuanceSet::checkPermission(ReadView const& view, STTx const& tx)
{
    auto const delegate = tx[~sfDelegate];
    if (!delegate)
        return tesSUCCESS;

    auto const delegateKey = keylet::delegate(tx[sfAccount], *delegate);
    auto const sle = view.read(delegateKey);

    if (!sle)
        return tecNO_DELEGATE_PERMISSION;

    if (checkTxPermission(sle, tx) == tesSUCCESS)
        return tesSUCCESS;

    auto const txFlags = tx.getFlags();

    // this is added in case more flags will be added for MPTokenIssuanceSet
    // in the future. Currently unreachable.
    if (txFlags & tfMPTokenIssuanceSetPermissionMask)
        return tecNO_DELEGATE_PERMISSION;  // LCOV_EXCL_LINE

    std::unordered_set<GranularPermissionType> granularPermissions;
    loadGranularPermission(sle, ttMPTOKEN_ISSUANCE_SET, granularPermissions);

    if (txFlags & tfMPTLock &&
        !granularPermissions.contains(MPTokenIssuanceLock))
        return tecNO_DELEGATE_PERMISSION;

    if (txFlags & tfMPTUnlock &&
        !granularPermissions.contains(MPTokenIssuanceUnlock))
        return tecNO_DELEGATE_PERMISSION;

    return tesSUCCESS;
}

TER
MPTokenIssuanceSet::preclaim(PreclaimContext const& ctx)
{
    // ensure that issuance exists
    auto const sleMptIssuance =
        ctx.view.read(keylet::mptIssuance(ctx.tx[sfMPTokenIssuanceID]));
    if (!sleMptIssuance)
        return tecOBJECT_NOT_FOUND;

    // if the mpt has disabled locking
    if (!((*sleMptIssuance)[sfFlags] & lsfMPTCanLock))
        return tecNO_PERMISSION;

    // ensure it is issued by the tx submitter
    if ((*sleMptIssuance)[sfIssuer] != ctx.tx[sfAccount])
        return tecNO_PERMISSION;

    if (auto const holderID = ctx.tx[~sfHolder])
    {
        // make sure holder account exists
        if (!ctx.view.exists(keylet::account(*holderID)))
            return tecNO_DST;

        // the mptoken must exist
        if (!ctx.view.exists(
                keylet::mptoken(ctx.tx[sfMPTokenIssuanceID], *holderID)))
            return tecOBJECT_NOT_FOUND;
    }

    return tesSUCCESS;
}

TER
MPTokenIssuanceSet::doApply()
{
    auto const mptIssuanceID = ctx_.tx[sfMPTokenIssuanceID];
    auto const txFlags = ctx_.tx.getFlags();
    auto const holderID = ctx_.tx[~sfHolder];
    std::shared_ptr<SLE> sle;

    if (holderID)
        sle = view().peek(keylet::mptoken(mptIssuanceID, *holderID));
    else
        sle = view().peek(keylet::mptIssuance(mptIssuanceID));

    if (!sle)
        return tecINTERNAL;

    std::uint32_t const flagsIn = sle->getFieldU32(sfFlags);
    std::uint32_t flagsOut = flagsIn;

    if (txFlags & tfMPTLock)
        flagsOut |= lsfMPTLocked;
    else if (txFlags & tfMPTUnlock)
        flagsOut &= ~lsfMPTLocked;

    if (flagsIn != flagsOut)
        sle->setFieldU32(sfFlags, flagsOut);

    view().update(sle);

    return tesSUCCESS;
}

}  // namespace ripple
