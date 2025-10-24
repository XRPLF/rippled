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

#include <xrpl/ledger/CredentialHelpers.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/digest.h>

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
removeExpired(ApplyView& view, STVector256 const& arr, beast::Journal const j)
{
    auto const closeTime = view.info().parentCloseTime;
    bool foundExpired = false;

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
            // LCOV_EXCL_START
            JLOG(j.fatal()) << "Internal error: can't retrieve Owner account.";
            return tecINTERNAL;
            // LCOV_EXCL_STOP
        }

        // Remove object from owner directory
        std::uint64_t const page = sleCredential->getFieldU64(node);
        if (!view.dirRemove(
                keylet::ownerDir(account), page, sleCredential->key(), false))
        {
            // LCOV_EXCL_START
            JLOG(j.fatal()) << "Unable to delete Credential from owner.";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
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
checkFields(STTx const& tx, beast::Journal j)
{
    if (!tx.isFieldPresent(sfCredentialIDs))
        return tesSUCCESS;

    auto const& credentials = tx.getFieldV256(sfCredentialIDs);
    if (credentials.empty() || (credentials.size() > maxCredentialsArraySize))
    {
        JLOG(j.trace())
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
            JLOG(j.trace())
                << "Malformed transaction: duplicates in credentials.";
            return temMALFORMED;
        }
    }

    return tesSUCCESS;
}

TER
valid(
    STTx const& tx,
    ReadView const& view,
    AccountID const& src,
    beast::Journal j)
{
    if (!tx.isFieldPresent(sfCredentialIDs))
        return tesSUCCESS;

    auto const& credIDs(tx.getFieldV256(sfCredentialIDs));
    for (auto const& h : credIDs)
    {
        auto const sleCred = view.read(keylet::credential(h));
        if (!sleCred)
        {
            JLOG(j.trace()) << "Credential doesn't exist. Cred: " << h;
            return tecBAD_CREDENTIALS;
        }

        if (sleCred->getAccountID(sfSubject) != src)
        {
            JLOG(j.trace())
                << "Credential doesn't belong to the source account. Cred: "
                << h;
            return tecBAD_CREDENTIALS;
        }

        if (!(sleCred->getFlags() & lsfAccepted))
        {
            JLOG(j.trace()) << "Credential isn't accepted. Cred: " << h;
            return tecBAD_CREDENTIALS;
        }

        // Expiration checks are in doApply
    }

    return tesSUCCESS;
}

TER
validDomain(ReadView const& view, uint256 domainID, AccountID const& subject)
{
    // Note, permissioned domain objects can be deleted at any time
    auto const slePD = view.read(keylet::permissionedDomain(domainID));
    if (!slePD)
        return tecOBJECT_NOT_FOUND;

    auto const closeTime = view.info().parentCloseTime;
    bool foundExpired = false;
    for (auto const& h : slePD->getFieldArray(sfAcceptedCredentials))
    {
        auto const issuer = h.getAccountID(sfIssuer);
        auto const type = h.getFieldVL(sfCredentialType);
        auto const keyletCredential =
            keylet::credential(subject, issuer, makeSlice(type));
        auto const sleCredential = view.read(keyletCredential);

        // We cannot delete expired credentials, that would require ApplyView&
        // However we can check if credentials are expired. Expected transaction
        // flow is to use `validDomain` in preclaim, converting tecEXPIRED to
        // tesSUCCESS, then proceed to call `verifyValidDomain` in doApply. This
        // allows expired credentials to be deleted by any transaction.
        if (sleCredential)
        {
            if (checkExpired(sleCredential, closeTime))
            {
                foundExpired = true;
                continue;
            }
            else if (sleCredential->getFlags() & lsfAccepted)
                return tesSUCCESS;
            else
                continue;
        }
    }

    return foundExpired ? tecEXPIRED : tecNO_AUTH;
}

TER
authorizedDepositPreauth(
    ApplyView const& view,
    STVector256 const& credIDs,
    AccountID const& dst)
{
    std::set<std::pair<AccountID, Slice>> sorted;
    std::vector<std::shared_ptr<SLE const>> lifeExtender;
    lifeExtender.reserve(credIDs.size());
    for (auto const& h : credIDs)
    {
        auto sleCred = view.read(keylet::credential(h));
        if (!sleCred)            // already checked in preclaim
            return tefINTERNAL;  // LCOV_EXCL_LINE

        auto [it, ins] =
            sorted.emplace((*sleCred)[sfIssuer], (*sleCred)[sfCredentialType]);
        if (!ins)
            return tefINTERNAL;  // LCOV_EXCL_LINE
        lifeExtender.push_back(std::move(sleCred));
    }

    if (!view.exists(keylet::depositPreauth(dst, sorted)))
        return tecNO_PERMISSION;

    return tesSUCCESS;
}

