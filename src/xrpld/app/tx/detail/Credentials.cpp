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

//------------------------------------------------------------------------------

bool
credentialCheckExpired(
    std::shared_ptr<SLE const> const& sle,
    NetClock::time_point const& closed)
{
    std::uint32_t const exp =
        sle->isFieldPresent(sfExpiration) ? sle->getFieldU32(sfExpiration) : 0;
    std::uint32_t const now = closed.time_since_epoch().count();
    return static_cast<bool>(exp) && (now > exp);
}

// special check for deletion
static bool
credentialCheckNotExpired(
    std::shared_ptr<SLE const> const& sle,
    NetClock::time_point const& closed)
{
    if (!sle->isFieldPresent(sfExpiration))
        return false;

    std::uint32_t const exp = sle->getFieldU32(sfExpiration);
    std::uint32_t const now = closed.time_since_epoch().count();

    return now <= exp;
}

Blob
signCredential(
    PublicKey const& signerPK,
    SecretKey const& signerSK,
    AccountID const& subject,
    std::string_view credType,
    std::optional<AccountID> const& masterIssuer)
{
    AccountID const issuer(
        masterIssuer ? *masterIssuer : calcAccountID(signerPK));
    Slice sct(credType.data(), credType.size());
    auto const kCred = keylet::credential(subject, issuer, sct);

    Serializer msg;
    msg.add32(HashPrefix::credential);
    msg.addBitString(kCred.key);

    auto const b = sign(signerPK, signerSK, msg.slice());
    return Blob(b.cbegin(), b.cend());
}

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

    auto const subject = tx[~sfSubject];
    auto const issuer = tx[~sfIssuer];
    auto const issuerPubKey = tx[~sfIssuerPubKey];
    auto const signature = tx[~sfSignature];

    if ((subject.has_value() == issuer.has_value()) ||
        (issuer.has_value() != issuerPubKey.has_value()) ||
        (issuer.has_value() != signature.has_value()))
    {
        // Either both fields are present or neither field is present.  In
        // either case the transaction is malformed.
        JLOG(j.trace())
            << "Malformed transaction: "
               "Invalid Subject, Issuer and Signature fields combination.";
        return temMALFORMED;
    }

    if (subject && subject->isZero())
    {
        JLOG(j.trace()) << "Malformed transaction: Subject field zeroed.";
        return temINVALID_ACCOUNT_ID;
    }

    if (issuer && issuer->isZero())
    {
        JLOG(j.trace()) << "Malformed transaction: Issuer field zeroed.";
        return temINVALID_ACCOUNT_ID;
    }

    auto const optUri = tx[~sfURI];
    if (optUri &&
        (optUri->empty() || (optUri->size() > maxCredentialURILength)))
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

    if (issuerPubKey && !publicKeyType(*issuerPubKey))
    {
        JLOG(j.trace()) << "Malformed transaction: invalid issuerPublicKey.";
        return temINVALID_ACCOUNT_ID;
    }

    if (signature)
    {
        PublicKey const pk(*issuerPubKey);
        auto const kCred = keylet::credential(
            tx[sfAccount], *issuer, ctx.tx[sfCredentialType]);

        Serializer msg;
        msg.add32(HashPrefix::credential);
        msg.addBitString(kCred.key);

        if (!verify(pk, msg.slice(), *signature, /*canonical*/ true))
        {
            JLOG(j.trace()) << "Malformed transaction: bad signature.";
            return temBAD_SIGNATURE;
        }
    }

    return preflight2(ctx);
}

TER
CredentialCreate::preclaim(PreclaimContext const& ctx)
{
    auto const credType(ctx.tx[sfCredentialType]);
    AccountID const account(ctx.tx[sfAccount]);
    auto const subject = ctx.tx[~sfSubject].value_or(account);

    std::optional<AccountID> signer = ctx.tx.isFieldPresent(sfIssuerPubKey)
        ? std::optional<AccountID>(
              calcAccountID(PublicKey(ctx.tx[sfIssuerPubKey])))
        : std::nullopt;
    auto const issuer = ctx.tx[~sfIssuer].value_or(account);

    if (ctx.tx.isFieldPresent(sfSubject) &&
        !ctx.view.exists(keylet::account(subject)))
    {
        JLOG(ctx.j.trace()) << "Subject doesn't exist.";
        return tecNO_TARGET;
    }

    if (ctx.view.exists(keylet::credential(subject, issuer, credType)))
    {
        JLOG(ctx.j.trace()) << "Credential doesn't exist.";
        return tecDUPLICATE;
    }

    if (signer && (signer != issuer))
    {
        // If Issuer is not an account derived from IssuerPubKey then it  must
        // be the regular key for Issuer account.

        auto const sleIssuer = ctx.view.read(keylet::account(issuer));
        if (!sleIssuer)
        {
            JLOG(ctx.j.trace()) << "Issuer doesn't exist.";
            return tecNO_ISSUER;
        }

        if (sleIssuer->getAccountID(sfRegularKey) != signer)
        {
            JLOG(ctx.j.trace())
                << "Regular key doesn't belong to Issuer account.";
            return tecNO_REGULAR_KEY;
        }
    }

    return tesSUCCESS;
}

