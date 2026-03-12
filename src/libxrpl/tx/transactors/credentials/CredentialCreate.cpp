#include <xrpl/basics/Log.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/CredentialHelpers.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/transactors/credentials/CredentialCreate.h>

#include <chrono>

namespace xrpl {

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

std::uint32_t
CredentialCreate::getFlagsMask(PreflightContext const& ctx)
{
    // 0 means "Allow any flags"
    return ctx.rules.enabled(fixInvalidTxFlags) ? tfUniversalMask : 0;
}

NotTEC
CredentialCreate::preflight(PreflightContext const& ctx)
{
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
        JLOG(j.trace()) << "Malformed transaction: invalid size of CredentialType.";
        return temMALFORMED;
    }

    return tesSUCCESS;
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

    if (ctx.view.exists(keylet::credential(subject, ctx.tx[sfAccount], credType)))
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
    Keylet const credentialKey = keylet::credential(subject, account_, credType);

    auto const sleCred = std::make_shared<SLE>(credentialKey);
    if (!sleCred)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const optExp = ctx_.tx[~sfExpiration];
    if (optExp)
    {
        std::uint32_t const closeTime =
            ctx_.view().header().parentCloseTime.time_since_epoch().count();

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
        return tefINTERNAL;  // LCOV_EXCL_LINE

    {
        STAmount const reserve{
            view().fees().accountReserve(sleIssuer->getFieldU32(sfOwnerCount) + 1)};
        if (mPriorBalance < reserve)
            return tecINSUFFICIENT_RESERVE;
    }

    sleCred->setAccountID(sfSubject, subject);
    sleCred->setAccountID(sfIssuer, account_);
    sleCred->setFieldVL(sfCredentialType, credType);

    if (ctx_.tx.isFieldPresent(sfURI))
        sleCred->setFieldVL(sfURI, ctx_.tx.getFieldVL(sfURI));

    {
        auto const page =
            view().dirInsert(keylet::ownerDir(account_), credentialKey, describeOwnerDir(account_));
        JLOG(j_.trace()) << "Adding Credential to owner directory " << to_string(credentialKey.key)
                         << ": " << (page ? "success" : "failure");
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
        auto const page =
            view().dirInsert(keylet::ownerDir(subject), credentialKey, describeOwnerDir(subject));
        JLOG(j_.trace()) << "Adding Credential to owner directory " << to_string(credentialKey.key)
                         << ": " << (page ? "success" : "failure");
        if (!page)
            return tecDIR_FULL;
        sleCred->setFieldU64(sfSubjectNode, *page);
        view().update(view().peek(keylet::account(subject)));
    }

    view().insert(sleCred);

    return tesSUCCESS;
}

}  // namespace xrpl
