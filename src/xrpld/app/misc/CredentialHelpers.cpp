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
#include <xrpld/ledger/View.h>

#include <unordered_set>

namespace ripple {
namespace credentials {

bool
checkExpired(
    std::shared_ptr<SLE const> const& sleCredential,
    NetClock::time_point const& closed)
{
    std::uint32_t const exp = (*sleCredential)[~sfExpiration].value_or(
        std::numeric_limits<std::uint32_t>::max());
    std::uint32_t const now = closed.time_since_epoch().count();
    return now > exp;
}

bool
removeExpired(ApplyView& view, STTx const& tx, beast::Journal const j)
{
    auto const closeTime = view.info().parentCloseTime;
    bool foundExpired = false;

    STVector256 const& arr(tx.getFieldV256(sfCredentialIDs));
    for (auto const& h : arr)
    {
        // Credentials already checked in preclaim. Look only for expired here.
        auto const k = keylet::credential(h);
        auto const sleCred = view.peek(k);

        if (sleCred && checkExpired(sleCred, closeTime))
        {
            JLOG(j.trace())
                << "Credentials are expired. Cred: " << sleCred->getText();
            // delete expired credentials even if the transaction failed
            deleteSLE(view, sleCred, j);
            foundExpired = true;
        }
    }

    return foundExpired;
}

TER
deleteSLE(
    ApplyView& view,
    std::shared_ptr<SLE> const& sleCredential,
    beast::Journal j)
{
    if (!sleCredential)
        return tecNO_ENTRY;

    auto delSLE =
        [&view, &sleCredential, j](
            AccountID const& account, SField const& node, bool isOwner) -> TER {
        auto const sleAccount = view.peek(keylet::account(account));
        if (!sleAccount)
        {
            JLOG(j.fatal()) << "Internal error: can't retrieve Owner account.";
            return tecINTERNAL;
        }

        // Remove object from owner directory
        std::uint64_t const page = sleCredential->getFieldU64(node);
        if (!view.dirRemove(
                keylet::ownerDir(account), page, sleCredential->key(), false))
        {
            JLOG(j.fatal()) << "Unable to delete Credential from owner.";
            return tefBAD_LEDGER;
        }

        if (isOwner)
            adjustOwnerCount(view, sleAccount, -1, j);

        return tesSUCCESS;
    };

    auto const issuer = sleCredential->getAccountID(sfIssuer);
    auto const subject = sleCredential->getAccountID(sfSubject);
    bool const accepted = sleCredential->getFlags() & lsfAccepted;

    auto err = delSLE(issuer, sfIssuerNode, !accepted || (subject == issuer));
    if (!isTesSuccess(err))
        return err;

    if (subject != issuer)
    {
        err = delSLE(subject, sfSubjectNode, accepted);
        if (!isTesSuccess(err))
            return err;
    }

    // Remove object from ledger
    view.erase(sleCredential);

    return tesSUCCESS;
}

NotTEC
checkFields(PreflightContext const& ctx)
{
    if (!ctx.tx.isFieldPresent(sfCredentialIDs))
        return tesSUCCESS;

    auto const& credentials = ctx.tx.getFieldV256(sfCredentialIDs);
    if (credentials.empty() || (credentials.size() > maxCredentialsArraySize))
    {
        JLOG(ctx.j.trace())
            << "Malformed transaction: Credentials array size is invalid: "
            << credentials.size();
        return temMALFORMED;
    }

    std::unordered_set<uint256> duplicates;
    for (auto const& cred : credentials)
    {
        auto [it, ins] = duplicates.insert(cred);
        if (!ins)
        {
            JLOG(ctx.j.trace())
                << "Malformed transaction: duplicates in credentials.";
            return temMALFORMED;
        }
    }

    return tesSUCCESS;
}

TER
valid(PreclaimContext const& ctx, AccountID const& src)
{
    if (!ctx.tx.isFieldPresent(sfCredentialIDs))
        return tesSUCCESS;

    auto const& credIDs(ctx.tx.getFieldV256(sfCredentialIDs));
    for (auto const& h : credIDs)
    {
        auto const sleCred = ctx.view.read(keylet::credential(h));
        if (!sleCred)
        {
            JLOG(ctx.j.trace()) << "Credential doesn't exist. Cred: " << h;
            return tecBAD_CREDENTIALS;
        }

        if (sleCred->getAccountID(sfSubject) != src)
        {
            JLOG(ctx.j.trace())
                << "Credential doesn’t belong to the source account. Cred: "
                << h;
            return tecBAD_CREDENTIALS;
        }

        if (!(sleCred->getFlags() & lsfAccepted))
        {
            JLOG(ctx.j.trace()) << "Credential isn't accepted. Cred: " << h;
            return tecBAD_CREDENTIALS;
        }

        // Expiration checks are in doApply
    }

    return tesSUCCESS;
}

TER
authorized(ApplyContext const& ctx, AccountID const& dst)
{
    auto const& credIDs(ctx.tx.getFieldV256(sfCredentialIDs));
    std::set<std::pair<AccountID, Slice>> sorted;
    std::vector<std::shared_ptr<SLE const>> lifeExtender;
    lifeExtender.reserve(credIDs.size());
    for (auto const& h : credIDs)
    {
        auto sleCred = ctx.view().read(keylet::credential(h));
        if (!sleCred)  // already checked in preclaim
            return tefINTERNAL;

        auto [it, ins] =
            sorted.emplace((*sleCred)[sfIssuer], (*sleCred)[sfCredentialType]);
        if (!ins)
            return tefINTERNAL;
        lifeExtender.push_back(std::move(sleCred));
    }

    if (!ctx.view().exists(keylet::depositPreauth(dst, sorted)))
    {
        JLOG(ctx.journal.trace()) << "DepositPreauth doesn't exist";
        return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

std::set<std::pair<AccountID, Slice>>
makeSorted(STArray const& in)
{
    std::set<std::pair<AccountID, Slice>> out;
    for (auto const& cred : in)
    {
        auto [it, ins] = out.emplace(cred[sfIssuer], cred[sfCredentialType]);
        if (!ins)
            return {};
    }
    return out;
}

}  // namespace credentials

TER
verifyDepositPreauth(
    ApplyContext& ctx,
    AccountID const& src,
    AccountID const& dst,
    std::shared_ptr<SLE> const& sleDst)
{
    // If depositPreauth is enabled, then an account that requires
    // authorization has at least two ways to get a payment in:
    //  1. If src == dst, or
    //  2. If src is deposit preauthorized by dst (either by account or by
    //  credentials).

    bool const credentialsPresent = ctx.tx.isFieldPresent(sfCredentialIDs);

    if (credentialsPresent &&
        credentials::removeExpired(ctx.view(), ctx.tx, ctx.journal))
        return tecEXPIRED;

    if (sleDst && (sleDst->getFlags() & lsfDepositAuth))
    {
        if (src != dst)
        {
            if (!ctx.view().exists(keylet::depositPreauth(dst, src)))
                return !credentialsPresent ? tecNO_PERMISSION
                                           : credentials::authorized(ctx, dst);
        }
    }

    return tesSUCCESS;
}

}  // namespace ripple
