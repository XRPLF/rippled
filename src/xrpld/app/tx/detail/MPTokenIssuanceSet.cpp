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
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
MPTokenIssuanceSet::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureMPTokensV1))
        return temDISABLED;

    if (ctx.tx.isFieldPresent(sfDomainID) &&
        !(ctx.rules.enabled(featurePermissionedDomains) &&
          ctx.rules.enabled(featureSingleAssetVault)))
        return temDISABLED;

    if (ctx.tx.isFieldPresent(sfDomainID) && ctx.tx.isFieldPresent(sfHolder))
        return temMALFORMED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto const txFlags = ctx.tx.getFlags();

    // check flags
    if (txFlags & tfMPTokenIssuanceSetMask)
        return temINVALID_FLAG;
    // fails if both flags are set
    else if ((txFlags & tfMPTLock) && (txFlags & tfMPTUnlock))
        return temINVALID_FLAG;

    auto const accountID = ctx.tx[sfAccount];
    auto const holderID = ctx.tx[~sfHolder];
    if (holderID && accountID == holderID)
        return temMALFORMED;

    if (ctx.rules.enabled(featureSingleAssetVault))
    {
        // Is this transaction actually changing anything ?
        if (txFlags == 0 && !ctx.tx.isFieldPresent(sfDomainID))
            return temMALFORMED;
    }

    return preflight2(ctx);
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

    if (!sleMptIssuance->isFlag(lsfMPTCanLock))
    {
        // For readability two separate `if` rather than `||` of two conditions
        if (!ctx.view.rules().enabled(featureSingleAssetVault))
            return tecNO_PERMISSION;
        else if (ctx.tx.isFlag(tfMPTLock) || ctx.tx.isFlag(tfMPTUnlock))
            return tecNO_PERMISSION;
    }

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

    if (auto const domain = ctx.tx[~sfDomainID])
    {
        if (not sleMptIssuance->isFlag(lsfMPTRequireAuth))
            return tecNO_PERMISSION;

        if (*domain != beast::zero)
        {
            auto const sleDomain =
                ctx.view.read(keylet::permissionedDomain(*domain));
            if (!sleDomain)
                return tecOBJECT_NOT_FOUND;
        }
    }

    return tesSUCCESS;
}

TER
MPTokenIssuanceSet::doApply()
{
    auto const mptIssuanceID = ctx_.tx[sfMPTokenIssuanceID];
    auto const txFlags = ctx_.tx.getFlags();
    auto const holderID = ctx_.tx[~sfHolder];
    auto const domainID = ctx_.tx[~sfDomainID];
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

    if (domainID)
    {
        // This is enforced in preflight.
        XRPL_ASSERT(
            sle->getType() == ltMPTOKEN_ISSUANCE,
            "MPTokenIssuanceSet::doApply : modifying MPTokenIssuance");

        if (*domainID != beast::zero)
        {
            sle->setFieldH256(sfDomainID, *domainID);
        }
        else
        {
            if (sle->isFieldPresent(sfDomainID))
                sle->makeFieldAbsent(sfDomainID);
        }
    }

    view().update(sle);

    return tesSUCCESS;
}

}  // namespace ripple