TER
CredentialCreate::doApply()
{
    auto const sleOwner = view().peek(keylet::account(account_));

    {
        STAmount const reserve{view().fees().accountReserve(
            sleOwner->getFieldU32(sfOwnerCount) + 1)};
        if (mPriorBalance < reserve)
            return tecINSUFFICIENT_RESERVE;
    }

    auto const subject = ctx_.tx[~sfSubject].value_or(account_);
    auto const issuer = ctx_.tx.isFieldPresent(sfIssuer)
        ? ctx_.tx[sfIssuer]
        : (ctx_.tx.isFieldPresent(sfIssuerPubKey)
               ? calcAccountID(PublicKey(ctx_.tx[sfIssuerPubKey]))
               : account_);
    auto const credType(ctx_.tx[sfCredentialType]);
    Keylet const kCred = keylet::credential(subject, issuer, credType);
    auto const sleCred = std::make_shared<SLE>(kCred);

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

    sleCred->setAccountID(sfSubject, subject);
    sleCred->setAccountID(sfIssuer, issuer);
    sleCred->setFieldVL(sfCredentialType, credType);

    if (ctx_.tx.isFieldPresent(sfURI))
        sleCred->setFieldVL(sfURI, ctx_.tx.getFieldVL(sfURI));

    if (!ctx_.tx.isFieldPresent(sfSubject))
        sleCred->setFieldU32(sfFlags, lsfAccepted);

    view().insert(sleCred);
    auto const page = view().dirInsert(
        keylet::ownerDir(account_), kCred, describeOwnerDir(account_));

    JLOG(j_.trace()) << "Adding Credential to owner directory "
                     << to_string(kCred.key) << ": "
                     << (page ? "success" : "failure");

    if (!page)
        return tecDIR_FULL;

    sleCred->setFieldU64(sfOwnerNode, *page);

    adjustOwnerCount(view(), sleOwner, 1, j_);
    view().update(sleOwner);

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
                               "Invalid Subject and Issuer fields combination.";
        return temMALFORMED;
    }

    // Make sure that the passed account is valid.
    if ((subject && !AccountID(*subject)) || (issuer && !AccountID(*issuer)))
    {
        JLOG(ctx.j.trace()) << "Malformed transaction: Subject or Issuer "
                               "field zeroed.";
        return temINVALID_ACCOUNT_ID;
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
CredentialDelete::deleteSLE(
    ApplyView& view,
    std::shared_ptr<SLE> const& sle,
    beast::Journal j)
{
    if (!sle)
        return tecNO_ENTRY;

    AccountID const owner = sle->getAccountID(
        (sle->getFlags() & lsfAccepted) ? sfSubject : sfIssuer);

    auto const sleOwner = view.peek(keylet::account(owner));
    if (!sleOwner)
    {
        JLOG(j.fatal()) << "Internal error: can't retrieve Owner account.";
        return tecINTERNAL;
    }

    // Remove object from owner directory
    std::uint64_t const page = sle->getFieldU64(sfOwnerNode);
    if (!view.dirRemove(keylet::ownerDir(owner), page, sle->key(), false))
    {
        JLOG(j.fatal()) << "Unable to delete Credential from owner.";
        return tefBAD_LEDGER;
    }

    adjustOwnerCount(view, sleOwner, -1, j);
    view.update(sleOwner);

    // Remove object from ledger
    view.erase(sle);

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

    if ((subject != account_) && (issuer != account_))
    {
        if (credentialCheckNotExpired(
                sleCred, ctx_.view().info().parentCloseTime))
        {
            JLOG(j_.trace()) << "Can't delete non-expired credential.";
            return tecNO_PERMISSION;
        }
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

    AccountID const issuer = ctx.tx[sfIssuer];
    if (!issuer)
    {
        JLOG(ctx.j.trace()) << "Malformed transaction: Issuer field zeroed.";
        return temINVALID_ACCOUNT_ID;
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
    AccountID const subject{account_};
    AccountID const issuer{ctx_.tx[sfIssuer]};

    auto const sleSubj = view().peek(keylet::account(subject));
    auto const sleIss = view().peek(keylet::account(issuer));

    {
        STAmount const reserve{view().fees().accountReserve(
            sleSubj->getFieldU32(sfOwnerCount) + 1)};
        if (mPriorBalance < reserve)
            return tecINSUFFICIENT_RESERVE;
    }

    auto const credType(ctx_.tx[sfCredentialType]);
    Keylet const kCred = keylet::credential(subject, issuer, credType);
    auto const sleCred = ctx_.view().peek(kCred);  // Checked in preclaim()

    if (credentialCheckExpired(sleCred, ctx_.view().info().parentCloseTime))
    {
        JLOG(j_.trace()) << "Credential is expired: " << sleCred->getText();
        // delete expired credentials even if the transaction failed
        CredentialDelete::deleteSLE(ctx_.view(), sleCred, j_);
        return tecEXPIRED;
    }

    // Change ownership from issuer to subject
    std::uint64_t const page = sleCred->getFieldU64(sfOwnerNode);
    if (!view().dirRemove(keylet::ownerDir(issuer), page, kCred, false))
    {
        JLOG(j_.fatal())
            << "CredentialAccept: Unable to delete Credential from owner.";
        return tefBAD_LEDGER;
    }
    adjustOwnerCount(view(), sleIss, -1, j_);
    view().update(sleIss);

    auto const pageIns = view().dirInsert(
        keylet::ownerDir(subject), kCred, describeOwnerDir(subject));

    JLOG(j_.trace()) << "Moving Credential to owner directory "
                     << to_string(kCred.key) << ": "
                     << (pageIns ? "success" : "failure");

    if (!pageIns)
        return tecDIR_FULL;

    sleCred->setFieldU32(sfFlags, lsfAccepted);
    sleCred->setFieldU64(sfOwnerNode, *pageIns);

    adjustOwnerCount(view(), sleSubj, 1, j_);
    view().update(sleCred);
    view().update(sleSubj);

    return tesSUCCESS;
}

}  // namespace ripple
