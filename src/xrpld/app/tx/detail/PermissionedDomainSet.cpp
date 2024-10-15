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

#include <xrpld/app/tx/detail/PermissionedDomainSet.h>
#include <xrpld/ledger/View.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TxFlags.h>
#include <map>
#include <optional>

namespace ripple {

NotTEC
PermissionedDomainSet::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featurePermissionedDomains))
        return temDISABLED;
    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto const credentials = ctx.tx.getFieldArray(sfAcceptedCredentials);
    if (credentials.empty() || credentials.size() > PD_ARRAY_MAX)
        return temMALFORMED;
    for (auto const& credential : credentials)
    {
        if (!credential.isFieldPresent(sfIssuer) ||
            !credential.isFieldPresent(sfCredentialType))
        {
            return temMALFORMED;
        }
        if (credential.getFieldVL(sfCredentialType).empty())
            return temMALFORMED;
    }

    auto const domain = ctx.tx.at(~sfDomainID);
    if (domain && *domain == beast::zero)
        return temMALFORMED;

    return preflight2(ctx);
}

TER
PermissionedDomainSet::preclaim(PreclaimContext const& ctx)
{
    if (!ctx.view.read(keylet::account(ctx.tx.getAccountID(sfAccount))))
        return tefINTERNAL;

    auto const credentials = ctx.tx.getFieldArray(sfAcceptedCredentials);
    for (auto const& credential : credentials)
    {
        if (!ctx.view.read(keylet::account(credential.getAccountID(sfIssuer))))
            return temBAD_ISSUER;
    }

    if (!ctx.tx.isFieldPresent(sfDomainID))
        return tesSUCCESS;
    auto const domain = ctx.tx.getFieldH256(sfDomainID);
    auto const sleDomain = ctx.view.read(keylet::permissionedDomain(domain));
    if (!sleDomain)
        return tecNO_ENTRY;
    auto const owner = sleDomain->getAccountID(sfOwner);
    auto account = ctx.tx.getAccountID(sfAccount);
    if (owner != account)
        return temINVALID_ACCOUNT_ID;

    return tesSUCCESS;
}

/** Attempt to create the Permissioned Domain. */
TER
PermissionedDomainSet::doApply()
{
    auto const ownerSle = view().peek(keylet::account(account_));

    // The purpose of this lambda is to modify the SLE for either creating a
    // new or updating an existing object, to reduce code repetition.
    // All checks have already been done. Just update credentials. Same logic
    // for either new domain or updating existing.
    // Silently remove duplicates.
    auto updateSle = [this](std::shared_ptr<STLedgerEntry> const& sle) {
        auto credentials = ctx_.tx.getFieldArray(sfAcceptedCredentials);
        std::map<uint256, STObject> hashed;
        for (auto const& c : credentials)
            hashed.insert({c.getHash(HashPrefix::transactionID), c});
        if (credentials.size() > hashed.size())
        {
            credentials = STArray();
            for (auto const& e : hashed)
                credentials.push_back(e.second);
        }

        credentials.sort(
            [](STObject const& left, STObject const& right) -> bool {
                if (left.getAccountID(sfIssuer) < right.getAccountID(sfIssuer))
                    return true;
                if (left.getAccountID(sfIssuer) == right.getAccountID(sfIssuer))
                {
                    if (left.getFieldVL(sfCredentialType) <
                        right.getFieldVL(sfCredentialType))
                    {
                        return true;
                    }
                }
                return false;
            });
        sle->setFieldArray(sfAcceptedCredentials, credentials);
    };

    if (ctx_.tx.isFieldPresent(sfDomainID))
    {
        // Modify existing permissioned domain.
        auto sleUpdate = view().peek(
            keylet::permissionedDomain(ctx_.tx.getFieldH256(sfDomainID)));
        // It should already be checked in preclaim().
        assert(sleUpdate);
        updateSle(sleUpdate);
        view().update(sleUpdate);
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
        slePd->setAccountID(sfOwner, account_);
        slePd->setFieldU32(sfSequence, ctx_.tx.getFieldU32(sfSequence));
        updateSle(slePd);
        auto const page = view().dirInsert(
            keylet::ownerDir(account_), pdKeylet, describeOwnerDir(account_));
        if (!page)
            return tecDIR_FULL;
        slePd->setFieldU64(sfOwnerNode, *page);
        // If we succeeded, the new entry counts against the creator's reserve.
        adjustOwnerCount(view(), ownerSle, 1, ctx_.journal);
        view().insert(slePd);
    }

    return tesSUCCESS;
}

}  // namespace ripple
