#include <xrpl/basics/Log.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/CredentialHelpers.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/transactors/credentials/CredentialDelete.h>

#include <chrono>

namespace xrpl {

using namespace credentials;

std::uint32_t
CredentialDelete::getFlagsMask(PreflightContext const& ctx)
{
    // 0 means "Allow any flags"
    return ctx.rules.enabled(fixInvalidTxFlags) ? tfUniversalMask : 0;
}

NotTEC
CredentialDelete::preflight(PreflightContext const& ctx)
{
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
        JLOG(ctx.j.trace()) << "Malformed transaction: invalid size of CredentialType.";
        return temMALFORMED;
    }

    return tesSUCCESS;
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
    auto const sleCred = view().peek(keylet::credential(subject, issuer, credType));
    if (!sleCred)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if ((subject != account_) && (issuer != account_) &&
        !checkExpired(sleCred, ctx_.view().header().parentCloseTime))
    {
        JLOG(j_.trace()) << "Can't delete non-expired credential.";
        return tecNO_PERMISSION;
    }

    return deleteSLE(view(), sleCred, j_);
}

}  // namespace xrpl
