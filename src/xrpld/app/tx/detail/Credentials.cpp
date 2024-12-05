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
#include <xrpld/app/tx/detail/Credentials.h>

#include <xrpld/core/TimeKeeper.h>
#include <xrpld/ledger/ApplyView.h>
#include <xrpld/ledger/View.h>
#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/st.h>

#include <chrono>

namespace ripple {

/*
    Credentials
    ======

   A verifiable credentials (VC
   https://en.wikipedia.org/wiki/Verifiable_credentials), as defined by the W3C
   specification (https://www.w3.org/TR/vc-data-model-2.0/), is a
   secure and tamper-evident way to represent information about a subject, such
   as an individual, organization, or even an IoT device. These credentials are
   issued by a trusted entity and can be verified by third parties without
   directly involving the issuer at all.
*/

using namespace credentials;

// ------- CREATE --------------------------

NotTEC
CredentialCreate::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureCredentials))
    {
        JLOG(ctx.j.trace()) << "featureCredentials is disabled.";
        return temDISABLED;
    }

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto const& tx = ctx.tx;
    auto& j = ctx.j;

    if (!tx[sfSubject])
    {
        JLOG(j.trace()) << "Malformed transaction: Invalid Subject";
        return temMALFORMED;
    }

    auto const uri = tx[~sfURI];
    if (uri && (uri->empty() || (uri->size() > maxCredentialURILength)))
    {
        JLOG(j.trace()) << "Malformed transaction: invalid size of URI.";
        return temMALFORMED;
    }

    auto const credType = tx[sfCredentialType];
    if (credType.empty() || (credType.size() > maxCredentialTypeLength))
    {
        JLOG(j.trace())
            << "Malformed transaction: invalid size of CredentialType.";
        return temMALFORMED;
    }

    return preflight2(ctx);
}

TER
CredentialCreate::preclaim(PreclaimContext const& ctx)
{
    auto const credType(ctx.tx[sfCredentialType]);
    auto const subject = ctx.tx[sfSubject];

    if (!ctx.view.exists(keylet::account(subject)))
    {
        JLOG(ctx.j.trace()) << "Subject doesn't exist.";
        return tecNO_TARGET;
    }

    if (ctx.view.exists(
            keylet::credential(subject, ctx.tx[sfAccount], credType)))
    {
        JLOG(ctx.j.trace()) << "Credential already exists.";
        return tecDUPLICATE;
    }

    return tesSUCCESS;
}

TER
CredentialCreate::doApply()
{
    auto const subject = ctx_.tx[sfSubject];
    auto const credType(ctx_.tx[sfCredentialType]);
    Keylet const credentialKey =
        keylet::credential(subject, account_, credType);

    auto const sleCred = std::make_shared<SLE>(credentialKey);
    if (!sleCred)
        return tefINTERNAL;

    auto const optExp = ctx_.tx[~sfExpiration];
    if (optExp)
    {
        std::uint32_t const closeTime =
            ctx_.view().info().parentCloseTime.time_since_epoch().count();

        if (closeTime > *optExp)
        {
            JLOG(j_.trace()) << "Malformed transaction: "
                                "Expiration time is in the past.";
            return tecEXPIRED;
        }

        sleCred->setFieldU32(sfExpiration, ctx_.tx.getFieldU32(sfExpiration));
    }

    auto const sleIssuer = view().peek(keylet::account(account_));
    if (!sleIssuer)
        return tefINTERNAL;

    {
        STAmount const reserve{view().fees().accountReserve(
            sleIssuer->getFieldU32(sfOwnerCount) + 1)};
        if (mPriorBalance < reserve)
            return tecINSUFFICIENT_RESERVE;
    }

    sleCred->setAccountID(sfSubject, subject);
    sleCred->setAccountID(sfIssuer, account_);
    sleCred->setFieldVL(sfCredentialType, credType);

    if (ctx_.tx.isFieldPresent(sfURI))
        sleCred->setFieldVL(sfURI, ctx_.tx.getFieldVL(sfURI));

    {
        auto const page = view().dirInsert(
            keylet::ownerDir(account_),
            credentialKey,
            describeOwnerDir(account_));
        JLOG(j_.trace()) << "Adding Credential to owner directory "
                         << to_string(credentialKey.key) << ": "
                         << (page ? "success" : "failure");
        if (!page)
            return tecDIR_FULL;
        sleCred->setFieldU64(sfIssuerNode, *page);

        adjustOwnerCount(view(), sleIssuer, 1, j_);
    }

    if (subject == account_)
    {
        sleCred->setFieldU32(sfFlags, lsfAccepted);
    }
    else
    {
        auto const page = view().dirInsert(
            keylet::ownerDir(subject),
            credentialKey,
            describeOwnerDir(subject));
        JLOG(j_.trace()) << "Adding Credential to owner directory "
                         << to_string(credentialKey.key) << ": "
                         << (page ? "success" : "failure");
        if (!page)
            return tecDIR_FULL;
        sleCred->setFieldU64(sfSubjectNode, *page);
        view().update(view().peek(keylet::account(subject)));
    }

    view().insert(sleCred);

    return tesSUCCESS;
}

