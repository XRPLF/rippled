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

#include <xrpld/app/misc/CredentialHelpers.h>
#include <xrpld/app/tx/detail/PermissionedDomainSet.h>
#include <xrpld/ledger/View.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TxFlags.h>

#include <optional>

namespace ripple {

NotTEC
PermissionedDomainSet::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featurePermissionedDomains) ||
        !ctx.rules.enabled(featureCredentials))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
    {
        JLOG(ctx.j.debug()) << "PermissionedDomainSet: invalid flags.";
        return temINVALID_FLAG;
    }

    if (auto err = credentials::checkArray(
            ctx.tx.getFieldArray(sfAcceptedCredentials),
            maxPermissionedDomainCredentialsArraySize,
            ctx.j);
        !isTesSuccess(err))
        return err;

    auto const domain = ctx.tx.at(~sfDomainID);
    if (domain && *domain == beast::zero)
        return temMALFORMED;

    return preflight2(ctx);
}

TER
PermissionedDomainSet::preclaim(PreclaimContext const& ctx)
{
    auto const account = ctx.tx.getAccountID(sfAccount);

    if (!ctx.view.exists(keylet::account(account)))
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const& credentials = ctx.tx.getFieldArray(sfAcceptedCredentials);
    for (auto const& credential : credentials)
    {
        if (!ctx.view.exists(
                keylet::account(credential.getAccountID(sfIssuer))))
            return tecNO_ISSUER;
    }

    if (ctx.tx.isFieldPresent(sfDomainID))
    {
        auto const sleDomain = ctx.view.read(
            keylet::permissionedDomain(ctx.tx.getFieldH256(sfDomainID)));
        if (!sleDomain)
            return tecNO_ENTRY;
        if (sleDomain->getAccountID(sfOwner) != account)
            return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

/** Attempt to create the Permissioned Domain. */
TER
PermissionedDomainSet::doApply()
{
    auto const ownerSle = view().peek(keylet::account(account_));
    if (!ownerSle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const sortedTxCredentials =
        credentials::makeSorted(ctx_.tx.getFieldArray(sfAcceptedCredentials));
    STArray sortedLE(sfAcceptedCredentials, sortedTxCredentials.size());
    for (auto const& p : sortedTxCredentials)
    {
        auto cred = STObject::makeInnerObject(sfCredential);
        cred.setAccountID(sfIssuer, p.first);
        cred.setFieldVL(sfCredentialType, p.second);
        sortedLE.push_back(std::move(cred));
    }

    if (ctx_.tx.isFieldPresent(sfDomainID))
    {
        // Modify existing permissioned domain.
        auto slePd = view().peek(
            keylet::permissionedDomain(ctx_.tx.getFieldH256(sfDomainID)));
        if (!slePd)
            return tefINTERNAL;  // LCOV_EXCL_LINE
        slePd->peekFieldArray(sfAcceptedCredentials) = std::move(sortedLE);
        view().update(slePd);
    }
    else
    {
        // Create new permissioned domain.
        // Check reserve availability for new object creation
        auto const balance = STAmount((*ownerSle)[sfBalance]).xrp();
        auto const reserve =
            ctx_.view().fees().accountReserve((*ownerSle)[sfOwnerCount] + 1);
        if (balance < reserve)
            return tecINSUFFICIENT_RESERVE;

        Keylet const pdKeylet = keylet::permissionedDomain(
            account_, ctx_.tx.getFieldU32(sfSequence));
        auto slePd = std::make_shared<SLE>(pdKeylet);
        if (!slePd)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        slePd->setAccountID(sfOwner, account_);
        slePd->setFieldU32(sfSequence, ctx_.tx.getFieldU32(sfSequence));
        slePd->peekFieldArray(sfAcceptedCredentials) = std::move(sortedLE);
        auto const page = view().dirInsert(
            keylet::ownerDir(account_), pdKeylet, describeOwnerDir(account_));
        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE

        slePd->setFieldU64(sfOwnerNode, *page);
        // If we succeeded, the new entry counts against the creator's reserve.
        adjustOwnerCount(view(), ownerSle, 1, ctx_.journal);
        view().insert(slePd);
    }

    return tesSUCCESS;
}

}  // namespace ripple
