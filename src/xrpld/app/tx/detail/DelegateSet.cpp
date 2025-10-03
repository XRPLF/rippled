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

#include <xrpld/app/tx/detail/DelegateSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/st.h>

namespace ripple {

NotTEC
DelegateSet::preflight(PreflightContext const& ctx)
{
    auto const& permissions = ctx.tx.getFieldArray(sfPermissions);
    if (permissions.size() > permissionMaxSize)
        return temARRAY_TOO_LARGE;

    // can not authorize self
    if (ctx.tx[sfAccount] == ctx.tx[sfAuthorize])
        return temMALFORMED;

    std::unordered_set<std::uint32_t> permissionSet;

    for (auto const& permission : permissions)
    {
        if (!permissionSet.insert(permission[sfPermissionValue]).second)
            return temMALFORMED;

        if (!Permission::getInstance().isDelegatable(
                permission[sfPermissionValue], ctx.rules))
            return temMALFORMED;
    }

    return tesSUCCESS;
}

TER
DelegateSet::preclaim(PreclaimContext const& ctx)
{
    if (!ctx.view.exists(keylet::account(ctx.tx[sfAccount])))
        return terNO_ACCOUNT;  // LCOV_EXCL_LINE

    if (!ctx.view.exists(keylet::account(ctx.tx[sfAuthorize])))
        return tecNO_TARGET;

    return tesSUCCESS;
}

TER
DelegateSet::doApply()
{
    auto const sleOwner = ctx_.view().peek(keylet::account(account_));
    if (!sleOwner)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const& authAccount = ctx_.tx[sfAuthorize];
    auto const delegateKey = keylet::delegate(account_, authAccount);

    auto sle = ctx_.view().peek(delegateKey);
    if (sle)
    {
        auto const& permissions = ctx_.tx.getFieldArray(sfPermissions);
        if (permissions.empty())
            // if permissions array is empty, delete the ledger object.
            return deleteDelegate(view(), sle, account_, j_);

        sle->setFieldArray(sfPermissions, permissions);
        ctx_.view().update(sle);
        return tesSUCCESS;
    }

    STAmount const reserve{ctx_.view().fees().accountReserve(
        sleOwner->getFieldU32(sfOwnerCount) + 1)};

    if (mPriorBalance < reserve)
        return tecINSUFFICIENT_RESERVE;

    auto const& permissions = ctx_.tx.getFieldArray(sfPermissions);
    if (!permissions.empty())
    {
        sle = std::make_shared<SLE>(delegateKey);
        sle->setAccountID(sfAccount, account_);
        sle->setAccountID(sfAuthorize, authAccount);

        sle->setFieldArray(sfPermissions, permissions);
        auto const page = ctx_.view().dirInsert(
            keylet::ownerDir(account_),
            delegateKey,
            describeOwnerDir(account_));

        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE

        (*sle)[sfOwnerNode] = *page;
        ctx_.view().insert(sle);
        adjustOwnerCount(ctx_.view(), sleOwner, 1, ctx_.journal);
    }

    return tesSUCCESS;
}

TER
DelegateSet::deleteDelegate(
    ApplyView& view,
    std::shared_ptr<SLE> const& sle,
    AccountID const& account,
    beast::Journal j)
{
    if (!sle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (!view.dirRemove(
            keylet::ownerDir(account), (*sle)[sfOwnerNode], sle->key(), false))
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Unable to delete Delegate from owner.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    auto const sleOwner = view.peek(keylet::account(account));
    if (!sleOwner)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    adjustOwnerCount(view, sleOwner, -1, j);

    view.erase(sle);

    return tesSUCCESS;
}

}  // namespace ripple