// ------- DELETE --------------------------
NotTEC
CredentialDelete::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureCredentials))
    {
        JLOG(ctx.j.trace()) << "featureCredentials is disabled.";
        return temDISABLED;
    }

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto const subject = ctx.tx[~sfSubject];
    auto const issuer = ctx.tx[~sfIssuer];

    if (!subject && !issuer)
    {
        // Neither field is present, the transaction is malformed.
        JLOG(ctx.j.trace()) << "Malformed transaction: "
                               "No Subject or Issuer fields.";
        return temMALFORMED;
    }

    // Make sure that the passed account is valid.
    if ((subject && subject->isZero()) || (issuer && issuer->isZero()))
    {
        JLOG(ctx.j.trace()) << "Malformed transaction: Subject or Issuer "
                               "field zeroed.";
        return temINVALID_ACCOUNT_ID;
    }

    auto const credType = ctx.tx[sfCredentialType];
    if (credType.empty() || (credType.size() > maxCredentialTypeLength))
    {
        JLOG(ctx.j.trace())
            << "Malformed transaction: invalid size of CredentialType.";
        return temMALFORMED;
    }

    return preflight2(ctx);
}

TER
CredentialDelete::preclaim(PreclaimContext const& ctx)
{
    AccountID const account{ctx.tx[sfAccount]};
    auto const subject = ctx.tx[~sfSubject].value_or(account);
    auto const issuer = ctx.tx[~sfIssuer].value_or(account);
    auto const credType(ctx.tx[sfCredentialType]);

    if (!ctx.view.exists(keylet::credential(subject, issuer, credType)))
        return tecNO_ENTRY;

    return tesSUCCESS;
}

TER
CredentialDelete::doApply()
{
    auto const subject = ctx_.tx[~sfSubject].value_or(account_);
    auto const issuer = ctx_.tx[~sfIssuer].value_or(account_);

    auto const credType(ctx_.tx[sfCredentialType]);
    auto const sleCred =
        view().peek(keylet::credential(subject, issuer, credType));
    if (!sleCred)
        return tefINTERNAL;

    if ((subject != account_) && (issuer != account_) &&
        !checkExpired(sleCred, ctx_.view().info().parentCloseTime))
    {
        JLOG(j_.trace()) << "Can't delete non-expired credential.";
        return tecNO_PERMISSION;
    }

    return deleteSLE(view(), sleCred, j_);
}

// ------- APPLY --------------------------

NotTEC
CredentialAccept::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureCredentials))
    {
        JLOG(ctx.j.trace()) << "featureCredentials is disabled.";
        return temDISABLED;
    }

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (!ctx.tx[sfIssuer])
    {
        JLOG(ctx.j.trace()) << "Malformed transaction: Issuer field zeroed.";
        return temINVALID_ACCOUNT_ID;
    }

    auto const credType = ctx.tx[sfCredentialType];
    if (credType.empty() || (credType.size() > maxCredentialTypeLength))
    {
        JLOG(ctx.j.trace())
            << "Malformed transaction: invalid size of CredentialType.";
        return temMALFORMED;
    }

    return preflight2(ctx);
}

TER
CredentialAccept::preclaim(PreclaimContext const& ctx)
{
    AccountID const subject = ctx.tx[sfAccount];
    AccountID const issuer = ctx.tx[sfIssuer];
    auto const credType(ctx.tx[sfCredentialType]);

    if (!ctx.view.exists(keylet::account(issuer)))
    {
        JLOG(ctx.j.warn()) << "No issuer: " << to_string(issuer);
        return tecNO_ISSUER;
    }

    auto const sleCred =
        ctx.view.read(keylet::credential(subject, issuer, credType));
    if (!sleCred)
    {
        JLOG(ctx.j.warn()) << "No credential: " << to_string(subject) << ", "
                           << to_string(issuer) << ", " << credType;
        return tecNO_ENTRY;
    }

    if (sleCred->getFieldU32(sfFlags) & lsfAccepted)
    {
        JLOG(ctx.j.warn()) << "Credential already accepted: "
                           << to_string(subject) << ", " << to_string(issuer)
                           << ", " << credType;
        return tecDUPLICATE;
    }

    return tesSUCCESS;
}

TER
CredentialAccept::doApply()
{
    AccountID const issuer{ctx_.tx[sfIssuer]};

    // Both exist as credential object exist itself (checked in preclaim)
    auto const sleSubject = view().peek(keylet::account(account_));
    auto const sleIssuer = view().peek(keylet::account(issuer));

    if (!sleSubject || !sleIssuer)
        return tefINTERNAL;

    {
        STAmount const reserve{view().fees().accountReserve(
            sleSubject->getFieldU32(sfOwnerCount) + 1)};
        if (mPriorBalance < reserve)
            return tecINSUFFICIENT_RESERVE;
    }

    auto const credType(ctx_.tx[sfCredentialType]);
    Keylet const credentialKey = keylet::credential(account_, issuer, credType);
    auto const sleCred = view().peek(credentialKey);  // Checked in preclaim()

    if (checkExpired(sleCred, view().info().parentCloseTime))
    {
        JLOG(j_.trace()) << "Credential is expired: " << sleCred->getText();
        // delete expired credentials even if the transaction failed
        auto const err = credentials::deleteSLE(view(), sleCred, j_);
        return isTesSuccess(err) ? tecEXPIRED : err;
    }

    sleCred->setFieldU32(sfFlags, lsfAccepted);
    view().update(sleCred);

    adjustOwnerCount(view(), sleIssuer, -1, j_);
    adjustOwnerCount(view(), sleSubject, 1, j_);

    return tesSUCCESS;
}

}  // namespace ripple
