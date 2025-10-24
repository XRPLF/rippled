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

#include <xrpld/app/tx/detail/PermissionedDomainDelete.h>

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
PermissionedDomainDelete::preflight(PreflightContext const& ctx)
{
    auto const domain = ctx.tx.getFieldH256(sfDomainID);
    if (domain == beast::zero)
        return temMALFORMED;

    return tesSUCCESS;
}

TER
PermissionedDomainDelete::preclaim(PreclaimContext const& ctx)
{
    auto const domain = ctx.tx.getFieldH256(sfDomainID);
    auto const sleDomain = ctx.view.read({ltPERMISSIONED_DOMAIN, domain});

    if (!sleDomain)
        return tecNO_ENTRY;

    XRPL_ASSERT(
        sleDomain->isFieldPresent(sfOwner) && ctx.tx.isFieldPresent(sfAccount),
        "ripple::PermissionedDomainDelete::preclaim : required fields present");
    if (sleDomain->getAccountID(sfOwner) != ctx.tx.getAccountID(sfAccount))
        return tecNO_PERMISSION;

    return tesSUCCESS;
}

/** Attempt to delete the Permissioned Domain. */
TER
PermissionedDomainDelete::doApply()
{
    XRPL_ASSERT(
        ctx_.tx.isFieldPresent(sfDomainID),
        "ripple::PermissionedDomainDelete::doApply : required field present");

    auto const slePd =
        view().peek({ltPERMISSIONED_DOMAIN, ctx_.tx.at(sfDomainID)});
    auto const page = (*slePd)[sfOwnerNode];

    if (!view().dirRemove(keylet::ownerDir(account_), page, slePd->key(), true))
    {
        // LCOV_EXCL_START
        JLOG(j_.fatal())
            << "Unable to delete permissioned domain directory entry.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    auto const ownerSle = view().peek(keylet::account(account_));
    XRPL_ASSERT(
        ownerSle && ownerSle->getFieldU32(sfOwnerCount) > 0,
        "ripple::PermissionedDomainDelete::doApply : nonzero owner count");
    adjustOwnerCount(view(), ownerSle, -1, ctx_.journal);
    view().erase(slePd);

    return tesSUCCESS;
}

}  // namespace ripple