std::set<std::pair<AccountID, Slice>>
makeSorted(STArray const& credentials)
{
    std::set<std::pair<AccountID, Slice>> out;
    for (auto const& cred : credentials)
    {
        auto [it, ins] = out.emplace(cred[sfIssuer], cred[sfCredentialType]);
        if (!ins)
            return {};
    }
    return out;
}

NotTEC
checkArray(STArray const& credentials, unsigned maxSize, beast::Journal j)
{
    if (credentials.empty() || (credentials.size() > maxSize))
    {
        JLOG(j.trace()) << "Malformed transaction: "
                           "Invalid credentials size: "
                        << credentials.size();
        return credentials.empty() ? temARRAY_EMPTY : temARRAY_TOO_LARGE;
    }

    std::unordered_set<uint256> duplicates;
    for (auto const& credential : credentials)
    {
        auto const& issuer = credential[sfIssuer];
        if (!issuer)
        {
            JLOG(j.trace()) << "Malformed transaction: "
                               "Issuer account is invalid: "
                            << to_string(issuer);
            return temINVALID_ACCOUNT_ID;
        }

        auto const ct = credential[sfCredentialType];
        if (ct.empty() || (ct.size() > maxCredentialTypeLength))
        {
            JLOG(j.trace()) << "Malformed transaction: "
                               "Invalid credentialType size: "
                            << ct.size();
            return temMALFORMED;
        }

        auto [it, ins] = duplicates.insert(sha512Half(issuer, ct));
        if (!ins)
        {
            JLOG(j.trace()) << "Malformed transaction: "
                               "duplicates in credenentials.";
            return temMALFORMED;
        }
    }

    return tesSUCCESS;
}

}  // namespace credentials

TER
verifyValidDomain(
    ApplyView& view,
    AccountID const& account,
    uint256 domainID,
    beast::Journal j)
{
    auto const slePD = view.read(keylet::permissionedDomain(domainID));
    if (!slePD)
        return tecOBJECT_NOT_FOUND;

    // Collect all matching credentials on a side, so we can remove expired ones
    // We may finish the loop with this collection empty, it's fine.
    STVector256 credentials;
    for (auto const& h : slePD->getFieldArray(sfAcceptedCredentials))
    {
        auto const issuer = h.getAccountID(sfIssuer);
        auto const type = h.getFieldVL(sfCredentialType);
        auto const keyletCredential =
            keylet::credential(account, issuer, makeSlice(type));
        if (view.exists(keyletCredential))
            credentials.push_back(keyletCredential.key);
    }

    bool const foundExpired = credentials::removeExpired(view, credentials, j);
    for (auto const& h : credentials)
    {
        auto sleCredential = view.read(keylet::credential(h));
        if (!sleCredential)
            continue;  // expired, i.e. deleted in credentials::removeExpired

        if (sleCredential->getFlags() & lsfAccepted)
            return tesSUCCESS;
    }

    return foundExpired ? tecEXPIRED : tecNO_PERMISSION;
}

TER
verifyDepositPreauth(
    STTx const& tx,
    ApplyView& view,
    AccountID const& src,
    AccountID const& dst,
    std::shared_ptr<SLE> const& sleDst,
    beast::Journal j)
{
    // If depositPreauth is enabled, then an account that requires
    // authorization has at least two ways to get a payment in:
    //  1. If src == dst, or
    //  2. If src is deposit preauthorized by dst (either by account or by
    //  credentials).

    bool const credentialsPresent = tx.isFieldPresent(sfCredentialIDs);

    if (credentialsPresent &&
        credentials::removeExpired(view, tx.getFieldV256(sfCredentialIDs), j))
        return tecEXPIRED;

    if (sleDst && (sleDst->getFlags() & lsfDepositAuth))
    {
        if (src != dst)
        {
            if (!view.exists(keylet::depositPreauth(dst, src)))
                return !credentialsPresent
                    ? tecNO_PERMISSION
                    : credentials::authorizedDepositPreauth(
                          view, tx.getFieldV256(sfCredentialIDs), dst);
        }
    }

    return tesSUCCESS;
}

}  // namespace ripple
